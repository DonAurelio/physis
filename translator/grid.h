// Licensed under the BSD license. See LICENSE.txt for more details.

#ifndef PHYSIS_TRANSLATOR_GRID_H_
#define PHYSIS_TRANSLATOR_GRID_H_

#include <set>

using std::pair;

#include "translator/translator_common.h"
#include "physis/physis_util.h"
#include "translator/rose_util.h"
#include "translator/stencil_range.h"

namespace physis {
namespace translator {

static const char *gridIndexNames[3] = {"x", "y", "z"};

typedef pair<string, IntVector> AccessLoc;
typedef map<AccessLoc, StencilRange> MemberStencilRangeMap;

// Represents a grid type, not a particular grid object.
// Grid objects are handled by class Grid.
class GridType: public AstAttribute {
  SgClassType *real_type_;
  SgNamedType *user_type_;
  unsigned rank_;
  string type_name_;
  SgType *point_type_;
  SgClassDefinition *point_def_;
  //! Used, e.g., to hold its corresponding device type
  SgType *aux_type_;
  //! The decl of aux_type_
  SgDeclarationStatement *aux_decl_;
  SgFunctionDeclaration *aux_free_decl_;
  SgFunctionDeclaration *aux_new_decl_;
  SgFunctionDeclaration *aux_copyin_decl_;
  SgFunctionDeclaration *aux_copyout_decl_;
  SgFunctionDeclaration *aux_get_decl_;
  SgFunctionDeclaration *aux_emit_decl_;

 public:
  
  GridType(SgClassType *real_type, SgNamedType *user_type);
  GridType(const GridType &gt);
  GridType *copy();

  unsigned rank() const { return rank_; }
  const string& type_name() const { return type_name_; };
  SgNamedType *user_type() const { return user_type_; }  
  SgType *point_type() const { return point_type_; }
  SgClassDefinition *point_def() const { return point_def_; }  
  SgType *aux_type() const { return aux_type_; }      
  SgType *&aux_type() { return aux_type_; }
  SgDeclarationStatement *aux_decl() const { return aux_decl_; }
  SgDeclarationStatement *&aux_decl() { return aux_decl_; }
  SgClassDefinition *aux_def() const {
    if (isSgClassDeclaration(aux_decl())) {
      return isSgClassDeclaration(
          isSgClassDeclaration(aux_decl())->get_definingDeclaration())->get_definition();
    } else {
      return NULL;
    }
  }
  SgFunctionDeclaration *aux_new_decl() const { return aux_new_decl_; }
  SgFunctionDeclaration *&aux_new_decl() { return aux_new_decl_; }    
  SgFunctionDeclaration *aux_free_decl() const { return aux_free_decl_; }
  SgFunctionDeclaration *&aux_free_decl() { return aux_free_decl_; }    
  SgFunctionDeclaration *aux_copyin_decl() const { return aux_copyin_decl_; }
  SgFunctionDeclaration *&aux_copyin_decl() { return aux_copyin_decl_; }    
  SgFunctionDeclaration *aux_copyout_decl() const { return aux_copyout_decl_; }
  SgFunctionDeclaration *&aux_copyout_decl() { return aux_copyout_decl_; }    
  SgFunctionDeclaration *aux_get_decl() const { return aux_get_decl_; }
  SgFunctionDeclaration *&aux_get_decl() { return aux_get_decl_; }    
  SgFunctionDeclaration *aux_emit_decl() const { return aux_emit_decl_; }
  SgFunctionDeclaration *&aux_emit_decl() { return aux_emit_decl_; }    
  bool IsPrimitivePointType() const;
  bool IsUserDefinedPointType() const;
  
  string getRealFuncName(const string &funcName) const;
  
  string getRealFuncName(const string &funcName,
                         const string &kernelName) const;
  string toString() const {
    return type_name_;
  }

  string getNewName() const {
    return type_name_ + "New";
  }

  int GetMemberIndex(const string &member_name) const;

  static string getTypeNameFromFuncName(const string &funcName);
  static unsigned GetRankFromTypeName(const string &tname);
  static unsigned GetRankFromFortranType(const SgClassType *type);
  static bool isGridType(SgType *ty);
  static bool isGridType(const string &t);
  static bool isGridTypeSpecificCall(SgFunctionCallExp *ce);
  static string GetGridFuncName(SgFunctionCallExp *ce);  
  static SgInitializedName*
  getGridVarUsedInFuncCall(SgFunctionCallExp *call);
  SgExpression *BuildElementTypeExpr();
  static const string name;
  static const string get_name;
  static const string get_periodic_name;
  static const string emit_name;
  static const string set_name;  
 private:
  //! Identify the type of the grid points
  void FindPointType();
};

class Grid {
  GridType *gt;
  SgFunctionCallExp *newCall;
  StencilRange stencil_range_;
  MemberStencilRangeMap member_stencil_range_;  
  SizeVector static_size_;
  bool has_static_size_;
  void identifySize(SgExpressionPtrList::const_iterator size_begin,
                    SgExpressionPtrList::const_iterator size_end);
  bool _isReadWrite;
  SgExpression *attribute_;
  
