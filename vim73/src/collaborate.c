/*
 * Notice we don't use vim's alloc or vim_free here. Not thread-safe.
 *
 *
 *
 */

#include "vim.h"

#include <pthread.h>

typedef struct editqueue_S {
    collabedit_T *edit;
    struct editqueue_S *next;
} editqueue_T;

static editqueue_T *edithead = NULL;
static editqueue_T *edittail = NULL;

static pthread_mutex_t qmutex = PTHREAD_MUTEX_INITIALIZER;
static int pending_edits = FALSE;

// Provides methods for communicating with the JS layer
// Translates PPB_Var's <--> rt_event's
// TODO(zpotter): these ->
// void send_collabedit(collabedit_T *ev);
// bool receive_collabedit(PPB_Var *var);

// Queues rt_events from rt_interface
// This queue allows async messages to come from NaCL & realtime.
// Fires an event (by inserting into keyboard input buffer) to let vim's main
// thread process rt_events.
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

    pending_edits = TRUE;

    // Release exclusive access to queue
    pthread_mutex_unlock(&qmutex);
}

collabedit_T* collab_dequeue() {
    collabedit_T *first = NULL;
    // Wait for exclusive access to the queue
    pthread_mutex_lock(&qmutex);    

    if (edithead != NULL) {
        first = edithead->edit;
        free(edithead);
        edithead = edithead->next;
    } else {
        // Queue empty
        pending_edits = FALSE;
    }
    // Release exclusive access to the queue
    pthread_mutex_unlock(&qmutex);

    return first;
}

void process_collabedits() {
    collabedit_T *cedit = NULL;
    while ((cedit = collab_dequeue()) != NULL) {
        // Process the collabedit_T
        ml_append(cedit->linenum, cedit->text, 0, FALSE);
        appended_lines_mark(cedit->linenum+1, 1);
        free(cedit);
    }
}

void pending_collabedits() {
    int pending_value = FALSE;
    // Wait for exclusive access to the queue
    pthread_mutex_lock(&qmutex);    
    pending_value = pending_edits;
    // Release exclusive access to the queue
    pthread_mutex_unlock(&qmutex);

    if (pending_value) {
        char_u collab_keys[3] = { CSI, KS_EXTRA, KE_COLLABEDIT };
        ui_inchar_undo(collab_keys, 3);
    }
}

