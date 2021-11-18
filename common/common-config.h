#ifndef _COMMON_CONFIG
#define _COMMON_CONFIG

#include <stddef.h>

#define COMMON_EXEC_CONFIG_LONG_OPTIONS(start_val) \
	{"cmd",		required_argument,	NULL,	(start_val)}, \
	{"entrypoint",	required_argument,	NULL,	(start_val) + 1}, \
	{"env",		required_argument,	NULL,	(start_val) + 2}, \
	{"user",	required_argument,	NULL,	(start_val) + 3}, \
	{"working-dir",	required_argument,	NULL,	(start_val) + 4}

#define COMMON_EXEC_CONFIG_OPTIONS_HELP "\
      --cmd=CMD                Extra args to entrypoint, set once for each arg\n\
      --entrypoint=ENTRYPOINT  Entrypoint, set once for each arguments\n\
      --env=KEY=VAL            Environment variables, set once for each var\n\
      --user=USER[:GROUP]      User and group, name or numeric id\n\
      --working-dir=WORKDIR    Working direcotry\n"

struct common_exec_config {
	const char **cmd, **entrypoint, **env;
	size_t cmd_len, entrypoint_len, env_len;
	size_t cmd_sz, entrypoint_sz, env_sz;
	const char *user, *workdir;
};

int parse_common_exec_opt(struct common_exec_config *config, int opt);

#endif
