// Licensed under the BSD license. See LICENSE.txt for more details.

#include "translator/optimizer/optimization_passes.h"
#include "translator/optimizer/optimization_common.h"
#include "translator/rose_util.h"
#include "translator/builder_interface.h"
#include "translator/translation_util.h"

#include <algorithm>

namespace si = SageInterface;
namespace sb = SageBuilder;

namespace physis {
namespace translator {
namespace optimizer {
namespace pass {

static void QueryGetInKernel(
    SgNode *top,
    vector<SgExpression*> &gets) {
  vector<SgNode*> exps =
      rose_util::QuerySubTreeAttribute<GridGetAttribute>(top);
  FOREACH(it, exps.begin(), exps.end()) {
    SgExpression *exp = isSgExpression(*it);
    PSAssert(exp);
    GridGetAttribute *gga =
        rose_util::GetASTAttribute<GridGetAttribute>(exp);
    PSAssert(gga);

    // Optimization applied only to in-kernel get
    if (!gga->in_kernel()) continue;
    gets.push_back(exp);
  }
  return;
}

// This ad-hoc code traversal heavily depends on how the previous
// translation passes are implemented. Should be made more robust. 
static SgExpression *GetGridFromGet(SgExpression *get) {
  if (isSgBinaryOp(get)) {
    //return isSgBinaryOp(get)->get_lhs_operand();
    return isSgBinaryOp(rose_util::removeCasts(isSgBinaryOp(get)->get_lhs_operand()))
        ->get_lhs_operand();
  } else {
    LOG_ERROR() << "Unsupported grid get: "
                << get->unparseToString() << "\n";
    PSAbort(1);
  }

  return NULL;
}

static bool IsInTrueBlock(SgIfStmt *if_stmt,
                          SgNode *node) {
  if (si::isAncestor(if_stmt->get_true_body(), node)) {
    return true;
  }
  if (si::isAncestor(if_stmt->get_false_body(), node)) {
    return false;
  }
  // node must be a child of either blocks, so this should not be
  // reached.
  PSAssert(0);
  return false;
}

static SgVarRefExp *ExtractVarRef(SgExpression *exp) {
  exp = rose_util::removeCasts(exp);
  SgVarRefExp *vr = isSgVarRefExp(exp);
  if (vr) return vr;
  SgBinaryOp *bop = isSgBinaryOp(exp);
  if (bop) {
    SgVarRefExp *lhs = ExtractVarRef(bop->get_lhs_operand());
    if (lhs) return lhs;
    SgVarRefExp *rhs = ExtractVarRef(bop->get_rhs_operand());
    if (rhs) return rhs;
    LOG_ERROR() << "No variable reference found in binary op\n";
    PSAbort(1);
  }
  LOG_ERROR() << "No variable reference found in: "
              << exp->unparseToString()
              << ", class: " << exp->class_name()
              << "\n";
  PSAbort(1);  
  return NULL;
}

static SgExpression *BuildGetOffsetCenter(
    SgExpression *get_exp,
    BuilderInterface *builder,
    SgScopeStatement *scope) {
  GridGetAttribute *grid_get_attr =
      rose_util::GetASTAttribute<GridGetAttribute>(get_exp);
  int nd = grid_get_attr->rank();
  SgExpressionPtrList center_exp;
  for (int i = 1; i <= nd; ++i) {
    SgExpression *center_index =
        GridOffsetAnalysis::GetIndexAt(GridGetAnalysis::GetOffset(get_exp), i);
    center_index = si::copyExpression(ExtractVarRef(center_index));
    center_exp.push_back(center_index);
  }
  StencilIndexList sil;
  StencilIndexListInitSelf(sil, nd);
  return builder->BuildGridOffset(si::copyExpression(GetGridFromGet(get_exp)),
                                  nd, &center_exp, &sil, true, false);
}

static SgExpression *BuildGetOffset(
    SgExpression *base_get_exp,
    BuilderInterface *builder,
    SgScopeStatement *scope,
    SgExpression *paired_get_exp) {
  GridGetAttribute *grid_get_attr =
      rose_util::GetASTAttribute<GridGetAttribute>(paired_get_exp);
  int nd = grid_get_attr->rank();
  const StencilIndexList *sil = grid_get_attr->GetStencilIndexList();
  PSAssert(StencilIndexRegularOrder(*sil));
  StencilRegularIndexList sril(*sil);
  SgExpressionPtrList offset_exprs;
  for (int i = 1; i <= nd; ++i) {
    SgExpression *index =
        GridOffsetAnalysis::GetIndexAt(
            GridGetAnalysis::GetOffset(base_get_exp), i);
    vector<SgNode*> sia =
        rose_util::QuerySubTreeAttribute<StencilIndexVarAttribute>(index);
    PSAssert(sia.size() == 1);
    SgExpression *offset_exp =
        si::copyExpression(isSgVarRefExp(sia.front()));
    ssize_t offset = sril.GetIndex(i);
    if (offset != 0) {
      offset_exp = sb::buildAddOp(offset_exp,
                                  sb::buildIntVal(offset));
    }
    offset_exprs.push_back(offset_exp);
  }
  return builder->BuildGridOffset(
      si::copyExpression(GetGridFromGet(base_get_exp)),
      nd, &offset_exprs, sil, true, false);
}


static void ProcessIfStmtStage1(SgExpression *get_exp,
                                SgIfStmt *if_stmt,
                                BuilderInterface *builder,
                                SgExpression *&paired_get_exp,
                                SgVariableDeclaration *&cond_var) {
  GridVarAttribute *gva = rose_util::GetASTAttribute<
    GridGetAttribute>(get_exp)->gva();
  
  // Find a get_exp that can be paired with this.
  vector<SgExpression *> gets;
  QueryGetInKernel(if_stmt, gets);
  paired_get_exp = NULL;
  FOREACH (it, gets.begin(), gets.end()) {
    LOG_DEBUG() << "Conditional get: "
                << (*it)->unparseToString() << "\n";
    if (get_exp == *it) continue;
    SgNode *parent = rose_util::FindCommonParent(get_exp, *it);
    LOG_DEBUG() << "Common parent: "
                << parent->unparseToString() << "\n";
    if (parent != if_stmt) {
      // located in the same conditional case
      continue;
    }
    // They must point to the same grid
    GridVarAttribute *gva_peer =
        rose_util::GetASTAttribute<GridGetAttribute>(*it)->gva();
    if (gva != gva_peer) continue;
    paired_get_exp = *it;
  }
  
  if (paired_get_exp) {
    LOG_DEBUG() << "Peer of get found: "
                << paired_get_exp->unparseToString()
                << "\n";
  } else {
    LOG_DEBUG() << "No peer found\n";
  }

  // Make a bool var of the if conditional so that it can be reused
  // for the new if block with the same conditional.
  SgStatement *cond = if_stmt->get_conditional();
  // Note: what class can the conditional be? Examining several ASTs
  // indicates it is always SgExprStatement, so we assume it is always
  // the case.
  if (!isSgExprStatement(cond)) {
    LOG_ERROR() << "Unexpected condition type: " << cond->class_name()
                << ": " << cond->unparseToString() << "\n";
    PSAbort(1);
  }
  SgScopeStatement *outer_scope
      = si::getScope(if_stmt->get_parent());
  cond_var
      = sb::buildVariableDeclaration(
          rose_util::generateUniqueName(outer_scope),
          sb::buildIntType(),
          sb::buildAssignInitializer(
              isSgExprStatement(cond)->get_expression()),
          outer_scope);
  si::insertStatementBefore(if_stmt, cond_var);
  // Replace the condition with the bool variable
  si::replaceStatement(
      if_stmt->get_conditional(),
      sb::buildExprStatement(sb::buildVarRefExp(cond_var)));
}

static void ProcessIfStmtStage2(SgExpression *get_exp,
                                SgIfStmt *if_stmt,
                                BuilderInterface *builder,
                                SgExpression *&paired_get_exp,
                                SgVariableDeclaration *&cond_var) {
  // precondition: get_exp is not replaced with a local var
  PSAssert(!isSgVarRefExp(get_exp));
  
  SgScopeStatement *outer_scope
      = si::getScope(if_stmt->get_parent());
  GridGetAttribute *grid_get_attr =
      rose_util::GetASTAttribute<GridGetAttribute>(get_exp);
  const StencilIndexList &sil =
      *grid_get_attr->GetStencilIndexList();
  
  // Declare the index variable
  SgVariableDeclaration *index_var = NULL;
  SgExpression *offset_exp =
      si::copyExpression(GridGetAnalysis::GetOffset(get_exp));
  // If the access is to the center point, no guard by condition is
  // necessary and just assign the offset
  if (paired_get_exp == NULL &&
      StencilIndexSelf(sil, sil.size())) {
    index_var = sb::buildVariableDeclaration(
        rose_util::generateUniqueName(outer_scope),
        physis::translator::BuildIndexType2(outer_scope),
        sb::buildAssignInitializer(offset_exp),
        outer_scope);
    si::insertStatementBefore(if_stmt, index_var);    
  } else {
    index_var = sb::buildVariableDeclaration(
        rose_util::generateUniqueName(outer_scope),
        physis::translator::BuildIndexType2(outer_scope),
        sb::buildAssignInitializer(sb::buildIntVal(0)),
        outer_scope);
    si::insertStatementBefore(if_stmt, index_var);
    // Make a conditional block to assign the correct index to index_var 
    SgBasicBlock *get_exp_index_assign =
        sb::buildBasicBlock(
            sb::buildAssignStatement(
                sb::buildVarRefExp(index_var),
                offset_exp));
    SgBasicBlock *paired_get_exp_index_assign = NULL;
    if (paired_get_exp) {
      SgExpression *paired_offset_exp =
          GridGetAnalysis::GetOffset(paired_get_exp);
      if (paired_offset_exp) {
        paired_offset_exp = 
            si::copyExpression(paired_offset_exp);
      } else {
        paired_offset_exp =
            BuildGetOffset(get_exp, builder, outer_scope,
                           paired_get_exp);
      }
      paired_get_exp_index_assign =
          sb::buildBasicBlock(
              sb::buildAssignStatement(
                  sb::buildVarRefExp(index_var),
                  paired_offset_exp));
    } else {
      // GetOffset of the center point, i.e., GetOffset(g, x, y, z)
      paired_get_exp_index_assign =
          sb::buildBasicBlock(
              sb::buildAssignStatement(
                  sb::buildVarRefExp(index_var),
                  BuildGetOffsetCenter(get_exp, builder, outer_scope)));
    }

    SgBasicBlock *true_block = get_exp_index_assign;
    SgBasicBlock *false_block = paired_get_exp_index_assign;
    if (!IsInTrueBlock(if_stmt, get_exp)) {
      std::swap(true_block, false_block);
    }
    SgIfStmt *index_assign_block = sb::buildIfStmt(
        sb::buildVarRefExp(cond_var),
        true_block, false_block);
    si::insertStatementBefore(if_stmt, index_assign_block);
  }

  si::replaceExpression(GridGetAnalysis::GetOffset(get_exp),
                        sb::buildVarRefExp(index_var));
  rose_util::GetASTAttribute<GridGetAttribute>(
      get_exp)->SetStencilIndexList(NULL);
      
  // Declare a new variable of the same type as grid.
  SgVariableDeclaration *v
      = sb::buildVariableDeclaration(
          rose_util::generateUniqueName(outer_scope),
          get_exp->get_type(),
          sb::buildAssignInitializer(si::copyExpression(get_exp)),
          outer_scope);
  // Replace the get expression with v
  si::replaceExpression(get_exp, sb::buildVarRefExp(v),
                        true);
  if (paired_get_exp)
    // Remove the get in this case since it is no longer used. 
    si::replaceExpression(paired_get_exp,
                          sb::buildVarRefExp(v), true);
  si::insertStatementBefore(if_stmt, v);
}

static void ProcessIfStmt(SgExpression *get_exp,
                          SgIfStmt *if_stmt,
                          BuilderInterface *builder) {
  /*
    Step 1: Simplify the if block by moving the get expression out of 
    the conditional block to a separate new conditional block.
    E.g.,
    if (t) {
      x = get(PSGetOffset(...)) + a
    }
    This block should be transformed to:
    float f = 0.0;
    if (t) {
      f = get(PSGetOffset(...));
    }
    if (t) {
      x = f + a
    }

    If multiple gets are used in both true and false parts, move all
    of them to the prepended block.
    E.g.,
    if (t) {
      x = get(PSGetOffset(a1));
    } else {
      x = get(PSGetOffset(a3));
    }
    This block should be transformed to:
    float f1 = 0.0;
    if (t) {
      f1 = get(PSGetOffset(a1));
    } else {
      f3 = get(PSGetOffset(a3));
    }
    if (t) {
      x = f1;
    } else {
      x = f3;
    }

    Step 2: Move forward the get out of the block.
    PSIndexType idx = PSGetOffset(x, y, z);
    if (t) {
      idx = PSGetOffset(...);
    }  
    float f = get(idx);
    // No change hereafter
  */

  SgExpression *paired_get_exp;
  SgVariableDeclaration *cond_var;
  
  // Step 1
  ProcessIfStmtStage1(get_exp, if_stmt, builder, paired_get_exp,
                      cond_var);
  
  // Step 2
  ProcessIfStmtStage2(get_exp, if_stmt, builder, paired_get_exp,
                      cond_var);
}

static void ProcessConditionalExp(SgExpression *get_exp,
                                  const SgConditionalExp *cond_exp) {
  LOG_WARNING() << "Select not supported.\n";
  return;
}

void unconditional_get(
    SgProject *proj,
    physis::translator::TranslationContext *tx,
    physis::translator::BuilderInterface *builder) {
  pre_process(proj, tx, __FUNCTION__);

  // Traverse grid gets in stencil kernels
  vector<SgExpression*> gets;
  QueryGetInKernel(proj, gets);
  FOREACH(it, gets.begin(), gets.end()) {
    SgExpression *exp = *it;
    LOG_DEBUG() << "Get: "
                << exp->unparseToString() << "\n";
    SgNode *cond = rose_util::IsConditional(exp);
    if (!cond) {
      LOG_DEBUG() << "Not conditional\n";
      continue;
    }
    if (isSgVarRefExp(exp)) {
      // This means this get is replaced with register access. This
      // can be paired with other real grid access, but cannot be a
      // base get expression for this optimization.
      continue;
    }
    LOG_DEBUG() << "Conditional get to optimize: "
                << exp->unparseToString() << "\n";
    if (isSgIfStmt(cond)) {
      ProcessIfStmt(exp, isSgIfStmt(cond), builder);
    } else if (isSgConditionalExp(cond)) {
      ProcessConditionalExp(exp, isSgConditionalExp(cond));
    }
  }
  post_process(proj, tx, __FUNCTION__);  
}

} // namespace pass
} // namespace optimizer
} // namespace translator
} // namespace physis

