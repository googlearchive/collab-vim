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
