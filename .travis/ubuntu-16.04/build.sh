#!/bin/bash -ex

docker run -e ENABLE_COMPATIBILITY_REPORTING -v $(pwd):/citra ubuntu:16.04 /bin/bash -ex /citra/.travis/ubuntu-16.04/docker.sh
