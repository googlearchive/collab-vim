// Copyright 2014 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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
#include "ppapi/c/ppb_var_array.h"
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
 * Interface variables for manipulating PP vars/dictionaries.
 */
static const PPB_Var *ppb_var;
static const PPB_VarDictionary *ppb_dict;
static const PPB_Messaging *ppb_msg;
static const PPB_VarArray *ppb_array;
static PP_Instance pp_ins;

/*
 * Strings for each collabtype_T used in parsing messages from JS.
 */
static struct PP_Var type_append_line;
static struct PP_Var type_insert_text;
static struct PP_Var type_remove_line;
static struct PP_Var type_delete_text;
static struct PP_Var type_buffer_sync;
static struct PP_Var type_cursor_move;
static struct PP_Var type_replace_line;
static struct PP_Var type_key;
static struct PP_Var buf_id_key;
static struct PP_Var line_key;
static struct PP_Var text_key;
static struct PP_Var index_key;
static struct PP_Var length_key;
static struct PP_Var filename_key;
static struct PP_Var lines_key;
static struct PP_Var user_id_key;
static struct PP_Var column_key;

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
#define UTF8_TO_VAR(str) ppb_var->VarFromUtf8(str, strlen(str))

/*
 * Returns a null-terminated C string converted from a string PP_Var.
 * This function assumes 'str_var' is no longer needed after this call, so
 * releases it.
 */
static char_u* var_to_cstr(struct PP_Var str_var) {
  const char *vstr;
  uint32_t var_len;
  vstr = ppb_var->VarToUtf8(str_var, &var_len);
  char *cstr = malloc(var_len + 1); // +1 for null char
  strncpy(cstr, vstr, var_len);
  cstr[var_len] = '\0'; // Null terminate the string
  ppb_var->Release(str_var);
  return (char_u *)cstr;
}

/*
 * Just like strcmp, but functions on two PP_Vars.
 * Caller's responsibility to ensure v1 and v2 are strings.
 */
static int pp_strcmp(const struct PP_Var v1, const struct PP_Var v2) {
  uint32_t v1len, v2len;
  const char *v1str, *v2str;
  v1str = ppb_var->VarToUtf8(v1, &v1len);
  v2str = ppb_var->VarToUtf8(v2, &v2len);
  int cmp = strncmp(v1str, v2str, MIN(v1len, v2len));
  if (!cmp)
    cmp = v1len - v2len;
  return cmp;
}

// Create a PP_Var from a collabedit_T.
struct PP_Var ppvar_from_collabedit(const collabedit_T *edit) {
  struct PP_Var dict = ppb_dict->Create();
  // This temporary PP_Var will be Release'd after the switch cases.
  struct PP_Var text_var;
  ppb_dict->Set(dict, buf_id_key, PP_MakeInt32(edit->buf_id));
  switch (edit->type) {
    case COLLAB_CURSOR_MOVE:
      ppb_dict->Set(dict, type_key, type_cursor_move);
      ppb_dict->Set(dict, line_key, PP_MakeInt32(edit->cursor_move.pos.lnum));
      ppb_dict->Set(dict, column_key, PP_MakeInt32(edit->cursor_move.pos.col));
      break;
    case COLLAB_APPEND_LINE:
      ppb_dict->Set(dict, type_key, type_append_line);
      ppb_dict->Set(dict, line_key, PP_MakeInt32(edit->append_line.line));
      text_var = UTF8_TO_VAR((char *)edit->append_line.text);
      ppb_dict->Set(dict, text_key, text_var);
      break;
    case COLLAB_INSERT_TEXT:
      ppb_dict->Set(dict, type_key, type_insert_text);
      ppb_dict->Set(dict, line_key, PP_MakeInt32(edit->insert_text.line));
      ppb_dict->Set(dict, index_key, PP_MakeInt32(edit->insert_text.index));
      text_var = UTF8_TO_VAR((char *)edit->insert_text.text);
      ppb_dict->Set(dict, text_key, text_var);
      break;
    case COLLAB_REMOVE_LINE:
      ppb_dict->Set(dict, type_key, type_remove_line);
      ppb_dict->Set(dict, line_key, PP_MakeInt32(edit->remove_line.line));
      break;
    case COLLAB_DELETE_TEXT:
      ppb_dict->Set(dict, type_key, type_delete_text);
      ppb_dict->Set(dict, line_key, PP_MakeInt32(edit->delete_text.line));
      ppb_dict->Set(dict, index_key, PP_MakeInt32(edit->delete_text.index));
      ppb_dict->Set(dict, length_key, PP_MakeInt32(edit->delete_text.length));
      break;
    case COLLAB_REPLACE_LINE:
      ppb_dict->Set(dict, type_key, type_replace_line);
      ppb_dict->Set(dict, line_key, PP_MakeInt32(edit->replace_line.line));
      text_var = UTF8_TO_VAR((char *)edit->replace_line.text);
      ppb_dict->Set(dict, text_key, text_var);
      break;
    case COLLAB_BUFFER_SYNC:
      ppb_dict->Set(dict, type_key, type_buffer_sync);
      // Outgoing message has no other information.
      break;
  }
  // Free the ref-counted temporary variable.
  ppb_var->Release(text_var);
  return dict;
}

