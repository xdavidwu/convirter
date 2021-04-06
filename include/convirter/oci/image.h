#ifndef CVIRT_OCI_IMAGE_H
#define CVIRT_OCI_IMAGE_H

struct cvirt_oci_image;
struct cvirt_oci_blob;

struct cvirt_oci_image *cvirt_oci_image_new(const char *path);

int cvirt_oci_image_add_blob(struct cvirt_oci_image *image, struct cvirt_oci_blob *blob);

int cvirt_oci_image_add_manifest(struct cvirt_oci_image *image, struct cvirt_oci_blob *blob);

int cvirt_oci_image_close(struct cvirt_oci_image *image);

void cvirt_oci_image_destroy(struct cvirt_oci_image *image);

#endif
