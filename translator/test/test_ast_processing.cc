// Licensed under the BSD license. See LICENSE.txt for more details.

#include "rose.h"
#include <assert.h>
#include <vector>
#include <boost/foreach.hpp>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "translator/ast_processing.h"
#include "translator/rose_util.h"
#include "translator/test/common.h"

using namespace ::testing;
using namespace ::std;
using namespace ::physis::translator::test;

namespace si = SageInterface;
namespace sb = SageBuilder;

namespace physis {
namespace translator {
namespace rose_util {

static SgFunctionDeclaration *GetFunction(SgNode *node,
                                          const char *fname) {
  return si::findFunctionDeclaration(node, fname, NULL, true);
}

static bool HasVariable(const string &name,
                        SgNode *node) {
  BOOST_FOREACH(SgVarRefExp *ref,
                si::querySubTree<SgVarRefExp>(node)) {
    if (rose_util::GetName(ref) == name) return true;
  }
  BOOST_FOREACH(SgInitializedName *v,
                si::querySubTree<SgInitializedName>(node)) {
    if (v->get_name() == name) return true;
  }
  return false;
}

class ast_processing_RemoveRedundantVariableCopy: public Test {
 public:
  void SetUp() {
    proj_ = FrontEnd("test_ast_processing_input_remove_redundant_variable_copy.c");
    func_ =
        si::findFunctionDeclaration(
            proj_, UnitTest::GetInstance()->current_test_info()->name(),
            NULL, true);
    PSAssert(func_);
  }
  void TearDown() {
    delete proj_;    
  }
  SgProject *proj_;
  SgFunctionDeclaration *func_;
};
  
TEST_F(ast_processing_RemoveRedundantVariableCopy,
       DoesNotRemoveNonRedundantVariableCopy) {
  int num_vars = si::querySubTree<SgInitializedName>(proj_).size();
  int num_assign = si::querySubTree<SgAssignOp>(proj_).size();
  int removed = rose_util::RemoveRedundantVariableCopy(func_);
  ASSERT_THAT(removed, Eq(0));
  int num_vars_after = si::querySubTree<SgInitializedName>(proj_).size();
  int num_assign_after = si::querySubTree<SgAssignOp>(proj_).size();
  ASSERT_THAT(num_assign_after, Eq(num_assign));
  ASSERT_THAT(num_vars_after, Eq(num_vars));
}

TEST_F(ast_processing_RemoveRedundantVariableCopy,
       RemovesRedundantVariableCopy) {

  ASSERT_THAT(HasVariable("y", func_), Eq(true));
  
  int removed = rose_util::RemoveRedundantVariableCopy(func_);
  AstTests::runAllTests(proj_);
  
  ASSERT_THAT(removed, Eq(1));
  ASSERT_THAT(HasVariable("y", func_), Eq(false));
  ASSERT_THAT(HasVariable("z", func_), Eq(true));  
}

TEST_F(ast_processing_RemoveRedundantVariableCopy,
       DoesNotRemoveVariableCopyWhenSrcReassigned) {

  ASSERT_THAT(HasVariable("y", func_), Eq(true));
  
  int removed = rose_util::RemoveRedundantVariableCopy(func_);
  AstTests::runAllTests(proj_);
  
  ASSERT_THAT(removed, Eq(0));
  ASSERT_THAT(HasVariable("y", func_), Eq(true));
}

TEST_F(ast_processing_RemoveRedundantVariableCopy,
       DoesNotRemoveVariableCopyWhenDstReassigned) {

  ASSERT_THAT(HasVariable("y", func_), Eq(true));
  
  int removed = rose_util::RemoveRedundantVariableCopy(func_);
  AstTests::runAllTests(proj_);
  
  ASSERT_THAT(removed, Eq(0));
  ASSERT_PRED2(HasVariable, "y", func_);
}

TEST_F(ast_processing_RemoveRedundantVariableCopy,
       RemoveRedundantVariableCopyWithFuncCall) {
  int removed = rose_util::RemoveRedundantVariableCopy(func_);
  AstTests::runAllTests(proj_);
  
  ASSERT_THAT(removed, Eq(1));
  ASSERT_THAT(HasVariable("y", func_), Eq(false));
}

TEST_F(ast_processing_RemoveRedundantVariableCopy,
       RemoveWhenAssignedWithUnaryOp) {
  
  int removed = rose_util::RemoveRedundantVariableCopy(func_);
  AstTests::runAllTests(proj_);
  
  ASSERT_THAT(removed, Eq(1));
  ASSERT_THAT(HasVariable("y", func_), Eq(false));
}

class ast_processing_RemoveUnusedFunction: public Test {
 public:
  void SetUp() {
    proj_ = FrontEnd("test_ast_processing_input_remove_unused_func.c");
  }
  void TearDown() {
    delete proj_;    
  }
  SgProject *proj_;
};

TEST_F(ast_processing_RemoveUnusedFunction,
       RemovesUnusedFunction) {
  int removed = rose_util::RemoveUnusedFunction(proj_);
  AstTests::runAllTests(proj_);
  
  ASSERT_THAT(removed, Eq(1));
  ASSERT_THAT(GetFunction(proj_, "ToRemove"), IsNull());
  ASSERT_THAT(GetFunction(proj_, "NotToRemove1"), NotNull());
  ASSERT_THAT(GetFunction(proj_, "NotToRemove2"), NotNull());
}


} // namespace rose_util
} // namespace translator
} // namespace physis
  
int main(int argc, char *argv[]) {
  ::testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}

