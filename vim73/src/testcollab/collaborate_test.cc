// TODO(zpotter): Add legal headers

// A test runner for all functionality provided in collaborate.c

#include "gtest/gtest.h"

extern "C" {
#include "vim.h"
#include "collab_structs.h"
#include "collab_util.h"
}

// A test fixture class that sets up the default window and buffer for SetUp,
// and clears the collaborative edit queue on TearDown.
class CollaborativeEditQueue : public testing::Test {
 protected:
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
  ASSERT_EQ(CSI, inbuf[0]);
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

// Takes a string literal and copies it into freshly malloc'ed memory.
// Meant for setting strings that will later be freed.
// Returns the new heap string as a char_u*.
static char_u* malloc_literal(const std::string& kStr) {
  // Assert that sizeof(char_u) is 1 for memcpy's sake.
  static_assert((sizeof(char_u) == 1), "sizeof char_u should be 1 for memcpy.");
  size_t num_bytes = (kStr.length() + 1); // Add 1 for the null byte
  char_u *str = static_cast<char_u *>(malloc(num_bytes));
  if (str == NULL) return NULL;
  memcpy(str, kStr.c_str(), num_bytes);
  return str;
}

// Tests that a single collabedit_T text insert is applied.
TEST_F(CollaborativeEditQueue, applies_text_insert) {
  // Enqueue an insert edit and process it.
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_TEXT_INSERT;
  hello_edit->file_buf = curbuf; 
  hello_edit->edit.text_insert.line = 0;
  hello_edit->edit.text_insert.text = malloc_literal("Hello world!\n"); 

  collab_enqueue(&collab_queue, hello_edit);
  
  collab_applyedits(&collab_queue);
  
  ASSERT_STREQ("Hello world!\n", reinterpret_cast<char *>(ml_get(1))); 

  // Check that queue is now empty
  ASSERT_EQ(NULL, collab_dequeue(&collab_queue));
}

// Tests that a single collabedit_T text delete is applied.
TEST_F(CollaborativeEditQueue, applies_text_delete) {
  // Start with some text in the buffer.
  ml_append(0, malloc_literal("Hello\n"), 0, FALSE);
  ml_append(1, malloc_literal("world!\n"), 0, FALSE);
  appended_lines_mark(1, 2);

  // Enqueue a delete edit and process it
  collabedit_T *hello_edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  hello_edit->type = COLLAB_TEXT_DELETE;
  hello_edit->file_buf = curbuf;
  hello_edit->edit.text_delete.line = 1;

  collab_enqueue(&collab_queue, hello_edit);
  
  collab_applyedits(&collab_queue);
  
  ASSERT_STREQ("world!\n", reinterpret_cast<char *>(ml_get(1)));

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
  edit->type = COLLAB_TEXT_INSERT;
  edit->file_buf = buffalo; 
  edit->edit.text_insert.line = 0;
  edit->edit.text_insert.text = malloc_literal("Hello buffalo!\n"); 
  collab_enqueue(&collab_queue, edit);
  
  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_INSERT;
  edit->file_buf = buffoon;
  edit->edit.text_insert.line = 0;
  edit->edit.text_insert.text = malloc_literal("Hello buffoon!\n"); 
  collab_enqueue(&collab_queue, edit);

  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_INSERT;
  edit->file_buf = buffet; 
  edit->edit.text_insert.line = 0;
  edit->edit.text_insert.text = malloc_literal("Hello buffet!\n"); 
  collab_enqueue(&collab_queue, edit);

  // Apply the edits.
  collab_applyedits(&collab_queue);

  // Confirm that the curbuf is still the same.
  ASSERT_EQ(oldbuf, curbuf); 
  ASSERT_STREQ("" , reinterpret_cast<char *>(ml_get(1)));

  // Confirm contents of different buffers.
  set_curbuf(buffalo, DOBUF_GOTO);
  ASSERT_STREQ("Hello buffalo!\n", reinterpret_cast<char *>(ml_get(1)));

  set_curbuf(buffoon, DOBUF_GOTO);
  ASSERT_STREQ("Hello buffoon!\n", reinterpret_cast<char *>(ml_get(1)));

  set_curbuf(buffet, DOBUF_GOTO);
  ASSERT_STREQ("Hello buffet!\n", reinterpret_cast<char *>(ml_get(1)));
}

// Tests that multiple collabedit_T's of different types can be applied.
TEST_F(CollaborativeEditQueue, applies_many_edits) {
  // Enqueue a few edits.
  collabedit_T *edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_INSERT;
  edit->file_buf = curbuf;
  edit->edit.text_insert.line = 0;
  edit->edit.text_insert.text = malloc_literal("Hello\n");
  collab_enqueue(&collab_queue, edit);
 
  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_INSERT;
  edit->file_buf = curbuf;
  edit->edit.text_insert.line = 1;
  edit->edit.text_insert.text = malloc_literal("world!\n");
  collab_enqueue(&collab_queue, edit);

  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_DELETE;
  edit->file_buf = curbuf;
  edit->edit.text_delete.line = 1;
  collab_enqueue(&collab_queue, edit);

  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_INSERT;
  edit->file_buf = curbuf;
  edit->edit.text_insert.line = 0;
  edit->edit.text_insert.text = malloc_literal("Test my\n");
  collab_enqueue(&collab_queue, edit);

  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_INSERT;
  edit->file_buf = curbuf;
  edit->edit.text_insert.line = 1;
  edit->edit.text_insert.text = malloc_literal("programmatic\n");
  collab_enqueue(&collab_queue, edit);

  edit = (collabedit_T*) malloc(sizeof(collabedit_T));
  edit->type = COLLAB_TEXT_DELETE;
  edit->file_buf = curbuf;
  edit->edit.text_delete.line = 1;
  collab_enqueue(&collab_queue, edit);

  // Process edit queue.
  collab_applyedits(&collab_queue);

  // Confirm expected output.
  ASSERT_STREQ("programmatic\n", reinterpret_cast<char *>(ml_get(1)));
  ASSERT_STREQ("world!\n", reinterpret_cast<char *>(ml_get(2))); 

  // Check that queue is now empty.
  ASSERT_EQ(NULL, collab_dequeue(&collab_queue));
}

