#!/bin/sh
rm -rf build
mkdir build
podman run -v .:/build -w /build alpine:3.14 ./build-alpine.sh
