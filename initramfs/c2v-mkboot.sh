#!/bin/sh

set -e

DATA_PATH=%INITRAMFS_DATA%
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

KERNEL_IMAGE=

find_vmlinuz() {
	for i in "/boot/vmlinuz-$KERNEL_VERSION" "/lib/modules/$KERNEL_VERSION/vmlinuz"; do
		[ -f "$i" ] && KERNEL_IMAGE="$i" && return
	done
	echo "Failed to find kernel image for $KERNEL_VERSION" >&2 && exit 1
}

module_files_for_initramfs() {
	echo lib/
	echo lib/modules
	echo "lib/modules/$KERNEL_VERSION"
	cd /
	# modules dep, alias ...etc
	find "lib/modules/$KERNEL_VERSION" -maxdepth 1 -name 'modules.*'
	# dkms
	[ -d "lib/modules/$KERNEL_VERSION/updates" ] && find "lib/modules/$KERNEL_VERSION/updates"
	echo "lib/modules/$KERNEL_VERSION/kernel"
	for sub in arch block crypto fs kernel lib; do
		[ -d "lib/modules/$KERNEL_VERSION/kernel/$sub" ] && find "lib/modules/$KERNEL_VERSION/kernel/$sub"
	done
	echo "lib/modules/$KERNEL_VERSION/kernel/drivers"
	for sub in $(find "/lib/modules/$KERNEL_VERSION/kernel/drivers" -maxdepth 1 -type d \
			-not -name 'net' -not -name 'video' -not -name 'phy' \
			-not -name 'media' -not -name 'nfc' -not -name 'infiniband' \
			-not -name 'leds' -not -name 'input' -not -name 'hwmon' \
			-not -name 'hid' -not -name 'gpu' -not -name 'gnss' \
			-not -name 'char' -not -name 'bluetooth' \
			-not -name 'accessibility' -not -name 'auxdisplay' \
			-printf '%f\n'); do
		[ -d "lib/modules/$KERNEL_VERSION/kernel/drivers/$sub" ] && find "lib/modules/$KERNEL_VERSION/kernel/drivers/$sub"
	done
	cd "$OLDPWD"
}

[ -z "$KERNEL_VERSION" ] && KERNEL_VERSION="$(ls -1 /lib/modules | head -n 1)"

[ -z "$KERNEL_VERSION" ] && echo "Failed to detect any installed kernels" && exit 1

[ ! -d "/lib/modules/$KERNEL_VERSION" ] && echo "Failed to find modules for $KERNEL_VERSION" >&2 && exit 1

[ "$DO_COPY_KERNEL" = y ] && find_vmlinuz

cpio -oLHnewc -D"$DATA_PATH" -O"$OUTPUT" -R0:0 <"$DATA_PATH"/files.list
module_files_for_initramfs | cpio -oLAHnewc -D/ -O"$OUTPUT"
gzip -f "$OUTPUT"
rm -f "$IMAGE_OUTPUT"
mksquashfs "/lib/modules/$KERNEL_VERSION" "$IMAGE_OUTPUT"
[ "$DO_COPY_KERNEL" = y ] && cp "$KERNEL_IMAGE" "$COPY_KERNEL"
