// TODO(zpotter): legal boilerplate

/*
 * Functions to asynchronously queue realtime edits and to notify vim's main
 * loop of mutations to process.
 *
 * Notice we don't use vim's alloc or vim_free here due to thread-safety.
 */

#include "vim.h"
#include <pthread.h>

#include "collab_util.h"

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

/* Sequence of keys interpretted as a collaborative event */
static char_u collab_keys[3] = { CSI, KS_EXTRA, KE_COLLABEDIT };
/* The next key in the sequence to send to the user input buffer */
static int next_key = -1;

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

  // First select the right collaborative buffer
  buf_T *oldbuf = curbuf;
  if (curbuf != collab_buf) set_curbuf(collab_buf, DOBUF_GOTO);

  // Apply edit depending on type
  switch (cedit->type) {
    case COLLAB_TEXT_DELETE:
      // Delete the specified line from the buffer
      ml_delete(cedit->edit.text_delete.index, 0);
      // Update cursor and mark for redraw
      deleted_lines_mark(cedit->edit.text_delete.index, 1);
      break;

    case COLLAB_TEXT_INSERT:
      // Add the new line to the buffer
      ml_append(cedit->edit.text_insert.index, cedit->edit.text_insert.text, 0, FALSE);
      // Update cursor and mark for redraw
      appended_lines_mark(cedit->edit.text_insert.index+1, 1);
      break;
  }
  // Switch back to old buffer if necessary 
  if (curbuf != oldbuf) set_curbuf(oldbuf, DOBUF_GOTO);

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
 * When called, if there are pending edits to process, this will copy up to
 * "maxlen" characters of a special sequence into "buf". When the sequence is
 * read by vim's user input processor, it will trigger a call to 
 * collab_process. Seems a little hacky, but this is how vim processes special
 * events. This function should only be called from vim's main thread.
 * Returns the number of characters copied into the buffer.
 */
int collab_inchar(char_u *buf, int maxlen) {
  // If not in the process of sending the complete sequence... 
  if (next_key < 0) {
    pthread_mutex_lock(&qmutex);
    if (edithead != NULL) {
      // There are pending edits, so the sequence should begin
      next_key = 0;
    }
    pthread_mutex_unlock(&qmutex);
  }

  int nkeys = 0;
  // If copying the sequence...
  if (next_key >= 0) {
    // Determine number of keys to copy
    nkeys = 3 - next_key;
    if (nkeys > maxlen) {
      nkeys = maxlen;
    }

    memcpy(buf, (collab_keys+next_key), nkeys*sizeof(char_u));

    // Determine the next key to copy if not complete    
    next_key += nkeys;
    if (next_key >= 3) {
      next_key = -1;
    }
  }

  // Return number of copied keys
  return nkeys;
}

// Declaration in collab_util.h
collabedit_T* collab_dequeue() {
  if (edithead == NULL) return NULL;

  editqueue_T* oldhead = edithead;
  edithead = oldhead->next;

  if (edithead == NULL) edittail = NULL;

  collabedit_T *popped = oldhead->edit;
  free(oldhead);

  return popped;
}
