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
 * Structs for representing collaborator information and file edit events.
 */

#ifndef VIM_COLLAB_STRUCTS_H_
#define VIM_COLLAB_STRUCTS_H_

#include <pthread.h>
#include "vim.h"

/*
 * Enumerations for different types of collaborative edits.
 */
typedef enum {
  COLLAB_CURSOR_MOVE, /* A user's cursor has moved. */
  COLLAB_APPEND_LINE, /* A new line was added to the document. */
  COLLAB_INSERT_TEXT, /* Text was inserted into an existing line. */
  COLLAB_REMOVE_LINE, /* A line was removed from the document. */
  COLLAB_DELETE_TEXT, /* Text was deleted from an existing line. */
  COLLAB_BUFFER_SYNC, /* A new document was opened or needs syncing. */
  COLLAB_REPLACE_LINE /* A line was replaced with new text. */
} collabtype_T;

/*
 * Represents a number of basic file edits.
 * Line numbers in this struct are 1-based, meaning line 1 is the first line.
 */
typedef struct collabedit_S {
  collabtype_T type;  /* The type determines which union member to use. */
  int buf_id;         /* A unique ID for the buffer this edit applies to. */
  union {
    struct {          /* Type: COLLAB_APPEND_LINE */
      linenr_T line;  /* The line to add after. Line 0 adds a new 1st line. */
      char_u *text;   /* The text to initialize the line with. Shouldn't end
                         with a newline. Ownership of 'text' transfered here. */
    } append_line;

    struct {          /* Type: COLLAB_INSERT_TEXT */
      linenr_T line;  /* The line to insert text into. */
      colnr_T index;  /* The character in the line to insert before. */
      char_u *text;   /* The text to insert. Ownership of 'text' transfered
                         here. */
    } insert_text;

    struct {          /* Type: COLLAB_REMOVE_LINE */
      linenr_T line;  /* The line to remove from the document. */
    } remove_line;

    struct {          /* Type: COLLAB_DELETE_TEXT */
      linenr_T line;  /* The line to remove text from. */
      colnr_T index;  /* The starting character in the line to remove. */
      size_t length;  /* The number of characters to remove. */
    } delete_text;

    struct {          /* Type: COLLAB_REPLACE_LINE */
      linenr_T line;  /* The line to replace. */
      char_u *text;   /* The new contents of the line. Shouldn't end with a
                         newline. Ownership of 'text' transfered here. */
    } replace_line;

    struct {            /* Type: COLLAB_BUFFER_SYNC */
      char_u *filename; /* The local filename. */
      linenr_T nlines;  /* The number of lines in the document. */
      char_u **lines;   /* The initial lines in the document. */
    } buffer_sync;

    struct {            /* Type: COLLAB_CURSOR_MOVE */
      char_u *user_id;  /* A unique string for each editor's cursor. Must match
                           regex "[a-zA-Z0-9_]*".
                           TODO(zpotter): Ensure JS only sends valid ID's. */
      pos_T pos;        /* The position of the cursor in the document. */
    } cursor_move;
  };
} collabedit_T;

/*
 * A node in the queue.
 */
typedef struct editnode_S {
  collabedit_T *edit;       /* An edit in the queue. */
  struct editnode_S *next;  /* The next edit in the queue, or NULL. */
} editnode_T;

/*
 * The queue and its mutex.
 */
typedef struct editqueue_S {
  editnode_T *head;       /* The head of the queue. */
  editnode_T *tail;       /* The tail of the queue. */

  pthread_mutex_t mutex;  /* Lock this mutex before modifying queue. */

  int event_write_fd;     /* After an enqueue, this file descriptor is written
                              to, causing vim's main thread to end waiting for
                              user input. */
  int event_read_fd;      /* File descriptor that contains a byte (any value)
                              for each event in the queue. */
} editqueue_T;

#endif // VIM_COLLAB_STRUCTS_H_

