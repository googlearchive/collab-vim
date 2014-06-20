/* collaborate.c */
void collab_enqueue __ARGS((collabedit_T *ev));
collabedit_T* collab_dequeue __ARGS((void));
void collab_applyedits __ARGS((void));
void collab_bufcheck __ARGS((void));
