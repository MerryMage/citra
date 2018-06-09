#!/bin/bash -ex
mkdir -p "$HOME/.ccache"
docker run --rm --privileged multiarch/qemu-user-static:register --reset
docker run -e ENABLE_COMPATIBILITY_REPORTING -v $(pwd):/citra -v "$HOME/.ccache":/root/.ccache multiarch/debian-debootstrap:arm64-sid /bin/bash -ex /citra/.travis/aarch64/docker.sh
