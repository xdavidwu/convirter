#ifndef OCI_R_LAYER_H
#define OCI_R_LAYER_H

#define BUFSZ	(4 * 1024)

struct cvirt_oci_r_layer {
	struct archive *image_archive;
	struct archive *layer_archive;
	char buf[BUFSZ];
};

#endif
