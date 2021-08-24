#!/bin/sh
rm -rf build
podman run -v .:/root -w /root alpine:3.14 ./build-alpine.sh
