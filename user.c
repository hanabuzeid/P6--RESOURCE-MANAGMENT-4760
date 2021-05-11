#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/time.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "shared.h"

void intialize(int, char**);
void initIPC();
void crash(char*);

static char *programName;

// ipc variables
static int shm_id = -1;
static int msq_id = -1;
static System *sys = NULL;
static Message msg;

int main(int argc, char **argv) {
	intialize(argc, argv);

	int sp_id = atoi(argv[1]);

	srand(time(NULL) ^ getpid());

	initIPC();

	bool started = false;
	bool requesting = false;
	bool acquired = false;
	Time arrival;
	Time duration;
	arrival.seconds = sys->clock.seconds;
	arrival.nano_seconds = sys->clock.nano_seconds;
	bool old = false;
	int i;

	//WHILE_LOOP 
	while (true) {
		msgrcv(msq_id, &msg, sizeof(Message), getpid(), 0);

		if (!old) {
			duration.seconds = sys->clock.seconds;
			duration.nano_seconds = sys->clock.nano_seconds;
			if (abs(duration.nano_seconds - arrival.nano_seconds) >= 1000 * 1000000) old = true;
			else if (abs(duration.seconds - arrival.seconds) >= 1) old = true;
		}

		bool terminating = false;
		bool releasing = false;
		int choice;

		if (!started || !old) choice = rand() % 2 + 0;
		else choice = rand() % 3 + 0;

		switch (choice) {
			case 0:
				started = true;
				if (!requesting) {
					i = 0;
		
					while (i < RESOURCES_MAX){
						sys->ptable[sp_id].req[i] = rand() % (sys->ptable[sp_id].maximum[i] - sys->ptable[sp_id].allocation[i] + 1);
						i++;
					}
					requesting = true;
				}
				break;
			case 1:
				if (acquired) {
					i = 0;
			
					while (i < RESOURCES_MAX)
					{
						sys->ptable[sp_id].rel[i] = sys->ptable[sp_id].allocation[i];
						i++;
					}
					terminating = true;
				}
				break;
			case 2:
				terminating = true;
				break;
		}

		msg.typeOfMessage = 1;
		msg.terminate = terminating;
		msg.req = requesting;
		msg.rel = releasing;
		msgsnd(msq_id, &msg, sizeof(Message), 0);

		if (terminating) break;
		else {
			if (requesting) {
				msgrcv(msq_id, &msg, sizeof(Message), getpid(), 0);

				if (msg.isSafe) {
					i = 0 ;
				
					while (i < RESOURCES_MAX) {
						sys->ptable[sp_id].allocation[i] += sys->ptable[sp_id].req[i];
						sys->ptable[sp_id].req[i] = 0;
						i++;
					}

					requesting = false;
					acquired = true;
				}
			}

			if (releasing) {
				i = 0;
			
				while (i < RESOURCES_MAX) {
					sys->ptable[sp_id].allocation[i] -= sys->ptable[sp_id].rel[i];
					sys->ptable[sp_id].rel[i] = 0;
					i++;
				}
				acquired = false;
			}
		}
	}
	
	return sp_id;
}

void intialize(int argc, char **argv) {
	programName = argv[0];
	//prgName ="Master";
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
}
//IPC
void initIPC() {
	key_t key;

	if ((key = ftok(KEY_PATHNAME, KEY_ID_SYSTEM)) == -1) crash("ftok");
	if ((shm_id = shmget(key, sizeof(System), 0)) == -1) crash("shmget");
	if ((sys = (System*) shmat(shm_id, NULL, 0)) == (void*) -1) crash("shmat");

	if ((key = ftok(KEY_PATHNAME, KEY_ID_MESSAGE_QUEUE)) == -1) crash("ftok");
	if ((msq_id = msgget(key, 0)) == -1) crash("msgget");
}
// MESSAGES CRASH ERRORS
void crash(char *msg) {
	char buf[BUFFER_LENGTH];
	snprintf(buf, BUFFER_LENGTH, "%s %s", programName, msg);
	perror(buf);
	
	exit(EXIT_FAILURE);
}