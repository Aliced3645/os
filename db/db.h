#pragma once

#include <stdlib.h>

typedef struct Node {
	char *name;
	char *value;
	struct Node *lchild;
	struct Node *rchild;
} Node_t;

extern Node_t head;

void interpret_command(const char *, char *, size_t);
void cleanup_db();
