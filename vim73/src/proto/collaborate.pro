/* collaborate.c */
struct collabedit_S;
struct editqueue_S;

void collab_init __ARGS((void));
void collab_newbuf __ARGS((int buffer_id, char_u *fname));
int collab_setbuf __ARGS((int buffer_id));
int collab_get_id __ARGS((buf_T *buf));
void collab_enqueue __ARGS((struct editqueue_S *queue, struct collabedit_S *ev));
void collab_applyedits __ARGS((struct editqueue_S *queue));
int collab_inchar __ARGS((char_u *buf, int maxlen, struct editqueue_S *queue));
int collab_pendingedits __ARGS((struct editqueue_S *queue));
void collab_remoteapply __ARGS((struct collabedit_S *edit));
void collab_cursorupdate __ARGS((void));
