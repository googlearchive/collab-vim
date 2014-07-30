// TODO(zpotter): Add legal headers

// A test runner for all functionality provided in collaborate.c

#include "gtest/gtest.h"
#include "testcollab.h"

extern "C" {
#include "nacl_io/nacl_io.h"
#include "vim.h"
#include "collab_structs.h"
#include "collab_util.h"
}

// A test fixture class that sets up the default window and buffer for SetUp,
// and clears the collaborative edit queue on TearDown.
class CollaborativeEditQueue : public testing::Test {
 protected:
  // Sets up just once before the first test.
  static void SetUpTestCase() {
    nacl_io_init();
    collab_init();
  }
  
  // Sets up the default buffer for collaboration.
  virtual void SetUp() {
    win_alloc_first();
    check_win_options(curwin);
  }

  // Clears the collaborative queue of edits.
  virtual void TearDown() {
    collabedit_T *pop;
    while ((pop = collab_dequeue(&collab_queue)) != NULL) {
      free(pop);
    }
  }
};

// Tests that when provided with a big enough input buffer, the entire special
// collaborative event key sequence is copied.
TEST_F(CollaborativeEditQueue, buffers_full_pending_keys) {
  // Insert a pending edit.
  collabedit_T some_edit;
  collab_enqueue(&collab_queue, &some_edit);  

  // Checking for user input should insert special key code into input buffer
  char_u inbuf[3];
  int num_available = ui_inchar(inbuf, 3, 0, NULL);

  ASSERT_EQ(3, num_available);
  ASSERT_EQ(K_SPECIAL, inbuf[0]);
  ASSERT_EQ(KS_EXTRA, inbuf[1]);
  ASSERT_EQ(KE_COLLABEDIT, inbuf[2]);
}

// Tests that when provided with a buffer that is not big enough to hold the
// entire special collaborative event key sequence, the entire sequence is
// copied in the correct order over multiple calls.
TEST_F(CollaborativeEditQueue, buffers_partial_pending_keys) {
  // Insert a pending edit.
  collabedit_T some_edit;
  collab_enqueue(&collab_queue, &some_edit);  

  // In total, there should be 3 char_u's for the sequence. Our buffer will be
  // able to hold 4 to make sure the end isn't being written.
  char_u inbuf[4] = {'\0', '\0', '\0', '\0'};

  // Get a single char at a time
  int num_available = ui_inchar(inbuf, 1, 0, NULL);
  ASSERT_EQ(1, num_available);
  ASSERT_EQ(K_SPECIAL, inbuf[0]);

  num_available = ui_inchar(inbuf+1, 1, 0, NULL);
  ASSERT_EQ(1, num_available);
  ASSERT_EQ(KS_EXTRA, inbuf[1]);

  num_available = ui_inchar(inbuf+2, 2, 0, NULL);
  ASSERT_EQ(1, num_available);
  ASSERT_EQ(KE_COLLABEDIT, inbuf[2]);

  // Make sure ui_inchar didn't write off the end
  ASSERT_EQ('\0', inbuf[3]);
}

// Tests that a single collabedit_T append line is applied.
TEST_F(CollaborativeEditQueue, applies_append_line) {
  // Enqueue an append edit and process it.
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_APPEND_LINE;
  hello_edit->file_buf = curbuf; 
  hello_edit->append_line.line = 0;
  hello_edit->append_line.text = malloc_literal("Hello world!"); 

  collab_enqueue(&collab_queue, hello_edit);
  
  collab_applyedits(&collab_queue);
  
  ASSERT_STREQ("Hello world!", reinterpret_cast<char *>(ml_get(1))); 

  // Check that queue is now empty
  ASSERT_EQ(NULL, collab_dequeue(&collab_queue));
}

// Tests that a single collabedit_T remove line is applied.
TEST_F(CollaborativeEditQueue, applies_remove_line) {
  // Start with some text in the buffer.
  ml_append_collab(0, malloc_literal("Hello"), 0, FALSE, FALSE);
  ml_append_collab(1, malloc_literal("world!"), 0, FALSE, FALSE);
  appended_lines_mark(1, 2);

  // Enqueue a delete edit and process it
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_REMOVE_LINE;
  hello_edit->file_buf = curbuf;
  hello_edit->remove_line.line = 1;

  collab_enqueue(&collab_queue, hello_edit);

  collab_applyedits(&collab_queue);

  ASSERT_STREQ("world!", reinterpret_cast<char *>(ml_get(1)));

  // Check that queue is now empty
  ASSERT_EQ(NULL, collab_dequeue(&collab_queue));
}

