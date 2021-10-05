#ifndef LIST_H
#define LIST_H

struct cvirt_list {
	struct cvirt_list *next;
	struct cvirt_list *prev;
	void *data;
};

struct cvirt_list *cvirt_list_new();
struct cvirt_list *cvirt_list_append(struct cvirt_list *list, void *data);
void cvirt_list_remove(struct cvirt_list *list, struct cvirt_list *item);
void cvirt_list_destroy(struct cvirt_list *list);

#endif
