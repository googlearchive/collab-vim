// TODO(zpotter): legal boilerplate

/*
 * Functions to asynchronously queue realtime edits and to notify vim's main
 * loop of mutations to process.
 *
 * Notice we don't use vim's alloc or vim_free here due to thread-safety.
 */

#include "vim.h"
#include <pthread.h>

#include "collab_structs.h"
#include "collab_util.h"
#include "vim_pepper.h"

/* The global queue to hold edits for loaded file buffers. */
editqueue_T collab_queue = { 
  .head = NULL,
  .tail = NULL,
  .mutex = PTHREAD_MUTEX_INITIALIZER,
  .event_write_fd = -1,
  .event_read_fd = -1
};

/* Sequence of keys interpretted as a collaborative event */
static const char_u collab_keys[3] = { K_SPECIAL, KS_EXTRA, KE_COLLABEDIT }; 
static const size_t collab_keys_length = 
  sizeof(collab_keys) / sizeof(collab_keys[0]);
/* The next key in the sequence to send to the user input buffer */
static int next_key_index = -1;

/*
 * Sends a local user edit to remote collaborators.
 * Implementation is specific to collaborative backend.
 */
extern void collab_remoteapply(collabedit_T *edit);

/*
 * Called from vim's main() before the main loop begins. Sets up data that
 * needs some configuration.
 */
void collab_init() {
  // Set up a signal pipe for the queue
  int fds[2];
  if (pipe(fds) == -1) {
    //TODO(zpotter): Handle this error from js-land
    js_printf("pipe failed to open");
  } else {
    collab_queue.event_read_fd = fds[0];
    collab_queue.event_write_fd = fds[1];
    // Make reads non-blocking
    fcntl(collab_queue.event_read_fd, F_SETFL, O_NONBLOCK);
  }
}

/*
 * Places a collabedit_T in a queue of pending edits. Takes ownership of cedit
 * and frees it after it has been applied to the buffer. This function is 
 * thread-safe and may block until the shared queue is safe to modify.
 */
void collab_enqueue(editqueue_T *queue, collabedit_T *cedit) {
  editnode_T *node = (editnode_T*)malloc(sizeof(editnode_T));
  node->edit = cedit;
  node->next = NULL;

  // Wait for exclusive access to the queue
  pthread_mutex_lock(&(queue->mutex));

  // Enqueue!
  if (queue->tail == NULL) {
    // Queue was empty
    queue->head = node;
  } else {
    // Add to end
    queue->tail->next = node;
  }
  queue->tail = node;

  // Release exclusive access to queue
  pthread_mutex_unlock(&(queue->mutex));

  // Vim might be waiting indefinitely for user input, so signal that there is
  // a new event to process by writing a dummy character to the event pipe. The
  // written character will later be discarded by the reading end of the pipe.
  write(queue->event_write_fd, "X", 1);
}

/*
 * Applies a single collabedit_T to the collab_buf. Frees cedit when done.
 */
