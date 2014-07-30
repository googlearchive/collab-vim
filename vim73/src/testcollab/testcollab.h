// TODO(zpotter): Add legal headers

#ifndef VIM_TESTCOLLAB_TESTCOLLAB_H_
#define VIM_TESTCOLLAB_TESTCOLLAB_H_

extern "C" {
#include "vim.h"
}

// Takes a string literal and copies it into freshly malloc'ed memory.
// Meant for setting strings that will later be freed.
// Returns the new heap string as a char_u*.
char_u * malloc_literal(const std::string& kStr);

#endif // VIM_TESTCOLLAB_TESTCOLLAB_MAIN_H_
