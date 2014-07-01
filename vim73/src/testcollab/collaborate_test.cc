// TODO(zpotter): Add legal headers

/*
 * A test runner for all functionality provided in collaborate.c
 */

#include "gtest/gtest.h"

extern "C" {
#include "vim.h"
#include "collab_util.h"
}

/*
 * A test fixture class that sets up the default window and buffer for SetUp,
 * and clears the collaborative edit queue on TearDown.
 */
class CollaborativeEditQueue : public testing::Test {
 protected:

  /*
   * Sets up the default buffer for collaboration
   */
  virtual void SetUp() {
    win_alloc_first();
    check_win_options(curwin);
    collab_setbuf(curbuf);
  }

  /*
   * Clears the collaborative queue of edits
   */ 
  virtual void TearDown() {
    collabedit_T *pop;
    while ((pop = collab_dequeue()) != NULL) {
      free(pop);
    }
  }
};

/*
 * Tests that the designated collaborative buffer can be set
 */
TEST_F(CollaborativeEditQueue, sets_collab_buffer) {
  // A file buffer, contents not important
  buf_T buf; 

  // Set new buffer and get old one
  // Old buffer was originally set in SetUp() and should be the global 'curbuf'
  ASSERT_EQ(curbuf, collab_setbuf(&buf));

  // Set the buffer back to NULL
  // Should return the address of the buffer we previously set
  ASSERT_EQ(&buf, collab_setbuf(NULL));

}

/*
 * Tests that when provided with a big enough input buffer, the entire special
 * collaborative event key sequence is copied.
 */
TEST_F(CollaborativeEditQueue, buffers_full_pending_keys) {
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

/*
 * Tests that when provided with a buffer that is not big enough to hold the
 * entire special collaborative event key sequence, the entire sequence is
 * copied in the correct order over multiple calls.
 */
TEST_F(CollaborativeEditQueue, buffers_partial_pending_keys) {
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

/*
 * Tests that a single collabedit_T text insert is applied
 */
TEST_F(CollaborativeEditQueue, applies_text_insert) {
  // Enqueue an insert edit and process it
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_TEXT_INSERT;
  hello_edit->edit.text_insert.index = 0;
  hello_edit->edit.text_insert.text = (char_u*) "Hello world!\n";

  collab_enqueue(hello_edit);
  
  collab_applyedits();
  
  ASSERT_STREQ("Hello world!\n", (char*)ml_get(1)); 

  // Check that queue is now empty
  ASSERT_EQ(NULL, collab_dequeue());
}

/*
 * Tests that a single collabedit_T text delete is applied
 */
TEST_F(CollaborativeEditQueue, applies_text_delete) {
  // Start with some text in the buffer
  ml_append(0, (char_u*)"Hello\n", 0, FALSE);
  ml_append(1, (char_u*)"world!\n", 0, FALSE);
  appended_lines_mark(1, 2);

  // Enqueue a delete edit and process it
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_TEXT_DELETE;
  hello_edit->edit.text_delete.index = 1;
  hello_edit->edit.text_delete.text = (char_u*) "Hello\n";

  collab_enqueue(hello_edit);
  
  collab_applyedits();
  
  ASSERT_STREQ("world!\n", (char*)ml_get(1)); 

  // Check that queue is now empty
  ASSERT_EQ(NULL, collab_dequeue());
}

/*
 * Tests that multiple collabedit_T's of different types can be applied.
 */
TEST_F(CollaborativeEditQueue, applies_many_edits) {
  // Enqueue a few edits 
  collabedit_T *edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_INSERT;
  edit->edit.text_insert.index = 0;
  edit->edit.text_insert.text = (char_u*) "Hello\n";
  collab_enqueue(edit);
 
  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_INSERT;
  edit->edit.text_insert.index = 1;
  edit->edit.text_insert.text = (char_u*) "world!\n";
  collab_enqueue(edit);

  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_DELETE;
  edit->edit.text_delete.index = 1;
  edit->edit.text_delete.text = (char_u*) "Hello\n";
  collab_enqueue(edit);

  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_INSERT;
  edit->edit.text_insert.index = 0;
  edit->edit.text_insert.text = (char_u*) "Test my\n";
  collab_enqueue(edit);

  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_INSERT;
  edit->edit.text_insert.index = 1;
  edit->edit.text_insert.text = (char_u*) "programmatic\n";
  collab_enqueue(edit);

  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_DELETE;
  edit->edit.text_delete.index = 1;
  edit->edit.text_delete.text = (char_u*) "Test my\n";
  collab_enqueue(edit);

  // Process edit queue
  collab_applyedits();

  // Confirm expected output
  ASSERT_STREQ("programmatic\n", (char*)ml_get(1));
  ASSERT_STREQ("world!\n", (char*)ml_get(2)); 

  // Check that queue is now empty
  ASSERT_EQ(NULL, collab_dequeue());
}

