#!/bin/bash

   Copyright 2014 Google Inc.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

export NACL_SDK_ROOT="$NACL_SDK_ROOT"
export TOOLCHAIN="pnacl"
export NACL_ARCH="pnacl"
export NACL_DEBUG=1

export NACL_EXEEXT=".pexe"

export NACL_SEL_LDR=${NACL_SDK_ROOT}/tools/sel_ldr.py
export PNACL_TRANSLATE="${NACL_SDK_ROOT}/toolchain/linux_pnacl/bin/pnacl-translate -arch x86-64"
export PNACL_FINALIZE=${NACL_SDK_ROOT}/toolchain/linux_pnacl/bin/pnacl-finalize

export CC=${NACL_SDK_ROOT}/toolchain/linux_pnacl/bin/pnacl-clang
export CXX=${NACL_SDK_ROOT}/toolchain/linux_pnacl/bin/pnacl-clang++
export AR=${NACL_SDK_ROOT}/toolchain/linux_pnacl/bin/pnacl-ar
export RANLIB=${NACL_SDK_ROOT}/toolchain/linux_pnacl/bin/pnacl-ranlib
export PKG_CONFIG_PATH=/pkgconfig
export PKG_CONFIG_LIBDIR=
export PATH=${PATH}:${NACL_SDK_ROOT}/toolchain/linux_pnacl/bin
export CPPFLAGS="-I${NACL_SDK_ROOT}/include"
export LDFLAGS="-L${NACL_SDK_ROOT}/lib/pnacl/Release \
  -L${NACL_SDK_ROOT}/toolchain/linux_pnacl/usr/local/lib \
  -Wl,-rpath-link=${NACL_SDK_ROOT}/toolchain/linux_pnacl/usr/local/lib"

export NACLSTRIP=${NACL_SDK_ROOT}/toolchain/linux_pnacl/bin/pnacl-strip

export CFLAGS="-O2"
export CXXFLAGS="-O2"

export CONF_ARGS="--host=${NACL_ARCH} --with-tlib=ncurses --prefix=/usr \
  --exec-prefix=/usr --with-http=no --with-html=no --with-ftp=no \
  --disable-mmx --disable-sse --disable-sse2 --disable-asm --with-x=no \
  CC=${CC}" # src/auto/configure will ignore previously defined CC

# From naclports/src/build_tools/common.sh
export NACL_CLI_MAIN_LIB="-Wl,--undefined=PSUserCreateInstance -lcli_main"
export NACL_CPP_LIB="c++"

export EXTRA_LIBS="${NACL_CLI_MAIN_LIB} -ltar -lppapi_simple -lnacl_io \
  -lppapi -lppapi_cpp -l${NACL_CPP_LIB}"

export TEST_LIBS="-lgtest -DGTEST_DONT_DEFINE_FAIL=1" # gtest's FAIL macro clashes with vim's

# Configuration
export vim_cv_toupper_broken=1
export vim_cv_terminfo=yes
export vim_cv_tty_mode=1
export vim_cv_tty_group=1
export vim_cv_getcwd_broken=yes
export vim_cv_stat_ignores_slash=yes
export vim_cv_memmove_handles_overlap=yes
if [ "${NACL_DEBUG}" == "1" ]; then
  export STRIP=echo
else
  export STRIP=${NACLSTRIP}
fi

PUBLISHDIR="publish"
export DESTDIR="$PUBLISHDIR/vim"

make "$@" 

# TODO(zpotter): Move this into src/Makefile...
# If make left a binary behind, pnacl-finalize it
if [ -e "src/${DESTDIR}/usr/bin/vim" ]; then
  echo "Packaging ${NACL_EXEEXT}"
  # Set up filesystem tar
  cd src/${PUBLISHDIR}
  cp vim/usr/bin/vim vim_${NACL_ARCH}${NACL_EXEEXT}
  tar cf vim.tar vim/usr/
  rm -rf vim/
  
  $(${PNACL_FINALIZE} vim_${NACL_ARCH}${NACL_EXEEXT} -o vim_${NACL_ARCH}.final${NACL_EXEEXT})
fi

