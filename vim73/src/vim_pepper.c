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
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/ppb_var_dictionary.h"
#include "ppapi_simple/ps.h"
#include "ppapi_simple/ps_event.h"
#include "ppapi_simple/ps_interface.h"
#include "nacl_io/nacl_io.h"

#include "vim.h"
#include "vim_pepper.h"
#include "collab_structs.h"

/*
 * Strings for each collabtype_T used in parsing messages from JS.
 */
static const char * const type_str = "collabedit_type";
static const char * const type_append_line = "append_line";
static const char * const type_insert_text = "insert_text";
static const char * const type_remove_line = "remove_line";
static const char * const type_delete_text = "delete_text";
static const char * const type_replace_line = "replace_line";

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
 * A macro to convert a null-terminated C string to a PP_Var.
 */
#define UTF8_TO_VAR(ppb_var_interface, str) \
  ppb_var_interface->VarFromUtf8(str, strlen(str))

/*
 * Returns a null-terminated C string converted from a string PP_Var.
 */
static char* var_to_cstr(const PPB_Var *ppb_var, struct PP_Var str_var) {
  const char *vstr;
  uint32_t var_len;
  vstr = ppb_var->VarToUtf8(str_var, &var_len);
  char *cstr = malloc(var_len + 1); // +1 for null char
  strncpy(cstr, vstr, var_len);
  cstr[var_len] = '\0'; // Null terminate the string
  return cstr;
}

/*
 * Waits for and handles all JS -> NaCL messages.
 * Unused parameter so this function can be used with pthreads. It seems that
 * pnacl-clang doesn't like unnamed parameters.
 */
static void* js_msgloop(void *unused) {
  // Get the interface variables for manipulating PP_Vars.
  const PPB_Var *ppb_var = PSGetInterface(PPB_VAR_INTERFACE);
  const PPB_VarDictionary *ppb_dict = PSGetInterface(PPB_VAR_DICTIONARY_INTERFACE);
  PSEvent* event;
  
  // Check for broken interfaces.
  if (ppb_var == NULL || ppb_dict == NULL) {
    // TODO(zpotter): Handle interface failure.
    js_printf("error: Failed to get PPB var/dictionary interface.");
    return NULL;
  }

  // PP_Vars used as keys to data in PP_Var dictionaries.
  const struct PP_Var type_key = UTF8_TO_VAR(ppb_var, type_str);
  const struct PP_Var line_key = UTF8_TO_VAR(ppb_var, "line");
  const struct PP_Var text_key = UTF8_TO_VAR(ppb_var, "text");
  const struct PP_Var index_key = UTF8_TO_VAR(ppb_var, "index");
  const struct PP_Var length_key = UTF8_TO_VAR(ppb_var, "length");

  // Filter to all JS messages.
  PSEventSetFilter(PSE_INSTANCE_HANDLEMESSAGE);
  while (1) {
    // Wait for the next event.
    event = PSEventWaitAcquire();
    struct PP_Var dict = event->as_var;
    // Ignore anything that isn't a dictionary representing a collabedit.
    if (dict.type != PP_VARTYPE_DICTIONARY ||
        !ppb_dict->HasKey(dict, type_key)) {
      // TODO(zpotter): PSEventRelease here? Or does another handler get the message next?
      js_printf("info: msgloop skipping non collabedit dict");
      continue;
    }

    //  Create a collabedit_T to later enqueue and apply.
    collabedit_T *edit = (collabedit_T *) malloc(sizeof(collabedit_T));
    edit->file_buf = curbuf; 

    // Parse the specific type of collabedit.
    char *var_type = var_to_cstr(ppb_var, ppb_dict->Get(dict, type_key));
    if (strcmp(var_type, type_append_line) == 0) {
      edit->type = COLLAB_APPEND_LINE;
      edit->append_line.line = ppb_dict->Get(dict, line_key).value.as_int;
      edit->append_line.text = (char_u *)var_to_cstr(ppb_var, ppb_dict->Get(dict, text_key));

    } else if (strcmp(var_type, type_insert_text) == 0) {
      edit->type = COLLAB_INSERT_TEXT;
      edit->insert_text.line = ppb_dict->Get(dict, line_key).value.as_int;
      edit->insert_text.index = ppb_dict->Get(dict, index_key).value.as_int;
      edit->insert_text.text = (char_u *)var_to_cstr(ppb_var, ppb_dict->Get(dict, text_key));

    } else if (strcmp(var_type, type_remove_line) == 0) {
      edit->type = COLLAB_REMOVE_LINE;
      edit->remove_line.line = ppb_dict->Get(dict, line_key).value.as_int;

    } else if (strcmp(var_type, type_delete_text) == 0) {
      edit->type = COLLAB_DELETE_TEXT;
      edit->delete_text.line = ppb_dict->Get(dict, line_key).value.as_int;
      edit->delete_text.index = ppb_dict->Get(dict, index_key).value.as_int;
      edit->delete_text.length = ppb_dict->Get(dict, length_key).value.as_int;

    } else if (strcmp(var_type, type_replace_line) == 0) {
      edit->type = COLLAB_REPLACE_LINE;
      edit->replace_line.line = ppb_dict->Get(dict, line_key).value.as_int;
      edit->replace_line.text = (char_u *)var_to_cstr(ppb_var, ppb_dict->Get(dict, text_key));

    } else {
      js_printf("info: msgloop unknown collabedit type");
      // Unknown collabtype_T
      free(edit);
      edit = NULL;
    }
    
    // Enqueue the edit for processing from the main thread.
    if (edit != NULL)
      collab_enqueue(&collab_queue, edit);
    PSEventRelease(event);
  } 
  // Never reached.
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
