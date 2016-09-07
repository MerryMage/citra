#!/bin/sh

set -e
set -x

#if OS is linux or is not set
if [ "$TRAVIS_OS_NAME" = "linux" -o -z "$TRAVIS_OS_NAME" ]; then
    if [ -d "$HOME/build-cache" ]; then
        exit 0
    fi
    export CC=gcc-6
    export CXX=g++-6
    mkdir $HOME/build-cache

    curl -L http://www.cmake.org/files/v3.2/cmake-3.2.0-Linux-i386.tar.gz \
        | tar -xz -C $HOME/build-cache --strip-components=1

    (
        wget http://libsdl.org/release/SDL2-2.0.4.tar.gz -O - | tar xz
        cd SDL2-2.0.4
        ./configure --prefix=$HOME/build-cache
        make -j4 && make install
    )
elif [ "$TRAVIS_OS_NAME" = "osx" ]; then
    brew update > /dev/null # silence the very verbose output
    brew unlink cmake || true
    brew install cmake qt5 sdl2 dylibbundler
fi