 public:
  
  Grid(GridType *gt, SgFunctionCallExp *newCall):
      gt(gt), newCall(newCall), stencil_range_(gt->rank()),
      _isReadWrite(false), attribute_(NULL) {
    SgExpressionPtrList &args = newCall->get_args()->get_expressions();
    size_t num_dims = gt->rank();
    PSAssert(args.size() == num_dims ||
             args.size() == num_dims+1);
    if (num_dims+1 == args.size()) {
      // grid attribute is given
      attribute_ = args[num_dims];
      LOG_DEBUG() << "Attribute is specified: "
                  << attribute_->unparseToString() << "\n";
    }
    
    identifySize(args.begin(), args.begin() + num_dims);
    if (has_static_size())
      LOG_DEBUG() << "static grid generated: "
                  << toString() << "\n";
  }

  GridType *getType() const {
    return gt;
  }
  virtual ~Grid() {}
  const SgFunctionCallExp *new_call() const { return newCall; }
  string toString() const;
  int getNumDim() const {
    return gt->rank();
  }
  bool has_static_size() const {
    return has_static_size_;
  }
  const SizeVector &static_size() const {
    assert(has_static_size());
    return static_size_;
  }

  template <class I>
  string getStaticGlobalOffset(I offsets) const {
    // For some reason, index expression with z offset
    // appearing first results in faster CUDA code
    SizeArray::const_iterator sizes = static_size().begin();
    StringJoin sj("+");
    StringJoin sizeStr("*");
    list<string> t;
    for (unsigned i = 0; i < getNumDim(); ++i, ++offsets) {
      string goffset = "(" + string(gridIndexNames[i]) +
          "+(" + *offsets + "))";
      if (i == 0) {
        t.push_back(goffset);
      } else {
        t.push_back(goffset + "*" + sizeStr.get());
      }
      sizeStr << *sizes;
      ++sizes;
    }
    FOREACH(it, t.rbegin(), t.rend()) {
      sj << *it;
    }
    return sj.get();
  }
#ifdef UNUSED_CODE
  bool isReadWrite() const {
    return _isReadWrite;
  }
  void setReadWrite(bool b = true) {
    _isReadWrite = b;
  }
#endif
  SgExpression *BuildAttributeExpr();

  const StencilRange &stencil_range() const {
    return stencil_range_;
  }
  StencilRange &stencil_range() {
    return stencil_range_;
  }
  virtual void SetStencilRange(const StencilRange &sr);

  const MemberStencilRangeMap &member_stencil_range() const {
    return member_stencil_range_;
  }
  MemberStencilRangeMap &member_stencil_range() {
    return member_stencil_range_;
  }
  virtual void SetMemberStencilRange(const MemberStencilRangeMap &msr);


  static bool IsIntrinsicCall(SgFunctionCallExp *call);
};

typedef std::set<Grid*> GridSet;

class GridVarAttribute: public AstAttribute {
 public:
  static const std::string name;  
  GridVarAttribute(GridType *gt);
  GridVarAttribute(const GridVarAttribute &x);
  virtual ~GridVarAttribute() {}
  GridVarAttribute *copy() {
    return new GridVarAttribute(*this);
  }
  void AddStencilIndexList(const StencilIndexList &sil);
  void AddMemberStencilIndexList(const string &member,
                                 const IntVector &indices,
                                 const StencilIndexList &sil);
  GridType *gt() { return gt_; }
  StencilRange &sr() { return sr_; }
  MemberStencilRangeMap &member_sr() { return member_sr_; }
  void FixAggregateAndMemberStencilRange();
  
 protected:
  GridType *gt_;
  StencilRange sr_;
  MemberStencilRangeMap member_sr_;
};

class GridOffsetAnalysis {
 public:
  //! Returns the grid variable from an offset expression
  /*!
    \param offset Offset expression
    \return Variable reference for the grid object
  */
  static SgVarRefExp *GetGridVar(SgExpression *offset);
  //! Returns the index expression for a specified dimension
  /*!
    \param offset Offset expression
    \param dim Dimension (>=1)
    \return Index expression
   */
  static SgExpression *GetIndexAt(SgExpression *offset, int dim);
  //! Returns all index expressions
  /*!
    \param offset Offset expression
    \return Expression vector of all indices
  */
  static SgExpressionPtrList GetIndices(SgExpression *offset);
  //! Returns the offset component for array member accesses
  /*!
    \param offset Offset expression
    \return Array offset component
  */
  static SgExpression *GetArrayOffset(SgExpression *offset);
  //! Returns the offset indices for array member accesses
  /*!
    \param offset Offset expression
    \return Array offset indices
  */
  static SgExpressionPtrList GetArrayOffsetIndices(SgExpression *offset);
};

class GridOffsetAttribute: public AstAttribute {
 public:
  GridOffsetAttribute(int num_dim, bool periodic,
                      const StencilIndexList *sil): 
    rank_(num_dim), periodic_(periodic), sil_(NULL) {
    if (sil) {
      sil_ = new StencilIndexList();
      *sil_ = *sil;
    }
  }
  virtual ~GridOffsetAttribute() {}
  GridOffsetAttribute *copy() {
    GridOffsetAttribute *a= new GridOffsetAttribute(
        rank_, periodic_, sil_);
    return a;
  }
  static const std::string name;
  bool periodic() const { return periodic_; }
  int rank() const { return rank_; }
  /*  
  void SetStencilIndexList(const StencilIndexList &sil) {
    sil_ = sil;
    }*/
  const StencilIndexList *GetStencilIndexList() { return sil_; }
  
