#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"

Que *createQueue() {
	Que *queue = (Que*) malloc(sizeof(Que));
	queue->head = NULL;
	queue->tail = NULL;
	queue->length = 0;
	return queue;
}

QueNode *queueNode(int QueIndex) {
	QueNode *node = (QueNode*) malloc(sizeof(QueNode));
	node->QueIndex = QueIndex;
	node->nextNode = NULL;
	return node;
}

void enqueue(Que *queue, int QueIndex) {
	QueNode *node = queueNode(QueIndex);
	queue->length++;
	if (queue->tail == NULL) {
		queue->head = queue->tail = node;
		return;
	}
	queue->tail->nextNode = node;
	queue->tail = node;
}

QueNode *dequeue(Que *queue) {
	if (queue->head == NULL) return NULL;
	QueNode *temp = queue->head;
	free(temp);
	queue->head = queue->head->nextNode;
	if (queue->head == NULL) queue->tail = NULL;
	queue->length--;
	return temp;
}

void removeFromQueue(Que *queue, int QueIndex) {
	Que *temp = createQueue();
	QueNode *current = queue->head;
	while (current != NULL) {
		if (current->QueIndex != QueIndex) enqueue(temp, current->QueIndex);
		current = (current->nextNode != NULL) ? current->nextNode : NULL;
	}
	while (!isQueueEmpty(queue))
		dequeue(queue);
	while (!isQueueEmpty(temp)) {
		enqueue(queue, temp->head->QueIndex);
		dequeue(temp);
	}
	free(temp);
}

bool isQueueEmpty(Que *queue) {
	if (queue->tail == NULL) return true;
	return false;
}

int sizeOfQueue(Que *queue) {
	return queue->length;
}