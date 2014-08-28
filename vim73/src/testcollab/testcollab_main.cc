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

#include "gtest/gtest.h"
#include "testcollab.h"

extern "C" {
#include "vim.h"
}

// Takes a string literal and copies it into freshly malloc'ed memory.
// Meant for setting strings that will later be freed.
// Returns the new heap string as a char_u*.
char_u* malloc_literal(const std::string& kStr) {
  // Assert that sizeof(char_u) is 1 for memcpy's sake.
  static_assert((sizeof(char_u) == 1), "sizeof char_u should be 1 for memcpy.");
  size_t num_bytes = (kStr.length() + 1); // Add 1 for the null byte
  char_u *str = static_cast<char_u *>(malloc(num_bytes));
  if (str == NULL) return NULL;
  memcpy(str, kStr.c_str(), num_bytes);
  return str;
}

int main(int argc, char* argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
