#!/bin/sh
rm -rf build
mkdir build
podman run --privileged -v .:/build -w /build alpine:3.14 ./build-alpine.sh
