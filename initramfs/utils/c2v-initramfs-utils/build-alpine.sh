#!/bin/sh
apk add meson gcc musl-dev
meson build
ninja -C build
