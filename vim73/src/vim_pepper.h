// TODO(zpotter): legal boilerplate

/*
 * Functions for the communication between JavaScript and NaCL.
 */

#ifndef VIM_PEPPER_H_
#define VIM_PEPPER_H_

/*
 * Just like printf, but output goes to the JS console.
 */
int js_printf(const char* format, ...);

#endif // VIM_PEPPER_H_