// Tests that a single collabedit_T insert text is applied.
TEST_F(CollaborativeEditQueue, applies_insert_text) {
  // Start with some text in the buffer.
  ml_append_collab(0, malloc_literal("Hell!"), 0, FALSE, FALSE);
  appended_lines_mark(1, 1);

  // Enqueue an insert edit and process it.
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_INSERT_TEXT;
  hello_edit->file_buf = curbuf; 
  hello_edit->insert_text.line = 1;
  hello_edit->insert_text.index = 4;
  hello_edit->insert_text.text = malloc_literal("o world"); 

  collab_enqueue(&collab_queue, hello_edit);

  collab_applyedits(&collab_queue);

  ASSERT_STREQ("Hello world!", reinterpret_cast<char *>(ml_get(1))); 

  // Check that queue is now empty
  ASSERT_EQ(NULL, collab_dequeue(&collab_queue));
}

// Tests that a single collabedit_T delete text is applied.
TEST_F(CollaborativeEditQueue, applies_delete_text) {
  // Start with some text in the buffer.
  ml_append_collab(0, malloc_literal("Hello qwerty world!"), 0, FALSE, FALSE);
  appended_lines_mark(1, 1);

  // Enqueue a delete edit and process it.
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_DELETE_TEXT;
  hello_edit->file_buf = curbuf; 
  hello_edit->delete_text.line = 1;
  hello_edit->delete_text.index = 6;
  hello_edit->delete_text.length = 7;

  collab_enqueue(&collab_queue, hello_edit);
  
  collab_applyedits(&collab_queue);
  
  ASSERT_STREQ("Hello world!", reinterpret_cast<char *>(ml_get(1))); 

  // Check that queue is now empty
  ASSERT_EQ(NULL, collab_dequeue(&collab_queue));
}

// Tests that multiple collabedit_T's with different file_buf's restores 
// curbuf.
TEST_F(CollaborativeEditQueue, restores_curbuf) {
  // Keep track of curbuf for later.
  buf_T *oldbuf = curbuf;

  // Create a few different buffers to apply edits to.
  buf_T *buffalo = buflist_new(NULL, NULL, 0, 0);
  buf_T *buffoon = buflist_new(NULL, NULL, 0, 0);
  buf_T *buffet = buflist_new(NULL, NULL, 0, 0);

  // Enqueue a few edits with different buffers.
  collabedit_T *edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_APPEND_LINE;
  edit->file_buf = buffalo; 
  edit->append_line.line = 0;
  edit->append_line.text = malloc_literal("Hello buffalo!"); 
  collab_enqueue(&collab_queue, edit);
  
  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_APPEND_LINE;
  edit->file_buf = buffoon;
  edit->append_line.line = 0;
  edit->append_line.text = malloc_literal("Hello buffoon!"); 
  collab_enqueue(&collab_queue, edit);

  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_APPEND_LINE;
  edit->file_buf = buffet; 
  edit->append_line.line = 0;
  edit->append_line.text = malloc_literal("Hello buffet!"); 
  collab_enqueue(&collab_queue, edit);

  // Apply the edits.
  collab_applyedits(&collab_queue);

  // Confirm that the curbuf is still the same.
  ASSERT_EQ(oldbuf, curbuf); 
  ASSERT_STREQ("" , reinterpret_cast<char *>(ml_get(1)));

  // Confirm contents of different buffers.
  set_curbuf(buffalo, DOBUF_GOTO);
  ASSERT_STREQ("Hello buffalo!", reinterpret_cast<char *>(ml_get(1)));

  set_curbuf(buffoon, DOBUF_GOTO);
  ASSERT_STREQ("Hello buffoon!", reinterpret_cast<char *>(ml_get(1)));

  set_curbuf(buffet, DOBUF_GOTO);
  ASSERT_STREQ("Hello buffet!", reinterpret_cast<char *>(ml_get(1)));
}

