#!/bin/bash

export NACL_SDK_ROOT="$NACL_SDK_ROOT"
export TOOLCHAIN="pnacl"
export NACL_ARCH="pnacl"
export NACL_DEBUG=1

export NACL_EXEEXT=".pexe"

export CC="$NACL_SDK_ROOT"/toolchain/linux_pnacl/bin/pnacl-clang
export CXX="$NACL_SDK_ROOT"/toolchain/linux_pnacl/bin/pnacl-clang++
export AR="$NACL_SDK_ROOT"/toolchain/linux_pnacl/bin/pnacl-ar
export RANLIB="$NACL_SDK_ROOT"/toolchain/linux_pnacl/bin/pnacl-ranlib
export PKG_CONFIG_PATH=/pkgconfig
export PKG_CONFIG_LIBDIR=
export PATH=${PATH}:"$NACL_SDK_ROOT"/toolchain/linux_pnacl/bin
export CPPFLAGS="-I$NACL_SDK_ROOT/include"
export LDFLAGS="-L$NACL_SDK_ROOT/lib/pnacl/Release -L$NACL_SDK_ROOT/toolchain/linux_pnacl/usr/local/lib -Wl,-rpath-link=$NACL_SDK_ROOT/toolchain/linux_pnacl/usr/local/lib -O2"
export NACLSTRIP="$NACL_SDK_ROOT"/toolchain/linux_pnacl/bin/pnacl-strip

export CFLAGS="-O2"
export CXXFLAGS="-O2"

EXTRA_CONFIGURE_ARGS="--host=${NACL_ARCH} --with-tlib=ncurses --prefix=/usr --exec-prefix=/usr --with-http=no --with-html=no --with-ftp=no --disable-mmx --disable-sse --disable-sse2 --disable-asm --with-x=no"
# From naclports/src/build_tools/common.sh
export NACL_CLI_MAIN_LIB="-Wl,--undefined=PSUserCreateInstance -lcli_main"
export NACL_CPP_LIB="c++"
export EXTRA_LIBS="${NACL_CLI_MAIN_LIB} -ltar -lppapi_simple -lnacl_io \
   -lppapi -lppapi_cpp -l${NACL_CPP_LIB}"

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

echo "CPPFLAGS=$CPPFLAGS"
echo "CFLAGS=$CFLAGS"
echo "CXXFLAGS=$CXXFLAGS"
echo "LDFLAGS=$LDFLAGS"

cd src && ./configure ${EXTRA_CONFIGURE_ARGS} 
make depend
cd ..

# Compile to vim73/src/publish/vim/
# make
make install 

# Set up filesystem tar
cd src/${PUBLISHDIR}
cp vim/usr/bin/vim vim_${NACL_ARCH}${NACL_EXEEXT}
tar cf vim.tar vim/usr/
rm -rf vim/

$(${NACL_SDK_ROOT}/toolchain/linux_pnacl/bin/pnacl-finalize vim_${NACL_ARCH}${NACL_EXEEXT} -o vim_${NACL_ARCH}.final${NACL_EXEEXT})
