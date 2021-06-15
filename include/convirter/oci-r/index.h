#ifndef CVIRT_OCI_R_INDEX_H
#define CVIRT_OCI_R_INDEX_H

struct cvirt_oci_r_index;

struct cvirt_oci_r_index *cvirt_oci_r_index_from_archive(const char *path);

// TODO: Nested index
// struct cvirt_oci_r_index *cvirt_oci_r_index_from_archive_blob(const char *path, const char *hash);

const char *cvirt_oci_r_index_get_native_manifest_digest(
	struct cvirt_oci_r_index *index);

void cvirt_oci_r_index_destroy(struct cvirt_oci_r_index *index);

#endif