// Tests that multiple collabedit_T's of different types can be applied.
TEST_F(CollaborativeEditQueue, applies_many_edits) {
  // Enqueue a few edits.
  // Line 1: Hello
  collabedit_T *edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_APPEND_LINE;
  edit->file_buf = curbuf;
  edit->append_line.line = 0;
  edit->append_line.text = malloc_literal("Hello");
  collab_enqueue(&collab_queue, edit);
 
  // Line 1: Hello world!
  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_INSERT_TEXT;
  edit->file_buf = curbuf; 
  edit->insert_text.line = 1;
  edit->insert_text.index = 5;
  edit->insert_text.text = malloc_literal(" world!"); 
  collab_enqueue(&collab_queue, edit);

  // Line 1: Test my 
  // Line 2: Hello world!
  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_APPEND_LINE;
  edit->file_buf = curbuf;
  edit->append_line.line = 0;
  edit->append_line.text = malloc_literal("Test my");
  collab_enqueue(&collab_queue, edit);

  // Line 1: Test my 
  // Line 2: world!
  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_DELETE_TEXT;
  edit->file_buf = curbuf; 
  edit->delete_text.line = 2;
  edit->delete_text.index = 0;
  edit->delete_text.length = 6;
  collab_enqueue(&collab_queue, edit);

  // Line 1: Test my 
  // Line 2: programmatic
  // Line 3: world!
  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_APPEND_LINE;
  edit->file_buf = curbuf;
  edit->append_line.line = 1;
  edit->append_line.text = malloc_literal("programmatic");
  collab_enqueue(&collab_queue, edit);

  // Line 1: programmatic
  // Line 2: world!
  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_REMOVE_LINE;
  edit->file_buf = curbuf;
  edit->remove_line.line = 1;
  collab_enqueue(&collab_queue, edit);

  // Process edit queue.
  collab_applyedits(&collab_queue);

  // Confirm expected output.
  ASSERT_STREQ("programmatic", reinterpret_cast<char *>(ml_get(1)));
  ASSERT_STREQ("world!", reinterpret_cast<char *>(ml_get(2))); 

  // Check that queue is now empty.
  ASSERT_EQ(NULL, collab_dequeue(&collab_queue));
}

// Tests that the cursor is adjusted to append lines.
TEST_F(CollaborativeEditQueue, cursor_adjusted_to_append_line) {
  // Start with some text in the buffer and set up init cursor.
  ml_append_collab(0, malloc_literal("Hello world!"), 0, FALSE, FALSE);
  appended_lines_mark(1, 1);
  curwin->w_cursor.lnum = 1;
  curwin->w_cursor.col= 5;

  // Append after cursor.
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_APPEND_LINE;
  hello_edit->file_buf = curbuf; 
  hello_edit->append_line.line = 1;
  hello_edit->append_line.text = malloc_literal("After cursor."); 
  collab_enqueue(&collab_queue, hello_edit);
  collab_applyedits(&collab_queue);

  // Cursor shouldn't have moved.
  ASSERT_EQ(1, curwin->w_cursor.lnum);
  ASSERT_EQ(5, curwin->w_cursor.col);
  
  // Append before cursor.
  hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_APPEND_LINE;
  hello_edit->file_buf = curbuf; 
  hello_edit->append_line.line = 0;
  hello_edit->append_line.text = malloc_literal("Before cursor."); 
  collab_enqueue(&collab_queue, hello_edit);
  collab_applyedits(&collab_queue);

  // Cursor should have moved a line down.
  ASSERT_EQ(2, curwin->w_cursor.lnum);
  ASSERT_EQ(5, curwin->w_cursor.col);
}

// Tests that the cursor is adjusted to remove lines.
TEST_F(CollaborativeEditQueue, cursor_adjusted_to_remove_line) {
  // Start with some text in the buffer and set up init cursor.
  ml_append_collab(0, malloc_literal("Hello world!"), 0, FALSE, FALSE);
  ml_append_collab(0, malloc_literal("Just another test string."), 0, FALSE, FALSE);
  ml_append_collab(0, malloc_literal("What did you expect?"), 0, FALSE, FALSE);
  ml_append_collab(0, malloc_literal("One more for good luck."), 0, FALSE, FALSE);
  appended_lines_mark(1, 4);
  curwin->w_cursor.lnum = 3;
  curwin->w_cursor.col= 5;

  // Delete the last line. 
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_REMOVE_LINE;
  hello_edit->file_buf = curbuf;
  hello_edit->remove_line.line = 4;
  collab_enqueue(&collab_queue, hello_edit);
  collab_applyedits(&collab_queue);

  // Cursor shouldn't have moved.
  ASSERT_EQ(3, curwin->w_cursor.lnum);
  ASSERT_EQ(5, curwin->w_cursor.col);
 
  // Delete a line above cursor. 
  hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_REMOVE_LINE;
  hello_edit->file_buf = curbuf;
  hello_edit->remove_line.line = 2;
  collab_enqueue(&collab_queue, hello_edit);
  collab_applyedits(&collab_queue);

  // Cursor should have moved up a line.
  ASSERT_EQ(2, curwin->w_cursor.lnum);
  ASSERT_EQ(5, curwin->w_cursor.col);
 
  // Delete the line of the cursor. 
  hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_REMOVE_LINE;
  hello_edit->file_buf = curbuf;
  hello_edit->remove_line.line = 2;
  collab_enqueue(&collab_queue, hello_edit);
  collab_applyedits(&collab_queue);

  // Cursor should have moved to end of line above.
  ASSERT_EQ(1, curwin->w_cursor.lnum);
  int end_col = STRLEN(ml_get(1)) - 1;
  ASSERT_EQ(end_col, curwin->w_cursor.col);
}

