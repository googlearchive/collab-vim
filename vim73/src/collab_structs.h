// TODO(zpotter): Legal boilerplate

/*
 * Structs for representing collaborator information and remote file edits.
 */

#ifndef VIM_COLLAB_STRUCTS_H_
#define VIM_COLLAB_STRUCTS_H_

#include <pthread.h>

typedef enum {
  COLLAB_TEXT_DELETE,	/* When a collaborator deletes text */
  COLLAB_TEXT_INSERT	/* When a collaborator inserts text */
} collabtype_T;

/*
 * Represents a number of basic file edits.
 */
typedef struct collabedit_S {
  collabtype_T type;	/* The type determines which union member to use. */
  buf_T *file_buf;	/* The collaborative file buffer this applies to. */
  union {
    struct {		/* COLLAB_TEXT_INSERT -> text_insert */
      size_t line;	/* The line number to insert after. Line 0 inserts at
                           very start of file. */
      char_u *text;	/* The text to insert. Should end with a \n\0  */
    } text_insert;
    
    struct {		/* COLLAB_TEXT_DELETE -> text_delete */
      size_t line;	/* The line to delete. First line is 1. */
    } text_delete;
  } edit;
} collabedit_T;

/*
 * A node in the queue.
 */
typedef struct editnode_S {
  collabedit_T *edit;		/* An edit in the queue. */
  struct editnode_S *next;	/* The next edit in the queue, or NULL. */
} editnode_T;

/*
 * The queue and its mutex.
 */
typedef struct editqueue_S {
  editnode_T *head;		/* The head of the queue. */
  editnode_T *tail;		/* The tail of the queue. */
  
  pthread_mutex_t mutex;	/* Lock this mutex before modifying queue. */
} editqueue_T;

#endif // VIM_COLLAB_STRUCTS_H_