// Create a collabedit_T from a PP_Var
collabedit_T * collabedit_from_ppvar(struct PP_Var dict) {
  // Ignore anything that isn't a dictionary representing a collabedit.
  if (dict.type != PP_VARTYPE_DICTIONARY ||
      !ppb_dict->HasKey(dict, type_key)) {
    return NULL;
  }

  //  Create a collabedit_T to represent the edit.
  collabedit_T *edit = (collabedit_T *) malloc(sizeof(collabedit_T));
  edit->buf_id = ppb_dict->Get(dict, buf_id_key).value.as_int;

  // Parse the specific type of collabedit.
  struct PP_Var var_type = ppb_dict->Get(dict, type_key);
  if (pp_strcmp(var_type, type_cursor_move) == 0) {
    edit->type = COLLAB_CURSOR_MOVE;
    edit->cursor_move.user_id = var_to_cstr(ppb_dict->Get(dict, user_id_key));
    int lnum = ppb_dict->Get(dict, line_key).value.as_int;
    int col = ppb_dict->Get(dict, column_key).value.as_int;
    edit->cursor_move.pos = (pos_T) {
        .lnum = lnum,
        .col = col
    };

  } else if (pp_strcmp(var_type, type_append_line) == 0) {
    edit->type = COLLAB_APPEND_LINE;
    edit->append_line.line = ppb_dict->Get(dict, line_key).value.as_int;
    edit->append_line.text = var_to_cstr(ppb_dict->Get(dict, text_key));

  } else if (pp_strcmp(var_type, type_insert_text) == 0) {
    edit->type = COLLAB_INSERT_TEXT;
    edit->insert_text.line = ppb_dict->Get(dict, line_key).value.as_int;
    edit->insert_text.index = ppb_dict->Get(dict, index_key).value.as_int;
    edit->insert_text.text = var_to_cstr(ppb_dict->Get(dict, text_key));

  } else if (pp_strcmp(var_type, type_remove_line) == 0) {
    edit->type = COLLAB_REMOVE_LINE;
    edit->remove_line.line = ppb_dict->Get(dict, line_key).value.as_int;

  } else if (pp_strcmp(var_type, type_delete_text) == 0) {
    edit->type = COLLAB_DELETE_TEXT;
    edit->delete_text.line = ppb_dict->Get(dict, line_key).value.as_int;
    edit->delete_text.index = ppb_dict->Get(dict, index_key).value.as_int;
    edit->delete_text.length = ppb_dict->Get(dict, length_key).value.as_int;

  } else if (pp_strcmp(var_type, type_replace_line) == 0) {
    edit->type = COLLAB_REPLACE_LINE;
    edit->replace_line.line = ppb_dict->Get(dict, line_key).value.as_int;
    edit->replace_line.text = var_to_cstr(ppb_dict->Get(dict, text_key));

  } else if (pp_strcmp(var_type, type_buffer_sync) == 0) {
    edit->type = COLLAB_BUFFER_SYNC;
    edit->buffer_sync.filename = var_to_cstr(ppb_dict->Get(dict, text_key));

    struct PP_Var line_list = ppb_dict->Get(dict, lines_key);
    edit->buffer_sync.nlines = ppb_array->GetLength(line_list);
    edit->buffer_sync.lines = calloc(edit->buffer_sync.nlines, sizeof(char_u*));

    for (uint32_t lnum = 0; lnum < edit->buffer_sync.nlines; ++lnum) {
      char_u *line = var_to_cstr(ppb_array->Get(line_list, lnum));
      edit->buffer_sync.lines[lnum] = line;
    }
    ppb_var->Release(line_list);

  } else {
    // Unknown collabtype_T
    free(edit);
    edit = NULL;
  }
  return edit;
}

