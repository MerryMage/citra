#!/bin/bash -ex

set -o pipefail

export PATH="/usr/local/opt/llvm/bin:$PATH"
export CPPFLAGS="-I/usr/local/opt/llvm/include $CPPFLAGS"
export LDFLAGS="-L/usr/local/opt/llvm/lib -Wl,-rpath,/usr/local/opt/llvm/lib $LDFLAGS"
export CC="clang"
export CXX="clang++"

export MACOSX_DEPLOYMENT_TARGET=10.9
export Qt5_DIR=$(brew --prefix)/opt/qt5

$(CXX) --version

mkdir build && cd build
cmake .. -DUSE_SYSTEM_CURL=ON -DCMAKE_OSX_ARCHITECTURES="x86_64;x86_64h" -DCMAKE_BUILD_TYPE=Release
make -j4

ctest -VV -C Release
