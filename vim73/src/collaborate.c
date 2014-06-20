// TODO(zpotter): legal boilerplate

/*
 * Functions to asynchronously queue realtime edits and to notify vim's main
 * loop of mutations to process.
 *
 * Notice we don't use vim's alloc or vim_free here due to thread-safety.
 */

#include "vim.h"
#include <pthread.h>

/*
 * A node in the queue
 */
typedef struct editqueue_S {
    collabedit_T *edit;
    struct editqueue_S *next;
} editqueue_T;

/* The head and tail of the queue */
static editqueue_T *edithead = NULL;
static editqueue_T *edittail = NULL;

/* Must lock this mutex to safely use the queue */
static pthread_mutex_t qmutex = PTHREAD_MUTEX_INITIALIZER;

// TODO(zpotter):
// Provides methods for communicating with the JS layer
// Translates between PPB_Var's and collabedit_T's
// void send_collabedit(collabedit_T *ev);
// bool receive_collabedit(PPB_Var *var);

/*
 * Places a collabedit_T in a queue of pending edits. This function is
 * thread-safe and may block until the shared queue is safe to modify.
 */
void collab_enqueue(collabedit_T *cedit) {
    editqueue_T *node = (editqueue_T*)malloc(sizeof(editqueue_T));
    node->edit = cedit;
    node->next = NULL;

    // Wait for exclusive access to the queue
    pthread_mutex_lock(&qmutex);

    // Enqueue!
    if (edittail == NULL) {
        // Queue was empty
        edithead = node;
        edittail = node;
    } else {
        // Add to end
        edittail->next = node;
        edittail = node;
    }

    // Release exclusive access to queue
    pthread_mutex_unlock(&qmutex);
}

/*
 * Dequeue's the first collabedit_T and returns a pointer to it.
 * Returns NULL if the queue is empty. This function is thread-safe.
 */
collabedit_T* collab_dequeue() {
    collabedit_T *first = NULL;
    // Wait for exclusive access to the queue
    pthread_mutex_lock(&qmutex);    

    if (edithead != NULL) {
        first = edithead->edit;
        free(edithead);
        edithead = edithead->next;
    }
    // Release exclusive access to the queue
    pthread_mutex_unlock(&qmutex);

    return first;
}

/*
 * Applies all collabedit_T mutations to the vim file buffer and leaves the
 * queue in an empty state. This function should only be called from vim's main
 * thread when it is safe to modify the file buffer.
 */
void collab_applyedits() {
    collabedit_T *cedit;
    while ((cedit = collab_dequeue()) != NULL) {
        // Process the collabedit_T
        ml_append(cedit->linenum, cedit->text, 0, FALSE);
        appended_lines_mark(cedit->linenum+1, 1);
        free(cedit);
    }
}

/*
 * When called, if there are pending edits to process, this will insert a 
 * special character sequence into vim's user input buffer. When the sequence
 * is read, it will trigger a call to collab_process. Seems a little hacky, but
 * this is how vim processes special events. This function should only be
 * called from vim's main thread.
 */
void collab_bufcheck() {
    int pending_edits;
    // Wait for exclusive access to the queue
    pthread_mutex_lock(&qmutex);    
    pending_edits = (edithead != NULL);
    // Release exclusive access to the queue
    pthread_mutex_unlock(&qmutex);

    if (pending_edits) {
        char_u collab_keys[3] = { CSI, KS_EXTRA, KE_COLLABEDIT };
        ui_inchar_undo(collab_keys, 3);
    }
}

