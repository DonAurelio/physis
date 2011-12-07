// Copyright 2011, Tokyo Institute of Technology.
// All rights reserved.
//
// This file is distributed under the license described in
// LICENSE.txt.
//
// Author: Naoya Maruyama (naoya@matsulab.is.titech.ac.jp)

#include "translator/rose_util.h"

namespace sb = SageBuilder;
namespace si = SageInterface;

using namespace physis;
using namespace physis::util;

namespace physis {
namespace translator {
namespace rose_util {

SgType *getType(SgNode *topLevelNode, const string &typeName) {
  SgName typeNameNode(typeName);
  Rose_STL_Container<SgNode*> types =
      NodeQuery::querySubTree(topLevelNode, V_SgNamedType);
  FOREACH(it, types.begin(), types.end()) {
    SgNamedType *type = isSgNamedType(*it);
    assert(type);
    if (type->get_name().getString() == typeName) {
      return type;
    }
  }
  return NULL;
}

void printAllTypeNames(SgNode *topLevelNode,
                       std::ostream &os) {
  Rose_STL_Container<SgNode*> types =
      NodeQuery::querySubTree(topLevelNode,
                              // odeQuery::VariableTypes);
                              V_SgType);
  FOREACH(it, types.begin(), types.end()) {
    SgType *type = isSgType(*it);
    assert(type);
    if (isSgNamedType(type)) {
      os << isSgNamedType(type)->get_name().getString() << "\n";
    }
  }
}

SgExpression *removeCasts(SgExpression *exp) {
  if (isSgCastExp(exp)) {
    return removeCasts(isSgCastExp(exp)->get_operand());
  } else {
    return exp;
  }
}

SgFunctionDeclaration *getFuncDeclFromFuncRef(SgExpression *refExp) {
  refExp = removeCasts(refExp);
  if (isSgFunctionRefExp(refExp)) {
    SgFunctionRefExp *rexp = isSgFunctionRefExp(refExp);
    return rexp->get_symbol()->get_declaration();
  } else {
    assert(0 && "Unsupported case");
  }
}

SgClassDefinition *getDefinition(SgClassType *t) {
  SgClassDeclaration *d =
      isSgClassDeclaration(t->get_declaration()->get_definingDeclaration());
  return isSgClassDefinition(d->get_definition());
}

SgFunctionDeclaration *getContainingFunction(SgNode *node) {
  while (node) {
    SgFunctionDeclaration *f = isSgFunctionDeclaration(node);
    if (f) return f;
    node = node->get_parent();
  }
  return NULL;
}

string getFuncName(SgFunctionRefExp *fref) {
  SgFunctionSymbol *fs = fref->get_symbol();
  return fs->get_name().getString();
}

string getFuncName(SgFunctionCallExp *call) {
  SgFunctionRefExp *fref = isSgFunctionRefExp(call->get_function());
  if (!fref) {
    throw physis::translator::PhysisException("Non-direct function calls are not supported.");
  }
  return getFuncName(fref);
}

SgExpression *copyExpr(const SgExpression *expr) {
  SgTreeCopy tc;
  SgExpression *c = isSgExpression(expr->copy(tc));
  assert(c);
  return c;
}

SgInitializedName *copySgNode(const SgInitializedName *expr) {
  SgTreeCopy tc;
  SgInitializedName *c = isSgInitializedName(expr->copy(tc));
  assert(c);
  return c;
}

SgFunctionSymbol *getFunctionSymbol(SgFunctionDeclaration *f) {
  return isSgFunctionSymbol(f->search_for_symbol_from_symbol_table());
}

SgFunctionRefExp *getFunctionRefExp(SgFunctionDeclaration *decl) {
  SgFunctionSymbol *fs = getFunctionSymbol(decl);
  return SageBuilder::buildFunctionRefExp(fs);
}

SgVarRefExp *buildFieldRefExp(SgClassDeclaration *decl, string name) {
  decl = isSgClassDeclaration(decl->get_definingDeclaration());
  SgVarRefExp *f =
      SageBuilder::buildVarRefExp(name, decl->get_definition());
  return f;
}

bool isFuncParam(SgInitializedName *in) {
  return isSgFunctionParameterList(in->get_declaration());
}

SgInitializedName *getInitializedName(SgVarRefExp *var) {
  SgVariableSymbol *sym = var->get_symbol();
  assert(sym);
  SgInitializedName *refDecl = sym->get_declaration();
  assert(refDecl);
  return refDecl;
}

string generateUniqueName(SgScopeStatement *scope, const string &prefix) {
  if (scope == NULL) {
    scope = SageBuilder::topScopeStack();
  }
  ROSE_ASSERT(scope);
  SgSymbolTable *symbol_table = scope->get_symbol_table();
  string name;
  int post_fix = symbol_table->size();
  do {
    name = prefix + toString(post_fix);
    post_fix++;
  } while (symbol_table->exists(name));
  return name;
}

void SetFunctionStatic(SgFunctionDeclaration *fdecl) {
  fdecl->get_declarationModifier().get_storageModifier().setStatic();
}

SgExpression *buildNULL() {
#if 0  
  return sb::buildCastExp(sb::buildIntVal(0),
                          sb::buildPointerType(sb::buildVoidType()));
#else
  return sb::buildVarRefExp("NULL");
#endif
}

SgVariableDeclaration *buildVarDecl(const string &name,
                                    SgType *type,
                                    SgExpression *val,
                                    SgScopeStatement *scope) {
  SgAssignInitializer *init =
      sb::buildAssignInitializer(val, type);
  SgVariableDeclaration *sdecl
      = sb::buildVariableDeclaration(name, type, init, scope);
  si::appendStatement(sdecl, scope);
  return sdecl;
}

void AppendExprStatement(SgScopeStatement *scope,
                         SgExpression *exp) {
  si::appendStatement(sb::buildExprStatement(exp), scope);
  return;
}

SgValueExp *BuildIntLikeVal(int v) {
  return sb::buildIntVal(v);
}

SgValueExp *BuildIntLikeVal(long v) {
  return sb::buildLongIntVal(v);
}

SgValueExp *BuildIntLikeVal(long long v) {
  return sb::buildLongLongIntVal(v);
}

SgVariableDeclaration *DeclarePSVectorInt(const std::string &name,
                                          const IntVector &vec,
                                          SgScopeStatement *block) {
  
  SgType *vec_type = sb::buildArrayType(sb::buildIntType(),
                                        sb::buildIntVal(PS_MAX_DIM));
  SgExprListExp *init_expr = sb::buildExprListExp();
  FOREACH (it, vec.begin(), vec.end()) {
    IntVector::value_type v = *it;
    init_expr->append_expression(BuildIntLikeVal(v));
  }
  
  SgAggregateInitializer *init
      = sb::buildAggregateInitializer(init_expr, vec_type);
  SgVariableDeclaration *decl
      = sb::buildVariableDeclaration(name, vec_type, init, block);
  si::appendStatement(decl, block);
  
  return decl;
}

void RedirectFunctionCalls(SgNode *node,
                           const std::string &current_func,
                           SgFunctionRefExp *new_func) {
  Rose_STL_Container<SgNode*> calls =
      NodeQuery::querySubTree(node, V_SgFunctionCallExp);
  SgFunctionSymbol *curfs =
      si::lookupFunctionSymbolInParentScopes(current_func);
  FOREACH (it, calls.begin(), calls.end()) {
    SgFunctionCallExp *fc = isSgFunctionCallExp(*it);
    PSAssert(fc);
    if (fc->getAssociatedFunctionSymbol() != curfs)
      continue;
    LOG_DEBUG() << "Redirecting call to " << current_func
                << " to " << new_func << "\n";
    fc->set_function(new_func);
  }
  
}

SgFunctionDeclaration * CloneFunction(SgFunctionDeclaration *decl,
                                      const std::string &new_name,
                                      SgScopeStatement *scope) {
  SgFunctionDeclaration *new_decl
      = sb::buildDefiningFunctionDeclaration(
          new_name,
          decl->get_type()->get_return_type(),
          static_cast<SgFunctionParameterList*>(
              si::copyStatement(decl->get_parameterList())),
          scope);
  SgFunctionDefinition *new_def =
      static_cast<SgFunctionDefinition*>(
          si::copyStatement(decl->get_definition()));
  new_decl->set_definition(new_def);
  new_def->set_declaration(new_decl);
  new_decl->get_functionModifier() = decl->get_functionModifier();
  return new_decl;
}

bool IsIntLikeType(const SgType *t) {
  if (isSgModifierType(t)) {
    return IsIntLikeType(isSgModifierType(t)->get_base_type());
  }
  return t->isIntegerType();
}

SgNode *IsConditional(const SgNode *node) {
  while (true) {
    SgNode *parent = node->get_parent();
    // Stop if no parent found
    if (!parent) break;
    // True if the parent is a node for conditional execution
    if (isSgIfStmt(parent) ||
        isSgConditionalExp(parent)) {
      return parent;
    }
    // Stop when crossing function boundary
    if (isSgFunctionDeclaration(parent)) {
      break;
    }
    node = parent;
  }
  return NULL;
}


SgNode *FindCommonParent(SgNode *n1, SgNode *n2) {
  while (n1) {
    if (n1 == n2 || si::isAncestor(n1, n2)) return n1;
    n1 = n1->get_parent();
  }
  return NULL;
}

}  // namespace rose_util
}  // namespace translator
}  // namespace physis
