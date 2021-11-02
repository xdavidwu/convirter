#define _DEFAULT_SOURCE //getgrouplist()

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int is_numeric(const char *str) {
	while (*str) {
		if (*str < '0' || *str > '9') {
			return 0;
		}
		str++;
	}
	return 1;
}

int main(int argc, char *argv[]) {
	chdir(argv[1]);
	char *user = argv[2];
	char *group = strchr(user, ':');
	if (group) {
		*group = '\0';
		group++;
	}

	uid_t uid;
	struct passwd *passwd = NULL;
	if (is_numeric(user)) {
		uid = atol(user);
	} else {
		errno = 0;
		passwd = getpwnam(user);
		if (!passwd) {
			if (!errno) {
				fprintf(stderr, "User %s not found\n", user);
				return ENOENT;
			}
			perror("getpwnam");
			return errno;
		}
		uid = passwd->pw_uid;
	}

	gid_t gid;
	int ngroups = 1;
	gid_t supp_groups[NGROUPS_MAX];
	if (!group) {
		// explicit group absent
		if (passwd) {
			gid = passwd->pw_gid;
		} else {
			passwd = getpwuid(uid);
			// contianer tools seems to leave gid as 0 if not found
			if (!passwd) {
				gid = 0;
			} else {
				gid = passwd->pw_gid;
			}
		}
		// set supplementary groups as specified in image-spec
		if (passwd) {
			ngroups = NGROUPS_MAX;
			getgrouplist(passwd->pw_name, gid, supp_groups, &ngroups);
		}
	} else if (is_numeric(group)) {
		gid = atol(group);
	} else {
		errno = 0;
		struct group *grp = getgrnam(group);
		if (!grp) {
			if (!errno) {
				fprintf(stderr, "Group %s not found\n", group);
				return ENOENT;
			}
			perror("getgrnam");
			return errno;
		}
		gid = grp->gr_gid;
	}
	if (ngroups == 1) {
		setgroups(1, &gid);
	} else {
		setgroups(ngroups, supp_groups);
	}
	setgid(gid);
	setuid(uid);
	execvp(argv[3], &argv[3]);
	perror("execvp");
	return errno;
}
