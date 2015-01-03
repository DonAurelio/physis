// Licensed under the BSD license. See LICENSE.txt for more details.

#include "translator/mpi_cuda_translator.h"

#include "translator/translation_context.h"
#include "translator/translation_util.h"
#include "translator/mpi_runtime_builder.h"
#include "translator/mpi_cuda_runtime_builder.h"
#include "translator/reference_runtime_builder.h"
#include "translator/cuda_runtime_builder.h"
#include "translator/cuda_util.h"
#include "translator/rose_util.h"
#include "translator/builder_interface.h"
#include "translator/rose_ast_attribute.h"

namespace pu = physis::util;
namespace sb = SageBuilder;
namespace si = SageInterface;
namespace ru = physis::translator::rose_util;
namespace cu = physis::translator::cuda_util;

namespace physis {
namespace translator {


void MPICUDATranslator::FixAST() {
  if (validate_ast_) {
    si::fixVariableReferences(project_);
  }
}

std::string MPICUDATranslator::GetBoundarySuffix(int dim, bool fw) {
  return string(PS_STENCIL_MAP_BOUNDARY_SUFFIX_NAME) + "_" +
      toString(dim+1) + "_" +
      (fw ? PS_STENCIL_MAP_BOUNDARY_FW_SUFFIX_NAME :
       PS_STENCIL_MAP_BOUNDARY_BW_SUFFIX_NAME);
}

std::string MPICUDATranslator::GetBoundarySuffix() {
  return string(PS_STENCIL_MAP_BOUNDARY_SUFFIX_NAME);
}

MPICUDATranslator::MPICUDATranslator(const Configuration &config)
    : MPITranslator(config),
      cuda_trans_(new CUDATranslator(config)),
      boundary_kernel_width_name_("halo_width"),
      inner_prefix_("_inner") {
  grid_create_name_ = "__PSGridNewMPI";
  target_specific_macro_ = "PHYSIS_MPI_CUDA";
  flag_multistream_boundary_ = false;
  const pu::LuaValue *lv =
      config.Lookup(Configuration::MULTISTREAM_BOUNDARY);
  if (lv) {
    PSAssert(lv->get(flag_multistream_boundary_));
  }
  if (flag_multistream_boundary_) {
    LOG_INFO() << "Multistream boundary enabled\n";
  }
  validate_ast_ = true;
}

MPICUDATranslator::~MPICUDATranslator() {
  delete cuda_trans_;
}

void MPICUDATranslator::SetUp(SgProject *project,
                              TranslationContext *context,
                              BuilderInterface *rt_builder) {
  MPITranslator::SetUp(project, context, rt_builder);
  LOG_DEBUG() << "Parent setup done\n";
  cuda_trans_->SetUp(project, context, rt_builder);
  LOG_DEBUG() << "cuda_trans_ setup done\n";
}

void MPICUDATranslator::Finish() {
  cuda_trans_->Finish();
  MPITranslator::Finish();
}

SgBasicBlock* MPICUDATranslator::BuildRunInteriorKernelBody(
    StencilMap *stencil, SgFunctionParameterList *param) {
  LOG_DEBUG() << "Generating run stencil interior kernel body\n";  

  // Reuse BuildRunKernelBody function, and then redirect calls to
  // the inner kernel function
  vector<SgVariableDeclaration*> indices;
  SgBasicBlock *body = builder()->BuildRunKernelFuncBody(stencil, param, indices);
  const std::string &normal_kernel_name = stencil->getKernel()->get_name();
  const std::string &inner_kernel_name = normal_kernel_name 
      + inner_prefix_;
  LOG_DEBUG() << "normal kernel name: " << normal_kernel_name << "\n";
  LOG_DEBUG() << "inner kernel name: " << inner_kernel_name << "\n";
  SgFunctionDeclaration *inner_kernel =
      sb::buildNondefiningFunctionDeclaration(
          inner_kernel_name,
          stencil->getKernel()->get_type()->get_return_type(),
          isSgFunctionParameterList(
              si::copyStatement(stencil->getKernel()->get_parameterList())),
          global_scope_);
  
  ru::RedirectFunctionCalls
      (body, normal_kernel_name, inner_kernel);
  return body;
}

SgBasicBlock* MPICUDATranslator::BuildRunBoundaryKernelBody(
    StencilMap *stencil,  SgFunctionParameterList *param) {
  LOG_DEBUG() << "Generating run boundary kernel body\n";
  SgInitializedName *dom_arg = param->get_args()[0];    
  SgBasicBlock *block = sb::buildBasicBlock();
  si::attachComment(block, "Generated by " + string(__FUNCTION__));
  int dim = stencil->getNumDim();  
  SgExpression *min_field = sb::buildDotExp(
      sb::buildVarRefExp(dom_arg), sb::buildVarRefExp("local_min"));
  SgExpression *max_field = sb::buildDotExp(
      sb::buildVarRefExp(dom_arg), sb::buildVarRefExp("local_max"));
  SgExpression *width = sb::buildVarRefExp(boundary_kernel_width_name_);
  vector<SgExpression*> offset_exprs;
  for (int i = 0; i < dim-1; ++i) {
    offset_exprs.push_back(sb::buildVarRefExp("offset" + toString(i)));
  }
  
  SgExpressionPtrList index_args;
  if (dim < 3) {
    LOG_ERROR() << "not supported yet.\n";
    PSAbort(1);
  }

  // int x = blockIdx.x * blockDim.x + threadIdx.x;
  SgVariableDeclaration *x_index = sb::buildVariableDeclaration(
      "x",
      sb::buildIntType(),
      sb::buildAssignInitializer(
          Add(Add(Mul(cu::BuildCudaIdxExp(cu::kBlockIdxX),
                      cu::BuildCudaIdxExp(cu::kBlockDimX)),
                  cu::BuildCudaIdxExp(cu::kThreadIdxX)),
              offset_exprs[0])),
      block);
  
  // int y = blockIdx.y * blockDim.y + threadIdx.y;  
  SgVariableDeclaration *y_index = sb::buildVariableDeclaration(
      "y",
      sb::buildIntType(),
      sb::buildAssignInitializer(
          Add(Add(Mul(
              cu::BuildCudaIdxExp(cu::kBlockIdxY),
              cu::BuildCudaIdxExp(cu::kBlockDimY)),
                  cu::BuildCudaIdxExp(cu::kThreadIdxY)),
              offset_exprs[1])),
      block);
  
  SgVariableDeclaration *z_index = sb::buildVariableDeclaration
      ("z", sb::buildIntType(), NULL, block);

  si::appendStatement(x_index, block);
  si::appendStatement(y_index, block);

  index_args.push_back(sb::buildVarRefExp(x_index));
  index_args.push_back(sb::buildVarRefExp(y_index));

  SgExpression *dom_min_z = ArrayRef(min_field, Int(2));
  SgExpression *dom_max_z = ArrayRef(max_field, Int(2));
  
  SgVariableDeclaration *loop_index = z_index;      
  SgStatement *loop_init = sb::buildAssignStatement(
      sb::buildVarRefExp(loop_index),
      si::copyExpression(dom_min_z));
  SgStatement *loop_test = sb::buildExprStatement(
      sb::buildLessThanOp(sb::buildVarRefExp(loop_index),
                          Add(si::copyExpression(dom_min_z),
                              si::copyExpression(width))));
  
  index_args.push_back(sb::buildVarRefExp(loop_index));

  SgVariableDeclaration* t[] = {x_index, y_index};
  vector<SgVariableDeclaration*> range_checking_idx(t, t + 2);

  si::appendStatement(
      builder()->BuildDomainInclusionCheck(
          range_checking_idx, dom_arg,
          sb::buildReturnStmt()),
      block);
  si::appendStatement(loop_index, block);

  SgExpression *loop_incr =
      sb::buildPlusPlusOp(sb::buildVarRefExp(loop_index));
  SgFunctionCallExp *kernel_call
      = builder()->BuildKernelCall(stencil, index_args, param);
  SgBasicBlock *loop_body = sb::buildBasicBlock(
      sb::buildExprStatement(kernel_call));
  SgStatement *loop
      = sb::buildForStatement(
          loop_init, loop_test, loop_incr, loop_body);
  si::appendStatement(loop, block);

  loop_init = sb::buildAssignStatement(
      sb::buildVarRefExp(loop_index),
      Add(si::copyExpression(dom_min_z),
          si::copyExpression(width)));
  loop_test = sb::buildExprStatement(
      sb::buildLessThanOp(sb::buildVarRefExp(loop_index),
                          sb::buildSubtractOp(
                              si::copyExpression(dom_max_z),
                              si::copyExpression(width))));
  loop = sb::buildForStatement(loop_init, loop_test,
                               loop_incr, loop_body);
  si::appendStatement(BuildDomainInclusionInnerCheck(
      range_checking_idx, dom_arg,
      si::copyExpression(width), loop), block);
  loop_init = sb::buildAssignStatement(
      sb::buildVarRefExp(loop_index),
      sb::buildSubtractOp(si::copyExpression(dom_max_z),
                          si::copyExpression(width)));
  loop_test = sb::buildExprStatement(
      sb::buildLessThanOp(sb::buildVarRefExp(loop_index),
                          si::copyExpression(dom_max_z)));
  loop = sb::buildForStatement(loop_init, loop_test, loop_incr, loop_body);
  si::appendStatement(loop, block);
  si::deleteAST(dom_max_z);
  si::deleteAST(dom_min_z);
  si::deleteAST(width);
  return block;
}

SgIfStmt *MPICUDATranslator::BuildDomainInclusionInnerCheck(
    const vector<SgVariableDeclaration*> &indices,
    SgInitializedName *dom_arg, SgExpression *width,
    SgStatement *ifclause) const {
  SgExpression *test_all = NULL;
  ENUMERATE (dim, index_it, indices.begin(), indices.end()) {
    SgVariableDeclaration *idx = *index_it;    
    SgExpression *dom_min = ArrayRef(
        sb::buildDotExp(sb::buildVarRefExp(dom_arg),
                        sb::buildVarRefExp("local_min")),
        Int(dim));
    SgExpression *dom_max = ArrayRef(
        sb::buildDotExp(sb::buildVarRefExp(dom_arg),
                        sb::buildVarRefExp("local_max")),
        Int(dim));
    SgExpression *test = sb::buildOrOp(
        sb::buildLessThanOp(
            sb::buildVarRefExp(idx),
            Add(dom_min,
                si::copyExpression(width))),
        sb::buildGreaterOrEqualOp(
            sb::buildVarRefExp(idx),
            sb::buildSubtractOp(dom_max,
                                si::copyExpression(width))));
    if (test_all) {
      test_all = sb::buildOrOp(test_all, test);
    } else {
      test_all = test;
    }
  }
  SgIfStmt *ifstmt = sb::buildIfStmt(test_all, ifclause, NULL);
  si::deleteAST(width);
  return ifstmt;
}

static void AppendSetCacheConfig(SgFunctionSymbol *fs,
                                 cu::CudaFuncCache config,
                                 SgScopeStatement *scope) {
  PSAssert(fs);
  si::appendStatement(
      sb::buildExprStatement(
          cu::BuildCudaCallFuncSetCacheConfig(
              fs, config)),
      scope);
}

static void AppendSetCacheConfig(const string &func_name,
                                 cu::CudaFuncCache config,
                                 SgScopeStatement *scope) {
  AppendSetCacheConfig(
      si::lookupFunctionSymbolInParentScopes(func_name),
      config, scope);
}

void MPICUDATranslator::SetCacheConfig(
    StencilMap *smap, SgFunctionSymbol *fs,
    SgScopeStatement *function_body, bool overlap_enabled) {
  // Refactoring: This block is for cache configuration.
  if (cache_config_done_.find(fs) == cache_config_done_.end()) {
    AppendSetCacheConfig(fs, cu::cudaFuncCachePreferL1,
                         function_body);
    cache_config_done_.insert(fs);
    if (overlap_enabled) {
      if (flag_multistream_boundary_) {
        for (int i = 0; i < smap->getNumDim(); ++i) {
          for (int j = 0; j < 2; ++j) {
            AppendSetCacheConfig(
                smap->GetRunName() + GetBoundarySuffix(i, j),
                cu::cudaFuncCachePreferL1, function_body);
          }
        }
      } else {
        AppendSetCacheConfig(
            smap->GetRunName() + GetBoundarySuffix(),
            cu::cudaFuncCachePreferL1, function_body);
      }
      AppendSetCacheConfig(
          ru::getFunctionSymbol(smap->run_inner()),
          cu::cudaFuncCachePreferL1, function_body);
    }
  }
}

static std::string GetTypeDimSig(const std::string &s) {
  for (int i = 1; i <= PS_MAX_DIM; ++i) {
    vector<string> names;
    names.push_back("Float");
    names.push_back("Double");
    FOREACH (it, names.begin(), names.end()) {
      string key = (*it) + toString(i) + "D";
      if (s.find(key) != string::npos) return key;
    }
  }
  return "";
}

SgFunctionDeclaration *
MPICUDATranslator::BuildInteriorKernel(SgFunctionDeclaration *original)
    const {
  SgFunctionDeclaration *inner_version =
      isSgFunctionDeclaration(si::copyStatement(original));
  inner_version->set_name(original->get_name() + inner_prefix_);
  
  PSAssert(inner_version);
  Rose_STL_Container<SgNode*> calls =
      NodeQuery::querySubTree(inner_version, V_SgFunctionCallExp);

  FOREACH (it, calls.begin(), calls.end()) {
    SgFunctionCallExp *fc = isSgFunctionCallExp(*it);
    PSAssert(fc);
    SgFunctionDeclaration *decl = fc->getAssociatedFunctionDeclaration();
    PSAssert(decl);
    SgFunctionSymbol *sym = fc->getAssociatedFunctionSymbol();
    PSAssert(sym);
    string name = sym->get_name();
    if (startswith(name, get_addr_name_)) {
      if (startswith(name, get_addr_no_halo_name_)) continue;
      //replace getaddr with getaddrnohalo
      string key = GetTypeDimSig(name);
      PSAssert(key != "");
      name = get_addr_no_halo_name_ + key;
      LOG_INFO() << "Redirecting to " << name << "\n";
      ru::RedirectFunctionCall(
          fc, sb::buildFunctionRefExp(name));
      continue;
    }
    // No redirection for emits
    if (startswith(name, emit_addr_name_)) continue;
    // Redirect intra-kernel calls 
    if (decl->get_definingDeclaration()) {
      ru::RedirectFunctionCall(
          fc, sb::buildFunctionRefExp(name + inner_prefix_));
      continue;
    }
  }
  return inner_version;
}

static int GetMaximumNumberOfDimensions(SgFunctionDeclaration *func) {
  // TODO: This does not work for intra kernels
  // Possible fix: find the Kernel object, and get the parent
  // func Find The root kernel func, and then use the below logic to
  // get the number of possible max dim.
  SgFunctionParameterList *params = func->get_parameterList();
  SgInitializedNamePtrList &param_args = params->get_args();
  int dim = 0;
  ENUMERATE (i, it, param_args.begin(), param_args.end()) {
    SgInitializedName *p = *it;
    if (!ru::IsIntLikeType(p)) {
      dim = i;
      break;
    }
  }
  LOG_DEBUG() << "Kernel (" << string(func->get_name()) << ") max dim: "
              << dim << "\n";
  return dim;
  
}

// Generates per-boundary kernels. 
std::vector<SgFunctionDeclaration*>
MPICUDATranslator::BuildBoundaryKernel(SgFunctionDeclaration *original) {
  std::vector<SgFunctionDeclaration*> bkernels;
  int ndim = GetMaximumNumberOfDimensions(original);
  for (int i = 0; i < ndim; ++i) {
    for (int j = 0; j < 2; ++j) {
      string suffix = GetBoundarySuffix(i, j);
      LOG_INFO() << "Generating device function: "
                 << string(original->get_name() + suffix) << "\n";
      SgFunctionDeclaration *boundary_version =
          isSgFunctionDeclaration(si::copyStatement(original));
      boundary_version->set_name(original->get_name() + suffix);
      PSAssert(boundary_version);
      bkernels.push_back(boundary_version);
      Rose_STL_Container<SgNode*> calls =
          NodeQuery::querySubTree(boundary_version, V_SgFunctionCallExp);

      FOREACH (it, calls.begin(), calls.end()) {
        SgFunctionCallExp *fc = isSgFunctionCallExp(*it);
        PSAssert(fc);
        SgFunctionDeclaration *decl = fc->getAssociatedFunctionDeclaration();
        PSAssert(decl);
        SgFunctionSymbol *sym = fc->getAssociatedFunctionSymbol();
        PSAssert(sym);
        string name = sym->get_name();
        if (startswith(name, get_addr_name_)) {
          if (startswith(name, get_addr_no_halo_name_)) continue;
          if (!fc->attributeExists(StencilIndexAttribute::name)) continue;
          const StencilIndexList &sil =
              static_cast<StencilIndexAttribute*>(
                  fc->getAttribute("StencilIndexList"))->stencil_index_list();

          LOG_DEBUG() << "sil: " << sil << "\n";
          // TODO: the kernel might be used for smaller dimension
          // grids, but that kernel is not recognized as regular even
          // if it's really the case because ndim is used as num_dim param.
          if (!StencilIndexRegularOrder(sil, ndim)) continue;
          int offset = sil[i].offset;
          StencilIndexList t = sil;
          t[i].offset = 0;
          if (!StencilIndexSelf(t, ndim)) continue;
          if ((j == 0 && offset < 0) || ((j == 1) && offset > 0)) {
            continue;
          }
          string key = GetTypeDimSig(name);
          PSAssert(key != "");
          name = get_addr_no_halo_name_ + key;
          LOG_INFO() << "Redirecting to " << name << "\n";
          ru::RedirectFunctionCall(
              fc, sb::buildFunctionRefExp(name));
          continue;
        }
        // No redirection for emits
        if (startswith(name, emit_addr_name_)) continue;
        // Redirect intra-kernel calls
        if (decl->get_definingDeclaration()) {
          ru::RedirectFunctionCall(
              fc, sb::buildFunctionRefExp(name + suffix));
          continue;
        }
      }
    }
  }
  return bkernels;
}

void MPICUDATranslator::TranslateKernelDeclaration(
    SgFunctionDeclaration *node) {
  LOG_DEBUG() << "Translating to CUDA kernel\n";
  node->get_functionModifier().setCudaDevice();

  // e.g., PSGrid3DFloat -> __PSGrid3DFloatDev *
  Rose_STL_Container<SgNode*> exps =
      NodeQuery::querySubTree(node, V_SgInitializedName);
  FOREACH (it, exps.begin(), exps.end()) {
    SgInitializedName *exp = isSgInitializedName(*it);
    PSAssert(exp);
    SgType *cur_type = exp->get_type();
    GridType *gt = tx_->findGridType(cur_type);
    // not a grid type
    if (!gt) continue;
    SgType *new_type = sb::buildPointerType(
        builder()->BuildOnDeviceGridType(gt));
    exp->set_type(new_type);
  }

  // Replace PSGridDim to __PSGridDimDev
  Rose_STL_Container<SgNode*> gdim_calls =
      NodeQuery::querySubTree(node, V_SgFunctionCallExp);
  SgFunctionSymbol *gdim_dev =
      si::lookupFunctionSymbolInParentScopes(PS_GRID_DIM_DEV_NAME);
  FOREACH (it, gdim_calls.begin(), gdim_calls.end()) {
    SgFunctionCallExp *fc = isSgFunctionCallExp(*it);
    PSAssert(fc);
    if (ru::getFuncName(fc) != PS_GRID_DIM_NAME) 
      continue;
    ru::RedirectFunctionCall(
        fc, sb::buildFunctionRefExp(gdim_dev));               
  }

  if (flag_mpi_overlap_)
    si::insertStatementBefore(node, BuildInteriorKernel(node), false);

  if (flag_multistream_boundary_) {
    std::vector<SgFunctionDeclaration*> boundary_kernels
        = BuildBoundaryKernel(node);
    FOREACH (it, boundary_kernels.begin(), boundary_kernels.end()) {
      // Note: Assertion failure unless autoMovePreprocessingInfo is false.
      si::insertStatementBefore(node, *it, false);
    }
  }
  return;
}


SgFunctionDeclaration *MPICUDATranslator::BuildRunInteriorKernel(
    StencilMap *stencil) {
  LOG_DEBUG() << "Building RunInteriorKernel function\n";

  if (!flag_mpi_overlap_) {
    LOG_DEBUG() << "Overlapping is disabled; skipping interior kernel generation.\n";
    return NULL;
  }
  
  SgFunctionParameterList *params = sb::buildFunctionParameterList();
  SgClassDefinition *param_struct_def = stencil->GetStencilTypeDefinition();
  PSAssert(param_struct_def);

  SgInitializedName *dom_arg = NULL;

  const SgDeclarationStatementPtrList &members =
      param_struct_def->get_members();
  // add offset for process
  for (int i = 0; i < stencil->getNumDim()-1; ++i) {
    si::appendArg(
        params,
        sb::buildInitializedName(PS_RUN_KERNEL_PARAM_OFFSET_NAME + toString(i),
                                 sb::buildIntType()));
  }
  FOREACH(member, members.begin(), members.end()) {
    SgVariableDeclaration *member_decl = isSgVariableDeclaration(*member);
    const SgInitializedNamePtrList &vars = member_decl->get_variables();
    SgInitializedName *arg = sb::buildInitializedName(
        vars[0]->get_name(), vars[0]->get_type());
    SgType *type = arg->get_type();
    LOG_DEBUG() << "type: " << type->unparseToString() << "\n";
    if (Domain::isDomainType(type)) {
      if (!dom_arg) { dom_arg = arg; }
    } else if (GridType::isGridType(type)) {
      SgType *gt = builder()->BuildOnDeviceGridType(
          tx_->findGridType(type));
      arg->set_type(gt);
      // skip the grid index
      ++member;
    }
    si::appendArg(params, arg);
  }
  PSAssert(dom_arg);

  string func_name = stencil->GetRunName() + inner_prefix_;
  LOG_INFO() << "Declaring and defining function named "
             << func_name << "\n";
  SgFunctionDeclaration *run_func =
      sb::buildDefiningFunctionDeclaration(func_name,
                                           sb::buildVoidType(),
                                           params, global_scope_);
  
  si::attachComment(run_func, "Generated by " + string(__FUNCTION__));
  run_func->get_functionModifier().setCudaKernel();
  SgBasicBlock *func_body =
      BuildRunInteriorKernelBody(stencil, params);
  ru::ReplaceFuncBody(run_func, func_body);
  LOG_DEBUG() << "RunInteriorKernel successfully generated\n";
  return run_func;
}

void MPICUDATranslator::BuildFunctionParamList(
    SgClassDefinition *param_struct_def,
    SgFunctionParameterList *&params,
    SgInitializedName *&grid_arg,
    SgInitializedName *&dom_arg) {
  LOG_DEBUG() << "Building function parameter list\n";
  const SgDeclarationStatementPtrList &members =
      param_struct_def->get_members();

  FOREACH(member, members.begin(), members.end()) {
    SgVariableDeclaration *member_decl = isSgVariableDeclaration(*member);
    const SgInitializedNamePtrList &vars = member_decl->get_variables();
    SgInitializedName *arg = sb::buildInitializedName(
        vars[0]->get_name(), vars[0]->get_type());
    SgType *type = arg->get_type();
    if (Domain::isDomainType(type)) {
      if (!dom_arg) {
        dom_arg = arg;
      }
    } else if (GridType::isGridType(type)) {
      SgType *gt = builder()->BuildOnDeviceGridType(
          tx_->findGridType(type));
      arg->set_type(gt);
      if (!grid_arg) {
        grid_arg = arg;
      }
      // skip the grid index
      ++member;
    }
    si::appendArg(params, arg);
  }
  return;
}

SgFunctionDeclarationPtrVector
MPICUDATranslator::BuildRunMultiStreamBoundaryKernel(
    StencilMap *stencil) {
  SgClassDefinition *param_struct_def = stencil->GetStencilTypeDefinition();
  PSAssert(param_struct_def);

  std::vector<SgFunctionDeclaration*> run_funcs;

  for (int i = 0; i < stencil->getNumDim(); ++i) {
    for (int j = 0; j < 2; ++j) {
      bool fw = j;
      string name = stencil->GetRunName() + GetBoundarySuffix(i, fw);
      LOG_INFO() << "Generating global function: " << name << "\n";
      SgFunctionParameterList *params = sb::buildFunctionParameterList();
      SgInitializedName *grid_arg = NULL;
      SgInitializedName *dom_arg = NULL;
      BuildFunctionParamList(param_struct_def, params,
                             grid_arg, dom_arg);
      PSAssert(dom_arg);
      SgFunctionDeclaration *run_func =
          sb::buildDefiningFunctionDeclaration(
              name, sb::buildVoidType(),
              params, global_scope_);
      si::attachComment(run_func, "Generated by " + string(__FUNCTION__));
      run_func->get_functionModifier().setCudaKernel();
      SgBasicBlock *func_body = BuildRunMultiStreamBoundaryKernelBody(
          stencil, grid_arg, dom_arg, params, i, j);
      ru::ReplaceFuncBody(run_func, func_body);
      run_funcs.push_back(run_func);
    }
  }
  return run_funcs;
}

SgFunctionDeclarationPtrVector
MPICUDATranslator::BuildRunBoundaryKernel(StencilMap *stencil) {
  std::vector<SgFunctionDeclaration*> run_funcs;
  if (!flag_mpi_overlap_) return run_funcs;;
  if (flag_multistream_boundary_)
    return BuildRunMultiStreamBoundaryKernel(stencil);

  SgFunctionParameterList *params = sb::buildFunctionParameterList();
  SgClassDefinition *param_struct_def = stencil->GetStencilTypeDefinition();
  PSAssert(param_struct_def);

  SgInitializedName *grid_arg = NULL;
  SgInitializedName *dom_arg = NULL;

  const SgDeclarationStatementPtrList &members =
      param_struct_def->get_members();

  // add offset for process
  for (int i = 0; i < stencil->getNumDim()-1; ++i) {
    si::appendArg(params,
                  sb::buildInitializedName("offset" + toString(i),
                                           sb::buildIntType()));
  }
  si::appendArg(params,
                sb::buildInitializedName(boundary_kernel_width_name_,
                                         sb::buildIntType()));
  
  FOREACH(member, members.begin(), members.end()) {
    SgVariableDeclaration *member_decl = isSgVariableDeclaration(*member);
    const SgInitializedNamePtrList &vars = member_decl->get_variables();
    SgInitializedName *arg = sb::buildInitializedName(
        vars[0]->get_name(), vars[0]->get_type());
    SgType *type = arg->get_type();
    LOG_DEBUG() << "type: " << type->unparseToString() << "\n";
    if (Domain::isDomainType(type)) {
      if (!dom_arg) {
        dom_arg = arg;
      }
    } else if (GridType::isGridType(type)) {
      SgType *gt = builder()->BuildOnDeviceGridType(
          tx_->findGridType(type));
      arg->set_type(gt);
      if (!grid_arg) { grid_arg = arg;  }
      // skip the grid index
      ++member;
    }
    si::appendArg(params, arg);
  }
  PSAssert(grid_arg);
  PSAssert(dom_arg);

  LOG_INFO() << "Declaring and defining function named "
             << stencil->GetRunName() << "\n";
  SgFunctionDeclaration *run_func =
      sb::buildDefiningFunctionDeclaration(stencil->GetRunName()
                                           + GetBoundarySuffix(),
                                           sb::buildVoidType(),
                                           params, global_scope_);
  
  si::attachComment(run_func, "Generated by " + string(__FUNCTION__));
  run_func->get_functionModifier().setCudaKernel();
  SgBasicBlock *func_body = BuildRunBoundaryKernelBody(
      stencil, params);
  ru::ReplaceFuncBody(run_func, func_body);
  run_funcs.push_back(run_func);
  return run_funcs;
}

SgBasicBlock* MPICUDATranslator::BuildRunMultiStreamBoundaryKernelBody(
    StencilMap *stencil,
    SgInitializedName *grid_arg,
    SgInitializedName *dom_arg,
    SgFunctionParameterList *params,
    int dim, bool fw) {
  LOG_DEBUG() << "Generating run boundary kernel body\n";
  SgBasicBlock *block = sb::buildBasicBlock();
  si::attachComment(block, "Generated by " + string(__FUNCTION__));
  SgExpression *min_field = sb::buildDotExp(
      sb::buildVarRefExp(dom_arg), sb::buildVarRefExp("local_min"));
  SgExpression *max_field = sb::buildDotExp(
      sb::buildVarRefExp(dom_arg), sb::buildVarRefExp("local_max"));
  
  SgExpressionPtrList index_args;
  PSAssert(stencil->getNumDim() == 3);

  SgVariableDeclaration *x_index = sb::buildVariableDeclaration
      ("x", sb::buildIntType(), NULL, block);
  SgVariableDeclaration *y_index = sb::buildVariableDeclaration
      ("y", sb::buildIntType(), NULL, block);
  SgVariableDeclaration *z_index = sb::buildVariableDeclaration
      ("z", sb::buildIntType(), NULL, block);

  si::appendStatement(x_index, block);
  si::appendStatement(y_index, block);
  si::appendStatement(z_index, block);

  index_args.push_back(sb::buildVarRefExp(x_index));
  index_args.push_back(sb::buildVarRefExp(y_index));
  index_args.push_back(sb::buildVarRefExp(z_index));  

  SgVariableDeclaration *loop_index = x_index;
  SgStatement *loop_init = sb::buildAssignStatement(
      sb::buildVarRefExp(loop_index),
      Add(cu::BuildCudaIdxExp(cu::kThreadIdxX),
          ArrayRef(min_field, Int(0))));
  SgStatement *loop_test = sb::buildExprStatement(
      sb::buildLessThanOp(Var(loop_index), ArrayRef(max_field, Int(0))));
  SgExpression *loop_incr =
      sb::buildPlusAssignOp(Var(loop_index), cu::BuildCudaIdxExp(cu::kBlockDimX));

  SgBasicBlock *loop_body = sb::buildBasicBlock();
  SgExprListExp *kernel_args=
      builder()->BuildKernelCallArgList(
          stencil, index_args, params);
  string kernel_name = stencil->getKernel()->get_name()
      + GetBoundarySuffix(dim, fw);
  SgFunctionDeclaration *kernel =
      sb::buildNondefiningFunctionDeclaration(
          kernel_name,
          stencil->getKernel()->get_type()->get_return_type(),
          isSgFunctionParameterList(
              si::copyStatement(stencil->getKernel()->get_parameterList())),
          global_scope_);
  SgFunctionCallExp *kernel_call =
      sb::buildFunctionCallExp(sb::buildFunctionRefExp(kernel), kernel_args);
  si::appendStatement(sb::buildExprStatement(kernel_call), loop_body);

  SgStatement *loop
      = sb::buildForStatement(loop_init, loop_test, loop_incr, loop_body);

  // Creates doubly nested loops with y and z dimensions
  for (int i = 1; i <= 2; i++) {
    SgExpression *threadIdx = NULL;
    SgExpression *blockDim = NULL;
    if (i == 1) {
      loop_index = y_index;
      threadIdx = cu::BuildCudaIdxExp(cu::kThreadIdxY);
      blockDim = cu::BuildCudaIdxExp(cu::kBlockDimY);
    } else if (i == 2) {
      loop_index = z_index;
      threadIdx = cu::BuildCudaIdxExp(cu::kThreadIdxZ);
      blockDim = cu::BuildCudaIdxExp(cu::kBlockDimZ);
    }
    loop_init = sb::buildAssignStatement(
        Var(loop_index), Add(threadIdx, ArrayRef(
            si::copyExpression(min_field), Int(i))));
    loop_test = sb::buildExprStatement(
        sb::buildLessThanOp(Var(loop_index),
                            ArrayRef(si::copyExpression(max_field), Int(i))));
    loop_incr = sb::buildPlusAssignOp(
        sb::buildVarRefExp(loop_index), blockDim);
    loop = sb::buildForStatement(loop_init, loop_test, loop_incr, loop);
  }
  si::appendStatement(loop, block);

  return block;
}


static std::string GetTypeName(SgType *ty) {
  if (isSgTypeFloat(ty)) {
    return string("Float");
  } else if (isSgTypeDouble(ty)) {
    return string("Double");
  } else if (isSgTypeInt(ty)) {
    return string("Int");
  } else if (isSgTypeLong(ty)) {
    return string("Long");
  } else {
    LOG_ERROR() << "Unsupported type\n";
    PSAbort(1);
    return ""; // just to suppress compiler warning
  }
}

static std::string GetTypeDimName(GridType *gt) {
  return GetTypeName(gt->point_type())
      + toString(gt->rank()) + "D";
}

} // namespace translator
} // namespace physis
