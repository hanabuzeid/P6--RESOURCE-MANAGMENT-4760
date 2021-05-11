#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>

typedef struct Node {
	int QueIndex;
	struct Node *nextNode;
} QueNode;

typedef struct {
	QueNode *head;
	QueNode *tail;
	int length;
} Que;

Que *createQueue();
QueNode *queueNode(int);
void enqueue(Que*, int);
QueNode *dequeue(Que*);
void removeFromQueue(Que*, int);
bool isQueueEmpty(Que*);
int sizeOfQueue(Que*);

#endif