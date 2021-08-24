#!/bin/sh

set -e

VERSION=1.34.0
SHA256=ec8d1615edb045b83b81966604759c4d4ac921434ab4011da604f629c06074ce

cd build
apk add gcc musl-dev linux-headers make
wget https://busybox.net/downloads/busybox-$VERSION.tar.bz2
echo "$SHA256  busybox-$VERSION.tar.bz2" | sha256sum -c -
tar -xf busybox-$VERSION.tar.bz2
cp ../config busybox-$VERSION/.config
cd busybox-$VERSION
make busybox
