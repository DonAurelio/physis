// Licensed under the BSD license. See LICENSE.txt for more details.

#include "translator/mpi_cuda_runtime_builder.h"

namespace sb = SageBuilder;
namespace si = SageInterface;
namespace ru = physis::translator::rose_util;

namespace physis {
namespace translator {

SgExpression *MPICUDARuntimeBuilder::BuildGridBaseAddr(
    SgExpression *gvref, SgType *point_type) {
  return ReferenceRuntimeBuilder::BuildGridBaseAddr(gvref, point_type);
}


SgFunctionCallExp *BuildGridGetDev(SgExpression *grid_var) {
  SgFunctionSymbol *fs
      = si::lookupFunctionSymbolInParentScopes("__PSGridGetDev");
  SgFunctionCallExp *fc =
      sb::buildFunctionCallExp(fs, sb::buildExprListExp(grid_var));
  return fc;
}

SgFunctionCallExp *BuildGetLocalSize(SgExpression *dim) {
  SgFunctionSymbol *fs
      = si::lookupFunctionSymbolInParentScopes("__PSGetLocalSize");
  SgFunctionCallExp *fc =
      sb::buildFunctionCallExp(fs, sb::buildExprListExp(dim));
  return fc;
}  
SgFunctionCallExp *BuildGetLocalOffset(SgExpression *dim) {
  SgFunctionSymbol *fs
      = si::lookupFunctionSymbolInParentScopes("__PSGetLocalOffset");
  SgFunctionCallExp *fc =
      sb::buildFunctionCallExp(fs, sb::buildExprListExp(dim));
  return fc;
}

SgFunctionCallExp *BuildDomainShrink(SgExpression *dom,
                                     SgExpression *width) {
  SgFunctionSymbol *fs
      = si::lookupFunctionSymbolInParentScopes("__PSDomainShrink");
  SgFunctionCallExp *fc =
      sb::buildFunctionCallExp(
          fs, sb::buildExprListExp(dom, width));
  return fc;
}

SgExpression *BuildStreamBoundaryKernel(int idx) {
  SgVarRefExp *inner_stream = sb::buildVarRefExp("stream_boundary_kernel");
  return sb::buildPntrArrRefExp(inner_stream, sb::buildIntVal(idx));
}

SgExprListExp *MPICUDARuntimeBuilder::BuildKernelCallArgList(
    StencilMap *stencil,
    SgExpressionPtrList &index_args,
    SgFunctionParameterList *run_kernel_params) {
  return cuda_rt_builder_->BuildKernelCallArgList(
      stencil, index_args, run_kernel_params);
}

SgIfStmt *MPICUDARuntimeBuilder::BuildDomainInclusionCheck(
    const vector<SgVariableDeclaration*> &indices,
    SgInitializedName *dom_arg, SgStatement *true_stmt) {
  return cuda_rt_builder_->BuildDomainInclusionCheck(
      indices, dom_arg, true_stmt);
}

// This is almost equivalent as CUDATranslator::BuildRunKernel, except
// for having offset.
SgFunctionDeclaration *MPICUDARuntimeBuilder::BuildRunKernelFunc(
    StencilMap *stencil) {
  SgFunctionParameterList *params = sb::buildFunctionParameterList();
  SgClassDefinition *param_struct_def = stencil->GetStencilTypeDefinition();
  PSAssert(param_struct_def);
  SgInitializedName *dom_arg = NULL;
  const SgDeclarationStatementPtrList &members =
      param_struct_def->get_members();
  // add offset for process
  for (int i = 0; i < stencil->getNumDim()-1; ++i) {
    si::appendArg(params,
                  sb::buildInitializedName("offset" + toString(i),
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
      SgType *gt = BuildOnDeviceGridType(
          ru::GetASTAttribute<GridType>(type));
      arg->set_type(gt);
      // skip the grid index
      ++member;
    }
    si::appendArg(params, arg);
  }
  PSAssert(dom_arg);

  LOG_INFO() << "Declaring and defining function named "
             << stencil->GetRunName() << "\n";
  SgFunctionDeclaration *run_func =
      sb::buildDefiningFunctionDeclaration(stencil->GetRunName(),
                                           sb::buildVoidType(),
                                           params, gs_);
  
  si::attachComment(run_func, "Generated by " + string(__FUNCTION__));
  run_func->get_functionModifier().setCudaKernel();
  vector<SgVariableDeclaration*> indices;
  SgBasicBlock *func_body = BuildRunKernelFuncBody(stencil, params, indices);
  rose_util::ReplaceFuncBody(run_func, func_body);  
  rose_util::AddASTAttribute(run_func,
                             new RunKernelAttribute(stencil));
  return run_func;
}

// This is the same as CUDATranslator::BuildRunKernelBody, except for
// this version needs to add offsets to the x and y indices.
SgBasicBlock* MPICUDARuntimeBuilder::BuildRunKernelFuncBody(
    StencilMap *stencil,
    SgFunctionParameterList *param,
    vector<SgVariableDeclaration*> &indices) {
  LOG_DEBUG() << __FUNCTION__;
  SgInitializedName *dom_arg = param->get_args()[0];  
  SgBasicBlock *block = sb::buildBasicBlock();
  si::attachComment(block, "Generated by " + string(__FUNCTION__));
  int dim = stencil->getNumDim();  
  SgExpression *min_field = sb::buildDotExp(
      sb::buildVarRefExp(dom_arg), sb::buildVarRefExp("local_min"));
  SgExpression *max_field = sb::buildDotExp(
      sb::buildVarRefExp(dom_arg), sb::buildVarRefExp("local_max"));
  vector<SgExpression*> offset_exprs;
  for (int i = 0; i < dim-1; ++i) {
    offset_exprs.push_back(sb::buildVarRefExp("offset" + toString(i)));
  }
  
  SgExpressionPtrList index_args;
  if (dim < 3) {
    LOG_ERROR() << "not supported yet.\n";
  } else if (dim == 3) {
    // x = blockIdx.x * blockDim.x + threadIdx.x + offset.x;

    SgAssignInitializer *x_init =
        sb::buildAssignInitializer(
            Add(Add(Mul(ru::BuildCudaIdxExp(ru::kBlockIdxX),
                        ru::BuildCudaIdxExp(ru::kBlockDimX)),
                    ru::BuildCudaIdxExp(ru::kThreadIdxX)),
                offset_exprs[0]));

    // y = blockIdx.y * blockDim.y + threadIdx.y + offset.y;    
    SgAssignInitializer *y_init = 
        sb::buildAssignInitializer(
            Add(Add(Mul(ru::BuildCudaIdxExp(ru::kBlockIdxY),
                        ru::BuildCudaIdxExp(ru::kBlockDimY)),
                    ru::BuildCudaIdxExp(ru::kThreadIdxY)),
                offset_exprs[1]));
        
    SgVariableDeclaration *x_index = sb::buildVariableDeclaration
        ("x", sb::buildIntType(), x_init, block);
    SgVariableDeclaration *y_index = sb::buildVariableDeclaration
        ("y", sb::buildIntType(), y_init, block);
    SgVariableDeclaration *z_index = sb::buildVariableDeclaration
        ("z", sb::buildIntType(), NULL, block);
    rose_util::AddASTAttribute<RunKernelIndexVarAttribute>(
        x_index, new RunKernelIndexVarAttribute(1));
    rose_util::AddASTAttribute<RunKernelIndexVarAttribute>(
        y_index, new RunKernelIndexVarAttribute(2));
    rose_util::AddASTAttribute<RunKernelIndexVarAttribute>(
        z_index, new RunKernelIndexVarAttribute(3));
    si::appendStatement(x_index, block);
    si::appendStatement(y_index, block);
    index_args.push_back(sb::buildVarRefExp(x_index));
    index_args.push_back(sb::buildVarRefExp(y_index));

    SgVariableDeclaration *loop_index = z_index;
    SgExpression *loop_begin =
        ArrayRef(min_field, Int(2));
    SgStatement *loop_init = sb::buildAssignStatement(
        sb::buildVarRefExp(loop_index),
        loop_begin);
    SgExpression *loop_end =
        ArrayRef(max_field,
                 Int(2));
    SgStatement *loop_test = sb::buildExprStatement(
        sb::buildLessThanOp(sb::buildVarRefExp(loop_index),
                            loop_end));
    index_args.push_back(sb::buildVarRefExp(loop_index));

    SgVariableDeclaration* t[] = {x_index, y_index};
    vector<SgVariableDeclaration*> range_checking_idx(t, t + 2);
    si::appendStatement(
        BuildDomainInclusionCheck(
            range_checking_idx, dom_arg,
            sb::buildReturnStmt()),
        block);
    si::appendStatement(loop_index, block);

    SgExpression *loop_incr =
        sb::buildPlusPlusOp(sb::buildVarRefExp(loop_index));
    SgFunctionCallExp *kernel_call
        = BuildKernelCall(stencil, index_args, param);
    SgBasicBlock *loop_body =
        sb::buildBasicBlock(sb::buildExprStatement(kernel_call));
    SgStatement *loop
        = sb::buildForStatement(loop_init, loop_test,
                                loop_incr, loop_body);
    si::appendStatement(loop, block);
    rose_util::AddASTAttribute(loop, new RunKernelLoopAttribute(3));
  }

  return block;
}

SgType *MPICUDARuntimeBuilder::BuildOnDeviceGridType(GridType *gt) {
  return cuda_rt_builder_->BuildOnDeviceGridType(gt);
}

} // namespace translator
} // namespace physis