/*
 * Waits for and handles all JS -> NaCL messages.
 * Unused parameter so this function can be used with pthreads. It seems that
 * pnacl-clang doesn't like unnamed parameters.
 */
static void* js_msgloop(void *unused) {
  // Filter to all JS messages.
  PSEventSetFilter(PSE_INSTANCE_HANDLEMESSAGE);
  PSEvent* event;
  while (1) {
    // Wait for the next event.
    event = PSEventWaitAcquire();
    collabedit_T *edit = collabedit_from_ppvar(event->as_var);
    // Enqueue the edit for processing from the main thread.
    if (edit != NULL)
      collab_enqueue(&collab_queue, edit);
    PSEventRelease(event);
  }
  // Never reached.
  return NULL;
}

/*
 * Function prototype declared in proto/collaborate.pro, extern decleration
 * in collaborate.c.
 *
 * This implementation of the function sends collabedits to the Drive Realtime
 * model via Pepper messaging.
 */
void collab_remoteapply(collabedit_T *edit) {
  // Turn edit into a PP_Var.
  struct PP_Var dict = ppvar_from_collabedit(edit);
  // Send the message to JS.
  ppb_msg->PostMessage(pp_ins, dict);
  // Clean up leftovers.
  ppb_var->Release(dict);
}

int ppb_var_init() {
  // Get the interface variables for manipulating PP_Vars.
  ppb_var = PSInterfaceVar();
  ppb_dict = PSGetInterface(PPB_VAR_DICTIONARY_INTERFACE);
  ppb_array = PSGetInterface(PPB_VAR_ARRAY_INTERFACE);
  ppb_msg = PSInterfaceMessaging();
  pp_ins = PSGetInstanceId();
  // Check for missing interfaces.
  if (ppb_var == NULL || ppb_dict == NULL ||
      ppb_msg == NULL || ppb_array == NULL) {
    return 1;
  }

  // Create PP_Vars for parsing and creating collabedit JS messages
  type_append_line = UTF8_TO_VAR("append_line");
  type_insert_text = UTF8_TO_VAR("insert_text");
  type_remove_line = UTF8_TO_VAR("remove_line");
  type_delete_text = UTF8_TO_VAR("delete_text");
  type_buffer_sync = UTF8_TO_VAR("buffer_sync");
  type_cursor_move = UTF8_TO_VAR("cursor_move");
  type_replace_line = UTF8_TO_VAR("replace_line");
  type_key = UTF8_TO_VAR("collabedit_type");
  buf_id_key = UTF8_TO_VAR("buf_id");
  line_key = UTF8_TO_VAR("line");
  text_key = UTF8_TO_VAR("text");
  index_key = UTF8_TO_VAR("index");
  length_key = UTF8_TO_VAR("length");
  filename_key = UTF8_TO_VAR("filename");
  lines_key = UTF8_TO_VAR("lines");
  user_id_key = UTF8_TO_VAR("user_id");
  column_key = UTF8_TO_VAR("column");

  return 0;
}

/*
 * The main execution point of this project.
 */
int nacl_main(int argc, char* argv[]) {
  if (setup_unix_environment("vim.tar"))
    return 1;

  if (ppb_var_init())
    return 2;

  // Start up message handler loop
  pthread_t looper;
  pthread_create(&looper, NULL, &js_msgloop, NULL);

  // Tell JS and Realtime that Vim is ready to receive the init file.
  collabedit_T sync = { .type = COLLAB_BUFFER_SYNC, .buf_id = 0 };
  collab_remoteapply(&sync);

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
    struct PP_Var msg = ppb_var->VarFromUtf8(str, strlen(str));
    ppb_msg->PostMessage(pp_ins, msg);
    ppb_var->Release(msg);
  }
  return printed;
}
