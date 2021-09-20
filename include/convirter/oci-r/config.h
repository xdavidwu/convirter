#ifndef CVIRT_OCI_R_CONFIG_H
#define CVIRT_OCI_R_CONFIG_H

struct cvirt_oci_r_config;

struct cvirt_oci_r_config *cvirt_oci_r_config_from_archive_blob(int fd,
	const char *digest);

int cvirt_oci_r_config_get_cmd_length(struct cvirt_oci_r_config *config);

const char *cvirt_oci_r_config_get_cmd_part(
	struct cvirt_oci_r_config *config, int index);

int cvirt_oci_r_config_get_entrypoint_length(struct cvirt_oci_r_config *config);

const char *cvirt_oci_r_config_get_entrypoint_part(
	struct cvirt_oci_r_config *config, int index);

int cvirt_oci_r_config_get_env_length(struct cvirt_oci_r_config *config);

const char *cvirt_oci_r_config_get_env(
	struct cvirt_oci_r_config *config, int index);

const char *cvirt_oci_r_config_get_user(struct cvirt_oci_r_config *config);

const char *cvirt_oci_r_config_get_working_dir(struct cvirt_oci_r_config *config);

void cvirt_oci_r_config_destroy(struct cvirt_oci_r_config *config);

#endif
