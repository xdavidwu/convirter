#!/bin/sh
set -e
(
	[ -n "$1" ] && cd "$1"
	rm -rf build
	podman run -v .:/root -w /root alpine:3.14 ./build-alpine.sh
)
[ -n "$2" ] && cp "$1/build/setuidgid" "$2"
