#include <archive.h>
#include <archive_entry.h>
#include <errno.h>
#include <json_tokener.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct archive *archive_from_file_and_seek(const char *path, const char *name,
		struct archive_entry **entry) {
	struct archive *res = archive_read_new();
	if (!res) {
		goto err;
	}
	archive_read_support_format_tar(res);
	int ret;
	ret = archive_read_open_filename(res, path, 4096);
	if (ret != ARCHIVE_OK) {
		goto err;
	}
	struct archive_entry *mentry;
	while (archive_read_next_header(res, &mentry) == ARCHIVE_OK) {
		if (!strcmp(archive_entry_pathname(mentry), name)) {
			if (entry) {
				*entry = mentry;
			}
			return res;
		}
	}
err:
	archive_read_free(res);
	return NULL;
}

struct json_object *json_from_archive(const char *path, const char *name) {
	struct json_object *res = NULL;
	struct archive_entry *entry = NULL;
	char *json_str = NULL;
	struct archive *archive = archive_from_file_and_seek(path, name, &entry);
	if (!archive) {
		goto out;
	}
	int sz = archive_entry_size(entry);
	json_str = calloc(sz + 1, sizeof(char));
	if (!json_str) {
		goto out;
	}
	int read = archive_read_data(archive, json_str, sz);
	if (read != sz) {
		goto out;
	}
	res = json_tokener_parse(json_str);
out:
	free(json_str);
	archive_read_free(archive);
	return res;
}

char *digest_to_name(const char *digest) {
	char *res = calloc(6 + strlen(digest) + 1, sizeof(char));
	if (!res) {
		return NULL;
	}
	strcat(res, "blobs/");
	strcat(res, digest);
	bool found = false;
	for (int i = 6; res[i]; i++) {
		if (res[i] == ':') {
			res[i] = '/';
			found = true;
			break;
		}
	}
	if (!found) {
		free(res);
		return NULL;
	}
	return res;
}
