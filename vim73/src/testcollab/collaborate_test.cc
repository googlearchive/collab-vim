// TODO(zpotter): Add legal headers

#include "gtest/gtest.h"

extern "C" {
#include "vim.h"
#include "collab_util.h"
}


class CollaborativeEditQueue : public testing::Test {
 protected:
  virtual void SetUp() {
    // Ensure the edit queue is empty before begining
    collabedit_T *pop;
    while ((pop = collab_dequeue()) != NULL) {
      free(pop);
    }
  }
  // virtual void TearDown() {}
};


TEST_F(CollaborativeEditQueue, sets_collab_buffer) {
  // A file buffer, contents not important
  buf_T buf; 

  // Set new buffer and get old one
  // Should return NULL, as the buffer hasn't been set yet
  ASSERT_EQ(NULL, collab_setbuf(&buf));

  // Set the buffer back to NULL
  // Should return the address of the buffer we previously set
  ASSERT_EQ(&buf, collab_setbuf(NULL));

}

TEST_F(CollaborativeEditQueue, applies_text_insert) {
  GTEST_FAIL();
}

TEST_F(CollaborativeEditQueue, applies_text_delete) {
  GTEST_FAIL();
}

TEST_F(CollaborativeEditQueue, applies_many_edits) {
  GTEST_FAIL();
}

TEST_F(CollaborativeEditQueue, applies_full_pending_keys) {
  // Insert a pending edit
  collabedit_T some_edit;
  collab_enqueue(&some_edit);  

  // Checking for user input should insert special key code into input buffer
  char_u inbuf[3];
  int num_available = ui_inchar(inbuf, 3, 0, NULL);

  ASSERT_EQ(3, num_available);
  ASSERT_EQ(CSI, inbuf[0]);
  ASSERT_EQ(KS_EXTRA, inbuf[1]);
  ASSERT_EQ(KE_COLLABEDIT, inbuf[2]);

}

TEST_F(CollaborativeEditQueue, applies_partial_pending_keys) {
  // Insert a pending edit
  collabedit_T some_edit;
  collab_enqueue(&some_edit);  

  // In total, there should be 3 char_u's for the sequence
  // Our buffer will be able to hold 4 to make sure the end isn't being written
  char_u inbuf[4] = {'\0', '\0', '\0', '\0'};

  // Get a single char at a time
  int num_available = ui_inchar(inbuf, 1, 0, NULL);
  ASSERT_EQ(1, num_available);
  ASSERT_EQ(CSI, inbuf[0]);

  num_available = ui_inchar(inbuf+1, 1, 0, NULL);
  ASSERT_EQ(1, num_available);
  ASSERT_EQ(KS_EXTRA, inbuf[1]);

  num_available = ui_inchar(inbuf+2, 2, 0, NULL);
  ASSERT_EQ(1, num_available);
  ASSERT_EQ(KE_COLLABEDIT, inbuf[2]);

  // Make sure ui_inchar didn't write off the end
  ASSERT_EQ('\0', inbuf[3]);
}

