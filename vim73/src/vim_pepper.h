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

/*
 * Functions for the communication between JavaScript and NaCL.
 */

#ifndef VIM_PEPPER_H_
#define VIM_PEPPER_H_

#include "ppapi/c/pp_var.h"
#include "collab_structs.h"

/*
 * Initialized PPB interfaces and sets up vars for message parsing.
 * Returns 0 on success, non-zero on failure.
 */
int ppb_var_init();

/*
 * Creates a collabedit_T from a PP_Var.
 * Returns NULL if 'var' is not a well formed collabedit_T.
 */
collabedit_T * collabedit_from_ppvar(struct PP_Var var);

/*
 * Creates a PP_Var from a collabedit_T.
 */
struct PP_Var ppvar_from_collabedit(const collabedit_T *edit);

/*
 * Just like printf, but output goes to the JS console.
 */
int js_printf(const char* format, ...);

#endif // VIM_PEPPER_H_
