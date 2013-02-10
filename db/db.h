#pragma once

#include <stdlib.h>
#include <pthread.h>

typedef struct Node {
	char *name;
	char *value;
	struct Node *lchild;
	struct Node *rchild;
        //fine grained lock
        pthread_rwlock_t lock;
} Node_t;

//LockNode forms a chain
extern Node_t head;
extern pthread_rwlock_t coarseDBLock;

void interpret_command(const char *, char *, size_t);
void cleanup_db();