// Tests that the cursor is adjusted to insert texts.
TEST_F(CollaborativeEditQueue, cursor_adjusted_to_insert_text) {
  // Start with some text in the buffer and set up init cursor.
  ml_append_collab(0, malloc_literal("Hello world!"), 0, FALSE, FALSE);
  appended_lines_mark(1, 1);
  curwin->w_cursor.lnum = 1;
  curwin->w_cursor.col= 5;

  // Insert after the cursor.
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_INSERT_TEXT;
  hello_edit->file_buf = curbuf; 
  hello_edit->insert_text.line = 1;
  hello_edit->insert_text.index = 7;
  hello_edit->insert_text.text = malloc_literal("qwerty"); 
  collab_enqueue(&collab_queue, hello_edit);
  collab_applyedits(&collab_queue);

  // Cursor shouldn't have moved.
  ASSERT_EQ(1, curwin->w_cursor.lnum);
  ASSERT_EQ(5, curwin->w_cursor.col);
 
  // Insert before the cursor.
  hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_INSERT_TEXT;
  hello_edit->file_buf = curbuf; 
  hello_edit->insert_text.line = 1;
  hello_edit->insert_text.index = 0;
  hello_edit->insert_text.text = malloc_literal("X"); 
  collab_enqueue(&collab_queue, hello_edit);
  collab_applyedits(&collab_queue);

  // Cursor should have moved forward one column.
  ASSERT_EQ(1, curwin->w_cursor.lnum);
  ASSERT_EQ(6, curwin->w_cursor.col);
}

// Tests that the cursor is adjusted to delete texts.
TEST_F(CollaborativeEditQueue, cursor_adjusted_to_delete_text) {
  // Start with some text in the buffer and set up init cursor.
  ml_append_collab(0, malloc_literal("Hello world!"), 0, FALSE, FALSE);
  appended_lines_mark(1, 1);
  curwin->w_cursor.lnum = 1;
  curwin->w_cursor.col= 5;

  // Delete after the cursor.
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_DELETE_TEXT;
  hello_edit->file_buf = curbuf; 
  hello_edit->delete_text.line = 1;
  hello_edit->delete_text.index = 6;
  hello_edit->delete_text.length = 1;
  collab_enqueue(&collab_queue, hello_edit);
  collab_applyedits(&collab_queue);
  
  // Cursor shouldn't have moved.
  ASSERT_EQ(1, curwin->w_cursor.lnum);
  ASSERT_EQ(5, curwin->w_cursor.col);

  // Delete before the cursor.
  hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_DELETE_TEXT;
  hello_edit->file_buf = curbuf; 
  hello_edit->delete_text.line = 1;
  hello_edit->delete_text.index = 0;
  hello_edit->delete_text.length = 2;
  collab_enqueue(&collab_queue, hello_edit);
  collab_applyedits(&collab_queue);
  
  // Cursor should have moved to the left by 2.
  ASSERT_EQ(1, curwin->w_cursor.lnum);
  ASSERT_EQ(3, curwin->w_cursor.col);

  // Delete over the cursor.
  hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_DELETE_TEXT;
  hello_edit->file_buf = curbuf; 
  hello_edit->delete_text.line = 1;
  hello_edit->delete_text.index = 1;
  hello_edit->delete_text.length = 6;
  collab_enqueue(&collab_queue, hello_edit);
  collab_applyedits(&collab_queue);
  
  // Cursor should have moved to start of the delete.
  ASSERT_EQ(1, curwin->w_cursor.lnum);
  ASSERT_EQ(1, curwin->w_cursor.col);
}
