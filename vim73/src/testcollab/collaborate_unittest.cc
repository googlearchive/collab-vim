// TODO(zpotter): Add legal headers

#include "gtest/gtest.h"

extern "C" {
#include "vim.h"
}

TEST(TestCase, SimpleTest) {
  EXPECT_EQ(4, 2*2);
}

TEST(TestCase, SimpleCollabTest) {
  collabedit_T *cedit = (collabedit_T*) malloc(sizeof(collabedit_T));

  collab_applyedits();
}
