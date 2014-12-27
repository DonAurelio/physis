// Licensed under the BSD license. See LICENSE.txt for more details.

#ifndef PHYSIS_TRANSLATOR_TRANSLATOR_COMMON_H_
#define PHYSIS_TRANSLATOR_TRANSLATOR_COMMON_H_

#define PHYSIS_TRANSLATOR

#include <set>
#include <list>
#include <vector>

#include "rose.h"

#include "common/config.h"
// Always enable debug output in translators
#define PS_DEBUG
#include "translator/config.h"
#include "physis/physis_common.h"
#include "physis/internal_common.h"
#include "physis/physis_util.h"
#include "translator/physis_exception.h"
#include "translator/physis_names.h"

namespace physis {
namespace translator {

typedef std::list<uint64_t> Uint64List;
typedef std::list<int64_t> Int64List;
typedef std::list<unsigned> UintList;
typedef std::list<int> IntList;
typedef std::vector<int> IntVec;
typedef Rose_STL_Container<SgFunctionCallExp*> SgFunctionCallExpPtrList;
typedef std::set<SgInitializedName*> SgInitializedNamePtrSet;
typedef std::vector<SgFunctionDeclaration*> SgFunctionDeclarationPtrVector;
typedef std::vector<SgExpression*> SgExpressionVector;

//using physis::util::IndexArray;
//using physis::util::IntVector;
//using physis::util::IntArray;
//using physis::util::SizeArray;

template <class T>
SgIntVal *Int(T x) {
  return SageBuilder::buildIntVal(x);
}

inline 
SgAddOp *Add(SgExpression *op1, SgExpression* op2) {
  return SageBuilder::buildAddOp(op1, op2);
}

inline 
SgAddOp *Add(SgExpression *op1, SgExpression* op2,
             SgExpression *op3) {
  return SageBuilder::buildAddOp(
      SageBuilder::buildAddOp(op1, op2), op3);
}

inline 
SgMultiplyOp *Mul(SgExpression *op1, SgExpression* op2) {
  return SageBuilder::buildMultiplyOp(op1, op2);
}

inline 
SgMultiplyOp *Mul(SgExpression *op1, SgExpression* op2,
                  SgExpression *op3) {
  return SageBuilder::buildMultiplyOp(
      SageBuilder::buildMultiplyOp(op1, op2), op3);
}

inline 
SgPntrArrRefExp *ArrayRef(SgExpression *op1, SgExpression* op2) {
  return SageBuilder::buildPntrArrRefExp(op1, op2);
}

template <class T>
SgVarRefExp *Var(T *v) {
  return SageBuilder::buildVarRefExp(v);
}

template <class T, class S>
SgVarRefExp *Var(T *v, S *w) {
  return SageBuilder::buildVarRefExp(v, w);
}

} // namespace translator
} // namespace physis

#endif /* PHYSIS_TRANSLATOR_TRANSLATOR_COMMON_H_ */
