// TODO(zpotter): legal boilerplate

/*
 * Functions to asynchronously queue realtime edits and to notify vim's main
 * loop of mutations to process.
 *
 * Notice we don't use vim's alloc or vim_free here due to thread-safety.
 */

#include "vim.h"
#include <pthread.h>

/*
 * A node in the queue
 */
typedef struct editqueue_S {
  collabedit_T *edit;
  struct editqueue_S *next;
} editqueue_T;

/* The head and tail of the queue */
static editqueue_T *edithead = NULL;
static editqueue_T *edittail = NULL;

/* Must lock this mutex to safely use the queue */
static pthread_mutex_t qmutex = PTHREAD_MUTEX_INITIALIZER;

/* The collaborative file buffer */
static buf_T *collab_buf = NULL;

/*
 * Sets the current collaborative buffer.
 * Returns the old buffer, or NULL.
 */
buf_T* collab_setbuf(buf_T *buf) {
  buf_T *old_buf = collab_buf;
  collab_buf = buf;
  return old_buf;
}

// TODO(zpotter):
// Provides methods for communicating with the JS layer
// Translates between PPB_Var's and collabedit_T's
// void send_collabedit(collabedit_T *ev);
// bool receive_collabedit(PPB_Var *var);

/*
 * Places a collabedit_T in a queue of pending edits. This function is
 * thread-safe and may block until the shared queue is safe to modify.
 */
void collab_enqueue(collabedit_T *cedit) {
  editqueue_T *node = (editqueue_T*)malloc(sizeof(editqueue_T));
  node->edit = cedit;
  node->next = NULL;

  // Wait for exclusive access to the queue
  pthread_mutex_lock(&qmutex);

  // Enqueue!
  if (edittail == NULL) {
    // Queue was empty
    edithead = node;
  } else {
    // Add to end
    edittail->next = node;
  }
  edittail = node;

  // Release exclusive access to queue
  pthread_mutex_unlock(&qmutex);
}

/*
 * Applies a single collabedit_T to the collab_buf.
 */
static void applyedit(collabedit_T *cedit) {
  // TODO(zpotter): Interpret index as character, not line numer
  switch (cedit->type) {
    case COLLAB_TEXT_DELETE:
      // Delete a line from the buffer TODO figure out buffer spec
      ml_delete(cedit->edit.text_delete.index, 0);
      // TODO(zpotter): determine if mark_adjust should be called when edit is not for current buf_T
      if (curbuf == collab_buf) {
        // If we modified the current buffer, update cursor and mark for redraw
        deleted_lines_mark(cedit->edit.text_delete.index, 1);
      }
      break;

    case COLLAB_TEXT_INSERT:
      // Add the new line to the buffer
      ml_append_buf(collab_buf, cedit->edit.text_insert.index, 
        cedit->edit.text_insert.text, 0, FALSE);
      if (curbuf == collab_buf) {
        // If we modified the current buffer, update cursor and mark for redraw
        appended_lines_mark(cedit->edit.text_insert.index+1, 1);
      }
      break;
  }
  // Done with collabedit_T, so free it
  free(cedit);
}

/*
 * Applies all currently pending collabedit_T mutations to the vim file buffer
 * This function should only be called from vim's main thread when it is safe
 * to modify the file buffer.
 */
void collab_applyedits() {
  editqueue_T *edits_todo = NULL; 
  editqueue_T *lastedit = NULL;
  // Dequeue entire edit queue for processing
  // Wait for exclusive access to the queue
  pthread_mutex_lock(&qmutex);  
  edits_todo = edithead;
  // Clear queue
  edithead = NULL;
  edittail = NULL;
  pthread_mutex_unlock(&qmutex);

  // Apply all pending edits
  while (edits_todo != NULL) {
    // Process the collabedit_T
    applyedit(edits_todo->edit);
    lastedit = edits_todo;
    edits_todo = edits_todo->next;
    free(lastedit);
  }
}

/*
 * When called, if there are pending edits to process, this will insert a 
 * special character sequence into vim's user input buffer. When the sequence
 * is read, it will trigger a call to collab_process. Seems a little hacky, but
 * this is how vim processes special events. This function should only be
 * called from vim's main thread.
 */
void collab_bufcheck() {
  int pending_edits;
  // Wait for exclusive access to the queue
  pthread_mutex_lock(&qmutex);  
  pending_edits = (edithead != NULL);
  // Release exclusive access to the queue
  pthread_mutex_unlock(&qmutex);

  if (pending_edits) {
    char_u collab_keys[3] = { CSI, KS_EXTRA, KE_COLLABEDIT };
    ui_inchar_undo(collab_keys, 3);
  }
}

