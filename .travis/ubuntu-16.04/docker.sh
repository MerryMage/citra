#!/bin/bash -ex

cd /citra

apt-get update
apt-get install -y software-properties-common
add-apt-repository ppa:ubuntu-toolchain-r/test
add-apt-repository ppa:beineri/opt-qt593-xenial
apt-get update
apt-get install -y build-essential libsdl2-dev qt59base qt593d qt59tools libcurl4-openssl-dev libssl-dev wget git gcc-7 g++-7

# Get a recent version of CMake
wget https://cmake.org/files/v3.10/cmake-3.10.1-Linux-x86_64.sh
echo y | sh cmake-3.10.1-Linux-x86_64.sh --prefix=cmake
export PATH=/citra/cmake/cmake-3.10.1-Linux-x86_64/bin:$PATH

export CC=gcc-7
export CXX=g++-7
export QTDIR=/opt/qt59
export PATH=/opt/qt59/bin:$PATH
export LD_LIBRARY_PATH=/opt/qt59/lib/x86_64-linux-gnu:/opt/qt59/lib
export PKG_CONFIG_PATH=/opt/qt59/lib/pkgconfig

mkdir build && cd build
cmake .. -DUSE_SYSTEM_CURL=ON -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_TRANSLATION=ON -DCITRA_ENABLE_COMPATIBILITY_REPORTING=${ENABLE_COMPATIBILITY_REPORTING:-"OFF"}
make -j4

ctest -VV -C Release
