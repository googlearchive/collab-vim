// TODO(zpotter): Add legal headers

#include "gtest/gtest.h"

extern "C" {
#include "vim.h"
}

TEST(CollaborateEditQueue, sets_collab_buffer) {
  // Create a new empty buffer
  buf_T *buf = (buf_T*)malloc(sizeof(buf_T)); 

  // Set new buffer and get old one
  // Should return NULL, as the buffer hasn't been set yet
  ASSERT_EQ(NULL, collab_setbuf(buf));

  // Set the buffer back to NULL
  // Should return the buffer we previously set
  ASSERT_EQ(buf, collab_setbuf(NULL));

  free(buf);
}

TEST(CollaborateEditQueue, applies_text_insert) {
  GTEST_FAIL();
}

TEST(CollaborateEditQueue, applies_text_delete) {
  GTEST_FAIL();
}

TEST(CollaborateEditQueue, applies_many_edits) {
  GTEST_FAIL();
}

TEST(CollaborateEditQueue, inserts_pending_edit_keys) {
  GTEST_FAIL();
}