 protected:
  int rank_;
  bool periodic_;  
  StencilIndexList *sil_;
};

class GridGetAnalysis {
 public:
  //! Returns grid var if a call to GridGet is given
  static SgInitializedName *IsGetCall(SgExpression *exp, bool &is_periodic);
  //! Returns grid var if a call to GridGet is given
  static SgInitializedName *IsGetCall(SgExpression *exp);
  //! Returns grid var if an expression is an grid array access
  static SgInitializedName *IsGetArrayRead(SgExpression *exp, bool &is_periodic);
  static SgInitializedName *IsGetArrayRead(SgExpression *exp);
  static SgInitializedName *IsGet(SgExpression *exp, bool &is_periodic);
  static SgInitializedName *IsGet(SgExpression *exp);    
  //! Returns the offset expression in a get expression
  static SgExpression *GetOffset(SgExpression *get_exp);
  //! Returns the grid variable in a get expression
  static SgInitializedName *GetGridVar(SgExpression *get_exp);
  static SgExpression *GetGridExp(SgExpression *get_exp);
  
};

class GridGetAttribute: public AstAttribute {
 public:
  GridGetAttribute(GridType *gt,
                   SgInitializedName *gv,
                   GridVarAttribute *gva,
                   bool in_kernel,
                   bool is_periodic,
                   const StencilIndexList *sil,
                   const string &member_name="",
                   const IntVector &indices=IntVector());
  GridGetAttribute(const GridGetAttribute &x);
  virtual ~GridGetAttribute();
  GridGetAttribute *copy();
  static const std::string name;
  bool in_kernel() const { return in_kernel_; }
  void SetInKernel(bool t) { in_kernel_ = t; };
  bool is_periodic() const { return is_periodic_; }
  bool &is_periodic() { return is_periodic_; }
  void SetStencilIndexList(const StencilIndexList *sil);
  const StencilIndexList *GetStencilIndexList() { return sil_; }
  int rank() const { return gt_->rank(); }
  const string &member_name() const { return member_name_; }
  GridType *gt() { return gt_; }
  SgInitializedName *gv() const { return gv_; }
  SgInitializedName *&gv() { return gv_; }
  GridVarAttribute *gva() { return gva_; }  
  string &member_name() { return member_name_; }
  bool IsUserDefinedType() const;
  bool IsMemberAccess() const;
  const IntVector& indices() const {
    return indices_;
  }
  IntVector& indices() {
    return indices_;
  }
  
 protected:
  GridType *gt_;
  SgInitializedName *gv_; // necessary?
  GridVarAttribute *gva_;
  bool in_kernel_;
  bool is_periodic_;
  StencilIndexList *sil_;
  string member_name_;
  IntVector indices_;
};

class GridEmitAttribute: public AstAttribute {
 public:
  static const std::string name;
  GridEmitAttribute(GridType *gt,
                    SgInitializedName *gv);
  GridEmitAttribute(GridType *gt,
                    SgInitializedName *gv,
                    const string &member_name);
  GridEmitAttribute(GridType *gt,
                    SgInitializedName *gv,
                    const string &member_name,
                    const vector<string> &array_offsets);
  GridEmitAttribute(const GridEmitAttribute &x);
  virtual ~GridEmitAttribute();
  GridEmitAttribute *copy();
  GridType *gt() { return gt_; }
  SgInitializedName *gv() { return gv_; }  
  bool is_member_access() const { return is_member_access_; }
  const string &member_name() const { return member_name_; }
  const vector<string> &array_offsets() const { return array_offsets_; }
 protected:
  GridType *gt_;
  //! Grid variable originally referenced in the user code.
  /*!
    May not be valid after translation.
   */
  SgInitializedName *gv_;
  bool is_member_access_;
  string member_name_;
  vector<string> array_offsets_;
};

} // namespace translator
} // namespace physis

inline std::ostream &operator<<(std::ostream &os, 
                                const physis::translator::Grid &g) {
  return os << g.toString();
}


#endif /* PHYSIS_TRANSLATOR_GRID_H_ */
