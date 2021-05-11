#ifndef SHARED_H
#define SHARED_H

#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>

#define sys _system

#define BUFFER_LENGTH 1024

#define KEY_PATHNAME "."
#define KEY_ID_SYSTEM 0
#define KEY_ID_MESSAGE_QUEUE 1
#define KEY_ID_SEMAPHORE 2
#define PERMS (S_IRUSR | S_IWUSR)

#define PATH_LOG "output.log"
#define TIMEOUT 5
#define PROCESSES_MAX 18
#define PROCESSES_TOTAL 40 //MAXIMUM 
#define RESOURCES_MAX 20
#define SHARED_RESOURCES_MIN (int) (RESOURCES_MAX * 0.15)
#define SHARED_RESOURCES_MAX (int) (RESOURCES_MAX * 0.25)

typedef struct {
	unsigned int seconds;
	unsigned int nano_seconds;
} 
Time;

typedef struct {
	long typeOfMessage;
	pid_t process_id;
	int sp_id;
	bool terminate;
	bool req;
	bool rel;
	bool isSafe;
	char text[BUFFER_LENGTH];
} Message;

typedef struct {
	int resource[RESOURCES_MAX];
	int shared;
} ResourceDescriptor;

typedef struct {
	pid_t process_id;
	int sp_id;
	int maximum[RESOURCES_MAX];
	int allocation[RESOURCES_MAX];
	int req[RESOURCES_MAX];
	int rel[RESOURCES_MAX];
} PCB;

typedef struct {
	Time clock;
	PCB ptable[PROCESSES_MAX];
} System;

#endif