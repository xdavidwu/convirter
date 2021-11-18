#include "common-config.h"
#include "xmem.h"

#include <errno.h>
#include <getopt.h>

static const int array_init_sz = 8;

int parse_common_exec_opt(struct common_exec_config *config, int opt) {
	switch (opt) {
	case 0:
		if (!config->cmd) {
			config->cmd = cvirt_xmalloc(array_init_sz * sizeof(const char *));
			config->cmd_sz = array_init_sz;
			config->cmd_len = 0;
		}
		if (config->cmd_len + 1 == config->cmd_sz) {
			config->cmd_sz *= 2;
			config->cmd = cvirt_xrealloc(config->cmd, config->cmd_sz * sizeof(const char *));
		}
		config->cmd[config->cmd_len + 1] = NULL;
		config->cmd[config->cmd_len++] = optarg;
		break;
	case 1:
		if (!config->entrypoint) {
			config->entrypoint = cvirt_xmalloc(array_init_sz * sizeof(const char *));
			config->entrypoint_sz = array_init_sz;
			config->entrypoint_len = 0;
		}
		if (config->entrypoint_len + 1 == config->entrypoint_sz) {
			config->entrypoint_sz *= 2;
			config->entrypoint = cvirt_xrealloc(config->entrypoint,
					config->entrypoint_sz * sizeof(const char *));
		}
		config->entrypoint[config->entrypoint_len + 1] = NULL;
		config->entrypoint[config->entrypoint_len++] = optarg;
		break;
	case 2:
		// TODO validate key=val
		if (!config->env) {
			config->env = cvirt_xmalloc(array_init_sz * sizeof(const char *));
			config->env_sz = array_init_sz;
			config->env_len = 0;
		}
		if (config->env_len + 1 == config->env_sz) {
			config->env_sz *= 2;
			config->env = cvirt_xrealloc(config->env, config->cmd_sz * sizeof(const char *));
		}
		config->env[config->env_len + 1] = NULL;
		config->env[config->env_len++] = optarg;
		break;
	case 3:
		// TODO validate USER[:GOURP] (number of colon?)
		config->user = optarg;
		break;
	case 4:
		config->workdir = optarg;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

