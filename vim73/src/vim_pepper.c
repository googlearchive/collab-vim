/*
 * Copyright (c) 2013 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <fcntl.h>
#include <libtar.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "ppapi/c/ppb_messaging.h"
#include "ppapi_simple/ps.h"
#include "ppapi_simple/ps_event.h"
#include "ppapi_simple/ps_interface.h"
#include "nacl_io/nacl_io.h"

#include "vim.h"
#include "vim_pepper.h"
#include "collab_structs.h"

/*
 * Defined in main.c, vim's own main method.
 */
extern int nacl_vim_main(int argc, char *argv[]);

/*
 * Sets up a nacl_io filesystem for vim's runtime files, such as the vimrc and
 * help files. The 'tarfile' contains the http filesystem.
 */
static int setup_unix_environment(const char* tarfile) {
  // Extra tar achive from http filesystem.
  int ret;
  TAR* tar;
  char filename[PATH_MAX];
  strcpy(filename, "/mnt/http/");
  strcat(filename, tarfile);
  ret = tar_open(&tar, filename, NULL, O_RDONLY, 0, 0);
  if (ret) {
    printf("error opening %s\n", filename);
    return 1;
  }

  ret = tar_extract_all(tar, "/");
  if (ret) {
    printf("error extracting %s\n", filename);
    return 1;
  }

  ret = tar_close(tar);
  assert(ret == 0);
  return 0;
}

/*
 * Waits for and handles all JS -> NaCL messages.
 * Unused parameter so this function can be used with pthreads. It seems that
 * pnacl-clang doesn't like unnamed parameters.
 */
static void* js_msgloop(void *unused) {
  PSEvent* event;
  
  PSEventSetFilter(PSE_INSTANCE_HANDLEMESSAGE);
  while (1) {
    // TODO(zpotter): Right now, incoming messages that are strings are assumed
    // to be text inserts for simplicity. Figure out best way to model realtime
    // events.
    event = PSEventWaitAcquire();
    if (event->as_var.type != PP_VARTYPE_STRING) continue;

    const char *message;
    uint32_t mlen;
    message = PSInterfaceVar()->VarToUtf8(event->as_var, &mlen);
    char_u *ctext = (char_u *) malloc(mlen + 1); // +1 for null char
    memcpy(ctext, message, mlen);
    ctext[mlen] = '\0'; // Null terminate the string var

    collabedit_T *edit = (collabedit_T *) malloc(sizeof(collabedit_T));
    edit->type = COLLAB_TEXT_INSERT;
    edit->file_buf = curbuf; 
    edit->edit.text_insert.line = 0;
    edit->edit.text_insert.text = ctext;

    collab_enqueue(&collab_queue, edit);
    
    PSEventRelease(event);
  } 
  return NULL;
}

/*
 * The main execution point of this project.
 */
int nacl_main(int argc, char* argv[]) {
  if (setup_unix_environment("vim.tar"))
    return 1;

  // Start up message handler loop
  pthread_t looper;
  pthread_create(&looper, NULL, &js_msgloop, NULL);

  // Execute vim's main loop
  return nacl_vim_main(argc, argv);
}

// Print to the js console.
int js_printf(const char* format, ...) {
  char *str;
  int printed;
  va_list argp;

  va_start(argp, format);
  printed = vasprintf(&str, format, argp); 
  va_end(argp);
  if (printed >= 0) {
    // The JS nacl_term just prints any unexpected message to the JS console.
    // We can just send a plain string to have it printed.
    struct PP_Var msg = PSInterfaceVar()->VarFromUtf8(str, strlen(str));
    PSInterfaceMessaging()->PostMessage(PSGetInstanceId(), msg);
    PSInterfaceVar()->Release(msg);
  }
  return printed;
}
