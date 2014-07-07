// TODO(zpotter): legal boilerplate

/*
 * Contains utility functions for collaborate.
 * Most of these are here for the sake of testing and will not be included in
 * the rest of the vim project. Functions here aren't thread-safe unless noted.
 */

#ifndef VIM_COLLAB_UTIL_H_
#define VIM_COLLAB_UTIL_H_

#include "collab_structs.h"

/*
 * Dequeues a single collabedit_T.
 */
collabedit_T* collab_dequeue(editqueue_T *queue);

#endif // VIM_COLLAB_UTIL_H_

