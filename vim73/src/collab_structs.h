// TODO(zpotter): Legal boilerplate

/*
 * Structs to be included in structs.h
 */

typedef enum {
  COLLAB_TEXT_DELETE,	/* When a collaborator deletes text */
  COLLAB_TEXT_INSERT	/* When a collaborator inserts text */
} collabtype_T;

typedef struct {
  collabtype_T type;
  union {
    struct {
      int index;
      char_u *text;
    } text_insert;
    
    struct {
      int index;
      char_u *text;
    } text_delete;
  } edit;
} collabedit_T;


