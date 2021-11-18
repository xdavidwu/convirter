#ifndef CVIRT_OCI_CONFIG_H
#define CVIRT_OCI_CONFIG_H

struct cvirt_oci_config;
struct cvirt_oci_layer;

struct cvirt_oci_config *cvirt_oci_config_new();

int cvirt_oci_config_add_layer(struct cvirt_oci_config *config, struct cvirt_oci_layer *layer);

int cvirt_oci_config_set_user(struct cvirt_oci_config *config, const char *user);

int cvirt_oci_config_set_cmd(struct cvirt_oci_config *config, char *const cmd[]);

int cvirt_oci_config_set_entrypoint(struct cvirt_oci_config *config, char *const entrypoint[]);

int cvirt_oci_config_add_env(struct cvirt_oci_config *config, const char *env);

int cvirt_oci_config_set_stop_signal(struct cvirt_oci_config *config, const char *signal);

int cvirt_oci_config_close(struct cvirt_oci_config *config);

void cvirt_oci_config_destroy(struct cvirt_oci_config *config);

#endif
