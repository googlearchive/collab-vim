// TODO(zpotter): legal boilerplate

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
