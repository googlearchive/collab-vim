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

/* An array of buf_T* to track collaborative events for. */
static buf_T **collab_bufs;
/* The length of collab_bufs. */
static int collab_capacity = 0;
/* The number of collaborative buf_T's. */
static int num_bufs = 0;

/*
 * Creates a new default buffer to track collabedit_T events for. The new buffer
 * will be referenced by 'bid'. It will be opened with the filename 'fname'.
 */
static void collab_addbuf(int bid, char_u *fname) {
  if (bid >= collab_capacity) {
    // Grow collab_bufs array.
    size_t newlen = MAX(2 * collab_capacity, bid + 1);
    collab_bufs = realloc(collab_bufs, newlen);
    collab_capacity = newlen;
    ++num_bufs;
  }
  // Create and store the new buffer.
  collab_bufs[bid] = buflist_new(fname, NULL, 1, 0);
}

/*
 * Sets the 'curbuf' global to the collaborative buffer identified by 'bid'.
 * Returns TRUE on a successful switch or FALSE if 'bid' doesn't match a buffer.
 */
static int collab_setbuf(int bid) {
  if (bid >= num_bufs || !collab_bufs[bid])
    return FALSE;
  // Only call set_curbuf if actually switching to a different buffer.
  if (curbuf != collab_bufs[bid])
    set_curbuf(collab_bufs[bid], DOBUF_GOTO);
  return TRUE;
}

/*
 * Returns the buffer id for 'buf' or -1 if 'buf' isn't tracked as a
 * collaborative buffer.
 */
int collab_get_bid(buf_T *buf) {
  for (int bid = 0; bid < num_bufs; ++bid) {
    if (collab_bufs[bid] == buf)
      return bid;
  }
  return -1;
}

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
  // Set up curbuf as first collaborative buffer.
  collab_bufs = malloc(sizeof(buf_T*));
  collab_capacity = 1;
  num_bufs = 1;
  collab_bufs[0] = curbuf;
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
  int did_setbuf = collab_setbuf(cedit->buf_id);
  // Apply edit depending on type
  switch (cedit->type) {
    case COLLAB_APPEND_LINE:
      ml_append_collab(cedit->append_line.line, cedit->append_line.text, 0, FALSE, FALSE);
      // Adjust cursor position: If the cursor is on a line below the newly
      // appended line, the line it was previously on has been pushed down.
      // Push the cursor down to its old position on the old line.
      if (curwin->w_cursor.lnum > cedit->append_line.line)
        curwin->w_cursor.lnum++;
      // Mark lines for redraw. Just appended a line below append_line.line
      appended_lines_mark(cedit->append_line.line, 1);
      free(cedit->append_line.text);
      break;

    case COLLAB_INSERT_TEXT:
    {
      // TODO(zpotter) adjust char index to utf8 byte index
      pos_T ins_pos = { .lnum = cedit->insert_text.line, .col = cedit->insert_text.index };
      ins_str_collab(ins_pos, cedit->insert_text.text, FALSE);
      // Adjust cursor position: If the cursor is on the edited line and after
      // the insert col, push it to the right the length of the inserted text.
      if (curwin->w_cursor.lnum == ins_pos.lnum &&
          curwin->w_cursor.col >= ins_pos.col)
        curwin->w_cursor.col += STRLEN(cedit->insert_text.text);
      free(cedit->insert_text.text);
      break;
    }

    case COLLAB_REMOVE_LINE:
      ml_delete_collab(cedit->remove_line.line, 0, FALSE);
      // Adjust cursor position...
      if (curwin->w_cursor.lnum > cedit->remove_line.line) {
        // If cursor is after removed line, shift cursor up a line.
        curwin->w_cursor.lnum--;
      } else if (curwin->w_cursor.lnum == cedit->remove_line.line) {
        // If cursor is on the deleted line...
        if (curwin->w_cursor.lnum > curbuf->b_ml.ml_line_count) {
          // If cursor is passed the last line, move it to the end of the
          // last line.
          curwin->w_cursor.lnum = curbuf->b_ml.ml_line_count;
          curwin->w_cursor.col = STRLEN(ml_get(curwin->w_cursor.lnum)) - 1;
        } else {
          // Move cursor to start of current line (which now has contents
          // of the next line).
          curwin->w_cursor.col = 0;
        }
      }
      // Mark line for redraw.
      deleted_lines_mark(cedit->remove_line.line, 1);
      break;

    case COLLAB_DELETE_TEXT:
    {
      // TODO(zpotter) adjust char index to utf8 byte index
      pos_T del_pos = { .lnum = cedit->delete_text.line, .col = cedit->delete_text.index };
      del_bytes_collab(del_pos, cedit->delete_text.length, FALSE);
      // Adjust cursor position: If the cursor is on the edited line and after
      // or on the start of the deleted text...
      if (curwin->w_cursor.lnum == del_pos.lnum &&
          curwin->w_cursor.col >= del_pos.col) {
        // If the cursor is on one of the deleted characters, send it to the
        // start of deleted selection. Otherwise, shift the cursor left by the
        // length of deleted text.
        if (curwin->w_cursor.col < del_pos.col + cedit->delete_text.length) {
          curwin->w_cursor.col = del_pos.col;
        } else {
          curwin->w_cursor.col -= cedit->delete_text.length;
        }
      }
      break;
    }

    case COLLAB_BUFFER_SYNC:
    {
      if (!did_setbuf) {
        // Create a new collaborative buffer.
        collab_addbuf(cedit->buf_id, cedit->buffer_sync.filename);
        did_setbuf = collab_setbuf(cedit->buf_id);
      } else {
        // Update local file name.
        setfname(curbuf, cedit->buffer_sync.filename, NULL, 0);
      }
      linenr_T cur_nlines = buflist_findfpos(curbuf)->lnum;
      linenr_T new_nlines = cedit->buffer_sync.nlines;
      // Replace any lines that already exist in buffer.
      for (int i = 1; i <= cur_nlines && i <= new_nlines; ++i) {
        ml_replace_collab(i, cedit->buffer_sync.lines[i - 1], FALSE, FALSE);
      }
      // Note that only one of the next two loops will execute their bodies.
      // Append any extra new lines.
      for (int i = cur_nlines; i < new_nlines; ++i) {
        // The first param of this function is the line number to append after.
        ml_append_collab(i, cedit->buffer_sync.lines[i], 0, FALSE, FALSE);
      }
      // Delete any extra old lines.
      for (int i = new_nlines + 1; i <= cur_nlines; ++i) {
        ml_delete_collab(i, 0, FALSE);
      }
      // Mark lines for redraw.
      changed_lines(0, 0, MAX(cur_nlines, new_nlines), (new_nlines - cur_nlines));
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
