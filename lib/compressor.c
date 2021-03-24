#include "compressor.h"

#include <archive.h>
#include <archive_entry.h>
#include <stdio.h>
#include <stdlib.h>

int compress(int fd_in, int fd_out) {
	struct archive *compressor = archive_write_new();
	if (!compressor) {
		return -1;
	}
	int res = archive_write_set_format_raw(compressor);
	if (res < 0) {
		archive_write_free(compressor);
		return res;
	}
	res = archive_write_add_filter_zstd(compressor);
	if (res < 0) {
		archive_write_free(compressor);
		return res;
	}
	res = archive_write_open_fd(compressor, fd_out);
	if (res < 0) {
		archive_write_free(compressor);
		return res;
	}

	// dummy entry required by raw writer
	struct archive_entry *entry = archive_entry_new();
	if (!entry) {
		archive_write_free(compressor);
		return -1;
	}
	archive_entry_set_filetype(entry, AE_IFREG);
	archive_write_header(compressor, entry);
	archive_entry_free(entry);

	char *buf = calloc(1 * 1024 * 1024, sizeof(char));
	if (!buf) {
		archive_write_free(compressor);
		return -1;
	}

	while ((res = read(fd_in, buf, 1 * 1024 * 1024))) {
		if (res == -1) {
			archive_write_free(compressor);
			return -1;
		}
		res = archive_write_data(compressor, buf, res);
		if (res < 0) {
			fprintf(stderr, "archive_write_data: %s\n", archive_error_string(compressor));
			archive_write_free(compressor);
			free(buf);
			return res;
		}
	}

	archive_write_close(compressor);
	archive_write_free(compressor);
	free(buf);
	return 0;
}
