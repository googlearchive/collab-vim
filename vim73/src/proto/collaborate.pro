/* collaborate.c */
buf_T* collab_setbuf __ARGS((buf_T *buf));
void collab_enqueue __ARGS((collabedit_T *ev));
void collab_applyedits __ARGS((void));
void collab_bufcheck __ARGS((void));
