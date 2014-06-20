// TODO(zpotter): Legal boilerplate

/*
 * Structs to be included in structs.h
 */

/*
 * A simple collaborative line insert
 */
typedef struct {
    int linenum;	/* The line number to insert before */
    char_u *text;	/* The new line's contents */
} collabedit_T;

