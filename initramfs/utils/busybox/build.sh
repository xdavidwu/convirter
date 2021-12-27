#!/bin/sh
set -e
(
	[ -n "$1" ] && cd "$1"
	rm -rf build
	mkdir build
	podman run -v .:/build -w /build alpine:3.14 ./build-alpine.sh
)
[ -n "$2" ] && cp "$1/build/busybox-1.34.0/busybox" "$2"
