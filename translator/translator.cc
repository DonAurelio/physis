// Copyright 2011, Tokyo Institute of Technology.
// All rights reserved.
//
// This file is distributed under the license described in
// LICENSE.txt.
//
// Author: Naoya Maruyama (naoya@matsulab.is.titech.ac.jp)

#include "translator/translator.h"

#include "translator/grid.h"
#include "translator/translation_context.h"
#include "translator/rose_ast_attribute.h"

namespace si = SageInterface;
namespace sb = SageBuilder;

namespace physis {
namespace translator {

void Translator::run(SgProject *project, TranslationContext *context) {
  LOG_DEBUG() << "Translation started\n";
  assert(project);
  project_ = project;
  src_ = isSgSourceFile((*project_)[0]);
  assert(src_);
  tx_ = context;

  global_scope_ = src_->get_globalScope();
  ROSE_ASSERT(global_scope_ != NULL);
  sb::pushScopeStack(global_scope_);

  ivec_type_ = sb::buildArrayType(sb::buildIntType(),
                                  sb::buildIntVal(PS_MAX_DIM));
  buildGridDecl();

  dom_type_ = isSgTypedefType(
      si::lookupNamedTypeInParentScopes(PSDOMAIN_TYPE_NAME, global_scope_));
  assert(dom_type_);
  LOG_DEBUG() << "dom base type: "
              << dom_type_->get_base_type()->class_name()
              << "\n";
  dom_ptr_type_ = sb::buildPointerType(dom_type_);
  
  assert(grid_swap_ = 
         si::lookupFunctionSymbolInParentScopes("__PSGridSwap",
                                                global_scope_));
  assert(grid_dim_get_func_ =
         si::lookupFunctionSymbolInParentScopes("PSGridDim",
                                                global_scope_));

  run();
  
  // TODO: optimization is disabled
  //optimize();
  
  finish();
  LOG_DEBUG() << "AST Translation finished\n";
}

void Translator::defineMacro(const string &name,
                             const string &val) {
  string macro("#define " + name + " " + val + "\n");
  LOG_DEBUG() << "Adding text: " + macro;
#if 0
  // This doesn't work when called multiple times.
  si::addTextForUnparser(src_->get_globalScope(),
                         macro,
                         AstUnparseAttribute::e_inside);
#else
  si::attachArbitraryText(src_->get_globalScope(), macro);
#endif
                          
}

void Translator::buildGridDecl() {
  LOG_DEBUG() << "grid type name: " << grid_type_name_ << "\n";
  grid_type_ = isSgTypedefType(
      si::lookupNamedTypeInParentScopes(grid_type_name_, global_scope_));
  assert(grid_type_);
  LOG_DEBUG() << "grid type found\n";  
  grid_ptr_type_ = sb::buildPointerType(grid_type_);
  SgClassType *anont = isSgClassType(grid_type_->get_base_type());
  if (anont) {
    grid_decl_ = isSgClassDeclaration(anont->get_declaration());
  } else {
    grid_decl_ = NULL;
  }
}

void Translator::visit(SgFunctionDeclaration *node) {
  if (tx_->isKernel(node)) {
    LOG_DEBUG() << "translate kernel declaration\n";
    translateKernelDeclaration(node);
  }
}

void Translator::visit(SgFunctionCallExp *node) {
  if (tx_->isNewCall(node)) {
    LOG_DEBUG() << "call to grid new found\n";
    const string name = rose_util::getFuncName(node);
    GridType *gt = tx_->findGridTypeByNew(name);
    assert(gt);
    translateNew(node, gt);
    return;
  }

  if (GridType::isGridTypeSpecificCall(node)) {
    SgInitializedName* gv = GridType::getGridVarUsedInFuncCall(node);
    assert(gv);
    string methodName = tx_->getGridFuncName(node);
    if (methodName == "get") {
      LOG_DEBUG() << "translating get\n";
      node->addNewAttribute(GridCallAttribute::name,
                            new GridCallAttribute(gv,
                                                  GridCallAttribute::GET));
      SgFunctionDeclaration *caller = getContainingFunction(node);
      if (tx_->isKernel(caller)) {
        LOG_DEBUG() << "Translating grid get appearing in kernel\n";
      } else {
        LOG_DEBUG() << "Translating grid get appearing in host\n";
      }
      // Call getkernel first if it's used in kernel; if true
      // returned, done. otherwise, try gethost if it's used in host;
      // the final fallback is translateget.
      if (!((tx_->isKernel(caller) && translateGetKernel(node, gv)) ||
            (!tx_->isKernel(caller) && translateGetHost(node, gv)))) {
        translateGet(node, gv, tx_->isKernel(caller));
      }
    } else if (methodName == "emit") {
      LOG_DEBUG() << "translating emit\n";
      node->addNewAttribute(GridCallAttribute::name,
                            new GridCallAttribute(
                                gv, GridCallAttribute::EMIT));
      translateEmit(node, gv);
    } else if (methodName == "set") {
      LOG_DEBUG() << "translating set\n";
      translateSet(node, gv);
    } else {
      throw PhysisException("Unsupported grid call");
    }
    setSkipChildren();
    return;
  }

  if (tx_->isMap(node)) {
    LOG_DEBUG() << "Translating map\n";
    LOG_DEBUG() << node->unparseToString() << "\n";
    translateMap(node, tx_->findMap(node));
    setSkipChildren();
    return;
  }

  if (tx_->isRun(node)) {
    LOG_DEBUG() << "Translating run\n";
    LOG_DEBUG() << node->unparseToString() << "\n";
    translateRun(node, tx_->findRun(node));
    setSkipChildren();
    return;
  }

  if (tx_->IsInit(node)) {
    LOG_DEBUG() << "Translating Init\n";
    LOG_DEBUG() << node->unparseToString() << "\n";
    translateInit(node);
    setSkipChildren();    
    return;
  }

  // This is not related to physis grids; leave it as is
  return;
}

} // namespace translator
} // namespace physis