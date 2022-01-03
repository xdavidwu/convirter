# convirter

Deals with containers and VMs.

Containers are in OCI Image archive format. (Transport oci-archive: in podman and skopeo)

Depends on libguestfs, libarchive, json-c and libgcrypt.

Build like a typical meson project, for example:

```sh
meson build/
ninja -C build/
sudo ninja -C build install
```

There are also tools that won't be automatically installed in `tools/`.

## c2v

Converts container to VM (rootfs) image, needs to be paired kernel, initramfs and modules squashfs from c2v-mkboot.

c2v-mkboot uses kernel from your system.

Example:

```sh
c2v-mkboot
```

Kernel, initramfs and modules squashfs will be outputted to current working directory.

```sh
c2v <oci archive> <VM rootfs image>
```

Ouputted VM rootfs image will be in QCOW2 disk format. Note that your libguestfs installation needs btrfs support for c2v to work. (Install btrfs tools on your system if missing.)

Layer structure will be preserved as btrfs snapshots.

Run the VM with:

```sh
qemu-system-x86_64 -enable-kvm -kernel kernel -initrd c2v.cpio.gz -hda c2v.sqsh -hdb <rootfs image> -nographic -m 256M -no-reboot -append 'console=ttyS0 panic=-1'
```

`kernel`, `c2v.cpio.gz` and `c2v.sqsh` are from c2v-mkboot output.

## v2c

Converts VM to container image.

```
v2c <VM image> <oci archive>
```

You can run it like `podman run -it oci-archive:<oci archive>`, for example.

### Optional layer reuse

Find out best base container image for a VM by:

```
v2c-findcontainer -d <data path> <VM image>
```

You can download required data at https://github.com/xdavidwu/convirter-data

Known containers and their estimated reused bytes are printed.

And then download the container image using skopeo:

```
skopeo copy docker://<image name> oci-archive:<output>
```

Pass to v2c to make it reuse layers from it:

```
v2c --layer-reuse=<archive from above> <VM image> <oci archive>
```

## License

MIT.

Bundled binaries in 'initramfs/' are under their original licenses:

* busybox: GPLv2, source code available at https://busybox.net/downloads/busybox-1.34.0.tar.bz2
* kmod: GPLv2, source code available at https://kernel.org/pub/linux/utils/kernel/kmod/kmod-29.tar.xz
