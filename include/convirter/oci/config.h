#ifndef CVIRT_OCI_CONFIG_H
#define CVIRT_OCI_CONFIG_H

struct cvirt_oci_config;
struct cvirt_oci_layer;

struct cvirt_oci_config *cvirt_oci_config_new();

int cvirt_oci_config_add_layer(struct cvirt_oci_config *config, struct cvirt_oci_layer *layer);

int cvirt_oci_config_close(struct cvirt_oci_config *config);

void cvirt_oci_config_destroy(struct cvirt_oci_config *config);

#endif
