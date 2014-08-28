// Copyright 2014 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
 * will be referenced by 'buffer_id'. It is opened with the filename 'fname'.
 * TODO(zpotter): Add a way to close and reclaim buffer ID's.
 */
void collab_newbuf(int buffer_id, char_u *fname) {
  if (buffer_id >= collab_capacity) {
    // Grow collab_bufs array.
    size_t newlen = MAX(2 * collab_capacity, buffer_id + 1);
    collab_bufs = realloc(collab_bufs, newlen);
    collab_capacity = newlen;
  }
  // Create and store the new buffer.
  collab_bufs[buffer_id] = buflist_new(fname, NULL, 1, 0);
  ++num_bufs;
}

/*
 * Sets the 'curbuf' global to the collab buffer identified by 'buffer_id'.
 * Returns TRUE on a successful switch or FALSE if ID doesn't match a buffer.
 */
int collab_setbuf(int buffer_id) {
  if (buffer_id >= num_bufs || !collab_bufs[buffer_id])
    return FALSE;
  // Only call set_curbuf if actually switching to a different buffer.
  if (curbuf != collab_bufs[buffer_id])
    set_curbuf(collab_bufs[buffer_id], DOBUF_GOTO);
  return TRUE;
}

/*
 * Returns the buffer ID for 'buf' or -1 if 'buf' isn't tracked as a
 * collaborative buffer.
 */
int collab_get_id(buf_T *buf) {
  for (int bid = 0; bid < num_bufs; ++bid) {
    if (collab_bufs[bid] == buf)
      return bid;
  }
  return -1;
}

/* The last known position of the local user's cursor. */
static pos_T last_pos;

/*
 * A simple struct to track highlight groups for user cursors.
 */
struct collabcursor_S {
  /* A unique string for each editor's cursor. Must match regex
     "[a-zA-Z0-9_]*". */
  char_u *user_id;
  /* The highlight match ID as returned by match_add(). */
  int match_id;
};

/* An array of remote cursors to highlight. */
static struct collabcursor_S *cursors = NULL;
/* The number of remote cursors in 'cursors'. */
static size_t num_cursors = 0;
/* The number of cursors able to be held in 'cursors'. */
static size_t cursor_capacity = 0;

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
    case COLLAB_CURSOR_MOVE:
    {
      // Collaborators' cursor positions are updated and shown here. This is
      // done with a bit of hackage, but will do for now. We show remote cursors
      // by highlighting the backgrounds of cursor positions. Vim's codebase
      // doesn't expose an easy way to do this, so we execute highlight commands
      // as if the user was executing them in command mode.
      // TODO(zpotter): Implement remote cursor positions with JS and HTML in
      // classic Docs style.
      struct collabcursor_S *cursor = NULL;
      // If user_id has been seen before, clear old match.
      for (size_t i = 0; i < num_cursors; ++i) {
        if (STRCMP(cursors[i].user_id, cedit->cursor_move.user_id) == 0) {
          match_delete(curwin, cursors[i].match_id, FALSE);
          cursor = &cursors[i];
          break;
        }
      }
      // If user_id is new, add it to cursor list.
      if (!cursor) {
        // Resize 'cursors' if full.
        if (num_cursors >= cursor_capacity) {
          cursor_capacity = MAX(2 * cursor_capacity, 4);
          cursors = realloc(cursors, cursor_capacity * sizeof(struct collabcursor_S));
        }
        // Create new cursor.
        cursors[num_cursors] = (struct collabcursor_S) {
            .user_id = NULL,
            .match_id = -1
        };
        cursor = &cursors[num_cursors];
        num_cursors++;
        cursor->user_id = malloc(STRLEN(cedit->cursor_move.user_id) + 1);
        STRCPY(cursor->user_id, cedit->cursor_move.user_id);
        // Here we create the new highlight group for user id. This is
        // equivalent to running the command ":hi <user_id> ctermbg=<color>".
        size_t group_len = STRLEN(cursor->user_id);
        syn_check_group(cursor->user_id, group_len);
        // Make hl_args large enough to hold user_id, plus color arguments:
        char_u hl_args[group_len + 10 + 1]; // strlen(" ctermbg=N") == 10
        STRCPY(hl_args, cursor->user_id);
        STRCAT(hl_args, " ctermbg=C");
        // Replace last char 'C' with a color. The color cycles through
        // chars '2'-'6'.
        hl_args[group_len + 9] = '0' + (num_cursors - 1) % 5 + 2;
        // Add the new highlight group.
        do_highlight(hl_args, FALSE, FALSE);
      }
      // Highlight the cursor position with a call to match. Match the cursor
      // position as if the following Vim command was called:
      //   ":match \%<COL>v\%<ROW>l"
      char_u cursor_pattern[100];
      sprintf((char *)cursor_pattern, "\\%%%iv\\%%%lil",
          cedit->cursor_move.pos.col + 1, cedit->cursor_move.pos.lnum);
      int mid = match_add(curwin, cursor->user_id, cursor_pattern, 0, cursor->match_id);
      cursor->match_id = mid;
      // Free up cursor_move strings.
      free(cedit->cursor_move.user_id);
      break;
    }

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
          // If cursor is past the last line, move it to the end of the
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
        collab_newbuf(cedit->buf_id, cedit->buffer_sync.filename);
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

/*
 * Updates last known position of local user's cursor.
 * If the cursor has moved since the last time this function was called,
 * the remote collaborators will be updated with the new cursor position.
 */
void collab_cursorupdate() {
  pos_T cur_pos = curwin->w_cursor;
  if (last_pos.lnum != cur_pos.lnum || last_pos.col != cur_pos.col) {
    int bid = collab_get_id(curbuf);
    // If bid < 0, buf is not actually collaborative.
    if (bid >= 0) {
      // Send the change to the remote collaborators.
      collabedit_T cursor_move = {
        .type = COLLAB_CURSOR_MOVE,
        .buf_id = bid,
        .cursor_move.pos = cur_pos
        // .cursor_move.user_id set in JS-land
      };
      collab_remoteapply(&cursor_move);
    }
  }
  // Update last known position.
  last_pos = cur_pos;
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
