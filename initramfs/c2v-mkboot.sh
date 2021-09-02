#!/bin/sh

set -e

DATA_PATH=%CONVIRTER_DATA%/initramfs
OUTPUT=c2v.cpio
COPY_KERNEL=kernel
DO_COPY_KERNEL=y
IMAGE_OUTPUT=c2v.sqsh
KERNEL_VERSION=

USAGE="Usage: $0: [OPTIONS]...

  -d DATA         Use DATA as data path instead of $DATA_PATH
  -o OUTPUT       Output initramfs to OUTPUT.gz instead of $OUTPUT.gz
  -s IMAGE_OUTPUT Output SquashFS image to IMAGE_OUTPUT instead of $IMAGE_OUTPUT
  -k VERSION      Use kernel VERSION instead of auto detecting
  -b KERNEL       Copy kernel image to KERNEL instead of $COPY_KERNEL 
  -n              Do not copy kernel, conflicts with -b
  -h              Show this help and quit
"

while getopts d:o:s:k:b:nh NAME; do
	case $NAME in
	d)
		DATA_PATH="$OPTARG"
		;;
	o)
		OUTPUT="$OPTARG"
		;;
	s)
		IMAGE_OUTPUT="$OPTARG"
		;;
	k)
		KERNEL_VERSION="$OPTARG"
		;;
	b)
		COPY_KERNEL="$OPTARG"
		;;
	n)
		DO_COPY_KERNEL=n
		;;
	h)
		echo "$USAGE"
		exit
		;;
	?)
		echo "$USAGE"
		exit 1
		;;
	esac
done

[ -z "$KERNEL_VERSION" ] && KERNEL_VERSION="$(ls -1 /lib/modules | head -n 1)"

[ -z "$KERNEL_VERSION" ] && echo "Failed to detect any installed kernels" && exit 1

[ ! -d "/lib/modules/$KERNEL_VERSION" ] && echo "Failed to find modules for $KERNEL_VERSION" >&2 && exit 1

[ "$DO_COPY_KERNEL" = y -a ! -f "/lib/modules/$KERNEL_VERSION/vmlinuz" ] && echo "Failed to find kernel image for $KERNEL_VERSION" >&2 && exit 1

cpio -oLHnewc -D"$DATA_PATH" -O"$OUTPUT" -R0:0 <"$DATA_PATH"/files.list
(echo lib/ && echo lib/modules && cd / && find "lib/modules/$KERNEL_VERSION") | cpio -oLAHnewc -D/ -O"$OUTPUT"
gzip -f "$OUTPUT"
rm -f "$IMAGE_OUTPUT"
mksquashfs "/lib/modules/$KERNEL_VERSION" "$IMAGE_OUTPUT"
[ "$DO_COPY_KERNEL" = y ] && cp "/lib/modules/$KERNEL_VERSION/vmlinuz" "$COPY_KERNEL"
