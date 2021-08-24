#!/bin/sh

set -e

VERSION=29
SHA256=0b80eea7aa184ac6fd20cafa2a1fdf290ffecc70869a797079e2cc5c6225a52a

cd build
apk add gcc musl-dev autoconf automake libtool make xz-dev zlib-static zlib-dev zstd-static zstd-dev
wget https://kernel.org/pub/linux/utils/kernel/kmod/kmod-$VERSION.tar.xz
echo "$SHA256  kmod-$VERSION.tar.xz" | sha256sum -c -
tar -xf kmod-$VERSION.tar.xz
cd kmod-$VERSION
./configure --with-zstd --with-xz --with-zlib --disable-manpages
sed -i 's|tools_kmod_LDADD = |tools_kmod_LDADD = -all-static |' Makefile
make tools/kmod
