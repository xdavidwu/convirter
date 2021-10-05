#include "list.h"
#include "xmem.h"

#include <stdlib.h>

struct cvirt_list *cvirt_list_new() {
	struct cvirt_list *list = cvirt_xcalloc(1, sizeof(struct cvirt_list));
	list->prev = list;
	return list;
}

struct cvirt_list *cvirt_list_append(struct cvirt_list *list, void *data) {
	struct cvirt_list *n_list = cvirt_list_new();
	n_list->data = data;
	n_list->prev = list->prev;
	list->prev->next = n_list;
	list->prev = n_list;
	return n_list;
}

void cvirt_list_remove(struct cvirt_list *list, struct cvirt_list *item) {
	item->prev->next = item->next;
	if (item->next) {
		item->next->prev = item->prev;
	} else {
		list->prev = item->prev;
	}
	free(item);
}

void cvirt_list_destroy(struct cvirt_list *list) {
	if (list == NULL) {
		return;
	}
	struct cvirt_list *ptr = list->prev, *optr;
	while (ptr != list) {
		optr = ptr;
		ptr = ptr->prev;
		free(optr);
	}
	free(list);
}