static void applyedit(collabedit_T *cedit) {
  // First select the right collaborative buffer
  buf_T *oldbuf = curbuf;
  if (curbuf != cedit->file_buf) set_curbuf(cedit->file_buf, DOBUF_GOTO);
  // Apply edit depending on type
  switch (cedit->type) {
    case COLLAB_APPEND_LINE:
      ml_append(cedit->append_line.line, cedit->append_line.text, 0, FALSE);
      // Adjust cursor position.
      if (curwin->w_cursor.lnum > cedit->append_line.line)
        curwin->w_cursor.lnum++;
      // Mark lines for redraw. Just appended a line below append_line.line
      appended_lines_mark(cedit->append_line.line, 1);
      free(cedit->append_line.text);
      break;

    case COLLAB_INSERT_TEXT:
    { // Define scope for case variables.
      // TODO(zpotter) adjust char index to utf8 byte index
      pos_T ins_pos = { .lnum = cedit->insert_text.line, .col = cedit->insert_text.index };
      ins_pos_str(ins_pos, cedit->insert_text.text);
      // Adjust cursor position.
      if (curwin->w_cursor.lnum == ins_pos.lnum &&
          curwin->w_cursor.col >= ins_pos.col)
        curwin->w_cursor.col += STRLEN(cedit->insert_text.text);
      free(cedit->insert_text.text);
      break;
    }

    case COLLAB_REMOVE_LINE:
      ml_delete(cedit->remove_line.line, 0);
      // Adjust cursor position.
      if (curwin->w_cursor.lnum > cedit->remove_line.line)
        curwin->w_cursor.lnum--;
      // Mark line for redraw.
      deleted_lines_mark(cedit->remove_line.line, 1);
      break;

    case COLLAB_DELETE_TEXT:
    { // Define scope for case variables.
      // TODO(zpotter) adjust char index to utf8 byte index
      pos_T del_pos = { .lnum = cedit->delete_text.line, .col = cedit->delete_text.index };
      del_pos_bytes(del_pos, cedit->delete_text.length);
      // Adjust cursor position.
      if (curwin->w_cursor.lnum == del_pos.lnum &&
          curwin->w_cursor.col >= del_pos.col) {
        if (curwin->w_cursor.col < del_pos.col + cedit->delete_text.length) {
          curwin->w_cursor.col = del_pos.col;
        } else {
          curwin->w_cursor.col -= cedit->delete_text.length;
        }
      }
      break;
    }

    case COLLAB_REPLACE_LINE:
      // An outgoing event. Should not see this case.
      js_printf("info: applyedit unexpected COLLAB_REPLACE_LINE edit");
      break;
  }
  // Switch back to old buffer if necessary.
  if (curbuf != oldbuf) set_curbuf(oldbuf, DOBUF_GOTO);
  // Done with collabedit_T, so free it.
  free(cedit);
}

/*
 * Applies all currently pending collabedit_T mutations to the vim file buffer
 * This function should only be called from vim's main thread when it is safe
 * to modify the file buffer.
 */
void collab_applyedits(editqueue_T *queue) {
  editnode_T *edits_todo = NULL; 
  editnode_T *lastedit = NULL;
  // Dequeue entire edit queue for processing
  // Wait for exclusive access to the queue
  pthread_mutex_lock(&(queue->mutex));  
  edits_todo = queue->head;
  // Clear queue
  queue->head = NULL;
  queue->tail = NULL;
  pthread_mutex_unlock(&(queue->mutex));

  // Apply all pending edits
  while (edits_todo) {
    // Process the collabedit_T
    applyedit(edits_todo->edit);
    lastedit = edits_todo;
    edits_todo = edits_todo->next;
    free(lastedit);
  }
}

/*
 * Returns true if the queue has collabedit_T's that have not been applied.
 */
int collab_pendingedits(editqueue_T *queue) {
  int pending = 0;
  pthread_mutex_lock(&(queue->mutex));
  pending = (queue->head != NULL);
  pthread_mutex_unlock(&(queue->mutex));
  return pending;
}

/*
 * When called, if there are pending edits to process, this will copy up to
 * "maxlen" characters of a special sequence into "buf". When the sequence is
 * read by vim's user input processor, it will trigger a call to 
 * collab_applyedits. Seems a little hacky, but it's how vim processes special
 * events. This function should only be called from vim's main thread.
 * Returns the number of characters copied into the buffer.
 */
int collab_inchar(char_u *buf, int maxlen, editqueue_T *queue) {
  // If not in the process of sending the complete sequence... 
  if (next_key_index < 0 && collab_pendingedits(queue)) {
    // There are pending edits, so the sequence should begin
    next_key_index = 0;
  }

  int nkeys = 0;
  // If copying the sequence...
  if (next_key_index >= 0) {
    // Determine number of keys to copy
    nkeys = collab_keys_length - next_key_index;
    if (nkeys > maxlen) {
      nkeys = maxlen;
    }

    memcpy(buf, (collab_keys+next_key_index), nkeys*sizeof(char_u));

    // Determine the next key to copy if not complete    
    next_key_index += nkeys;
    if (next_key_index >= collab_keys_length) {
      next_key_index = -1;
    }
  }

  // Return number of copied keys
  return nkeys;
}

// Declaration in collab_util.h
collabedit_T* collab_dequeue(editqueue_T *queue) {
  if (queue->head == NULL) return NULL;

  editnode_T* oldhead = queue->head;
  queue->head = oldhead->next;

  if (queue->head == NULL) queue->tail = NULL;

  collabedit_T *popped = oldhead->edit;
  free(oldhead);

  return popped;
}
