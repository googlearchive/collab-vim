/* collaborate.c */
struct collabedit_S;
struct editqueue_S;

void collab_enqueue __ARGS((struct editqueue_S *queue, struct collabedit_S *ev));
void collab_applyedits __ARGS((struct editqueue_S *queue));
int collab_inchar __ARGS((char_u *buf, int maxlen, struct editqueue_S *queue));
