
typedef struct node {
	void* data;	
	struct node* next;
} node;

typedef struct {
	node* head;
	node* tail;
	int size;
} llist;

llist* llist_create() {
	llist* list = malloc(sizeof(llist));
	list->head = NULL;
	list->tail = NULL;
	list->size = 0;
	return list;
}

void llist_addBack(llist* list, void* data) {
	node* n = malloc(sizeof(node));
	n->data = data;

	if (list->size == 0) {
		list->head = n;
		list->tail = n;
		n->next = NULL;
	} else {
		list->tail->next = n;
		list->tail = n;
	}
	list->size++;
}

void llist_addFront(llist* list, void* data) {
	node* n = malloc(sizeof(node));
	n->data = data;

	if (list->size == 0) {
		list->head = n;
		list->tail = n;
		n->next = NULL;
	} else {
		list->head->next = n;
		list->head = n;
	}
	list->size++;
}

void* llist_remove(llist* list, int pos) {
	if (pos > list->size -1) {
		printf("out of bounds pos %d\n", pos);
		return NULL;
	}
	node* curr = list->head;
	node* prev = NULL;
	for (int i=0; i<pos; i++) {
		prev = curr;
		curr = curr->next;	
	}

	if (list->size == 1) {
		list->head = NULL;
		list->tail = NULL;
	} else {
		if (curr == list->head) {
			list->head = curr->next;
		} else if (curr == list->tail) {
			list->tail = prev;
		} else {
			prev->next = curr->next;
		}
	}

	void* data = curr->data;
	free(curr);
	list->size--;
	return data;
}

int llist_indexOf(llist* list, void* data) {
	node* curr = list->head;
	for (int i=0; i<list->size; i++) {
		if(curr->data == data)
			return i;
		curr = curr->next;
	}
	return -1;
}

void* llist_get(llist* list, int pos) {
	node* curr = list->head;
	for (int i=0; i<list->size; i++) {
		if(i == pos)
			return curr->data;
		curr = curr->next;
	}
	return NULL;
}
