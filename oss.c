#include <errno.h>
#include <libgen.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "shared.h"
#include "queue.h"

#define log _log
// SYSTEM SIMULATION
void initializeSystem();
void initializeDescriptor();
void startSimulation();
void controlProcesses();
void tryingSpawnProcess();
void spawnProcess(int);
void initializePCB(pid_t, int);
int findAvailablePID();
void advanceClock();
bool isSafe(Que*, int);

// USAGE FUNCTION
void intialize(int, char**);
void usage(int);
void registrationOfSignalHandlers();
void signalHandler(int);
void timer(int);
void initIPC();
void freeIPC();

// HANDLING FUNCTION
void error(char*, ...);
void crash(char*);
void log(char*, ...);
void semLock(const int);
void semUnlock(const int);
void printDescriptor();
void vectorPrint(char*, int[RESOURCES_MAX]);
void matrixPrint(char*, Que*, int[][RESOURCES_MAX], int);
void printSummary();

static char *programName;
static volatile bool quit = false;

// ipc var
static int shm_id = -1;
static int msq_id = -1;
static int sem_id = -1;
static System *sys = NULL;
static Message msg;

// system simulation 
static bool verbose = false;
static Que *queue;
static Time nextSpawn;
static ResourceDescriptor descriptor;
static int active_count = 0;
static int spawn_count = 0;
static int exit_count = 0;
//static int count_mem_acc = 0;
static pid_t p_ids[PROCESSES_MAX];

int main(int argc, char **argv) {
	intialize(argc, argv);

	srand(time(NULL) ^ getpid());

	bool ok = true;

	while (true) {
		int c = getopt(argc, argv, "hv");
		if (c == -1) break;
		switch (c) {
			case 'h':
				usage(EXIT_SUCCESS);
			case 'v':
				verbose = true;
				break;
			default:
				ok = false;
		}
	}

	if (optind < argc) {
		char buf[BUFFER_LENGTH];
		snprintf(buf, BUFFER_LENGTH, "NO ALTERNATIVE FOUND: ");
		while (optind < argc) {
			strncat(buf, argv[optind++], BUFFER_LENGTH);
			if (optind < argc) strncat(buf, ", ", BUFFER_LENGTH);
		}
		error(buf);
		ok = false;
	}
	
	if (!ok) usage(EXIT_FAILURE);
	registrationOfSignalHandlers();

	//close log file
	FILE *file;
	if ((file = fopen(PATH_LOG, "w")) == NULL) crash("fopen");
	if (fclose(file) == EOF) crash("fclose");

	initIPC();
	memset(p_ids, 0, sizeof(p_ids));
	sys->clock.seconds = 0;
	sys->clock.nano_seconds = 0;
	nextSpawn.seconds = 0;
	nextSpawn.nano_seconds = 0;
	initializeSystem();
	queue = createQueue();
	initializeDescriptor();
	printDescriptor();

	startSimulation();

	printSummary();
	freeIPC();

	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

void initializeSystem() {
	int i = 0;

	while (i  < PROCESSES_MAX) {
		sys->ptable[i].sp_id = -1;
		sys->ptable[i].process_id = -1;
		i++;
	}
}

void initializeDescriptor() {
	int i = 0;

	while (i < RESOURCES_MAX)
	{
		descriptor.resource[i] = rand() % 10 + 1;
		i++;
	}

	descriptor.shared = (SHARED_RESOURCES_MAX == 0) ? 0 : rand() % (SHARED_RESOURCES_MAX - (SHARED_RESOURCES_MAX - SHARED_RESOURCES_MIN)) + SHARED_RESOURCES_MIN;
}

void startSimulation() {
	while (true) {
		tryingSpawnProcess();
		advanceClock();
		controlProcesses();
		advanceClock();

		int status;
		pid_t process_id = waitpid(-1, &status, WNOHANG);
		if (process_id > 0) {
			int sp_id = WEXITSTATUS(status);
			p_ids[sp_id] = 0;
			active_count--;
			exit_count++;
		}

		if (quit) {
			if (exit_count == spawn_count) break;
		} else {
			if (exit_count == PROCESSES_TOTAL) break;
		}
	}
}

void controlProcesses() {
	int i, length = 0;
	QueNode *nextNode = queue->head;
	
	while (nextNode != NULL) {
		advanceClock();
		int sp_id = nextNode->QueIndex;
		msg.typeOfMessage = sys->ptable[sp_id].process_id;
		msg.sp_id = sp_id;
		msg.process_id = sys->ptable[sp_id].process_id;
		msgsnd(msq_id, &msg, sizeof(Message), 0);
		msgrcv(msq_id, &msg, sizeof(Message), 1, 0);

		advanceClock();

		if (msg.terminate) {
			log("%s [%d.%d] p%d IS TERMINATING \n", basename(programName), sys->clock.seconds, sys->clock.nano_seconds, msg.sp_id);

			removeFromQueue(queue, sp_id);
			nextNode = queue->head;
			i = 0 ;
			while (i  < length){
				nextNode = (nextNode->nextNode != NULL) ? nextNode->nextNode : NULL;
				i++;
			}
			continue;
		}

		if (msg.req) {
			log("%s [%d.%d] p%d IS REQUESTING \n", basename(programName), sys->clock.seconds, sys->clock.nano_seconds, msg.sp_id);

			msg.typeOfMessage = sys->ptable[sp_id].process_id;
			msg.isSafe = isSafe(queue, sp_id);
			msgsnd(msq_id, &msg, sizeof(Message), 0);
		}

		advanceClock();

		if (msg.rel) log("%s [%d.%d] p%d IS RELEASING \n", basename(programName), sys->clock.seconds, sys->clock.nano_seconds, msg.sp_id);
		
		length++;
		nextNode = (nextNode->nextNode != NULL) ? nextNode->nextNode : NULL;
	}
}

void tryingSpawnProcess() {
	if (active_count >= PROCESSES_MAX) return;
	if (spawn_count >= PROCESSES_TOTAL) return;
	if (nextSpawn.nano_seconds < (rand() % (500 + 1)) * 1000000) return;
	if (quit) return;
	nextSpawn.nano_seconds = 0;
	int sp_id = findAvailablePID();
	if (sp_id == -1) return;
	spawnProcess(sp_id);
}

void spawnProcess(int sp_id) {
	pid_t process_id = fork();
	p_ids[sp_id] = process_id;

	if (process_id == -1) crash("fork");
	else if (process_id == 0) {
		char arg[BUFFER_LENGTH];
		snprintf(arg, BUFFER_LENGTH, "%d", sp_id);
		execl("./user", "user", arg, (char*) NULL);
		crash("execl");
	}

	initializePCB(process_id, sp_id);
	enqueue(queue, sp_id);
	active_count++;
	spawn_count++;

	log("%s [%d.%d] p%d IS CREATING\n", basename(programName), sys->clock.seconds, sys->clock.nano_seconds, sp_id);
}

void initializePCB(pid_t process_id, int sp_id) {
	int i = 0 ;
	PCB *pcb = &sys->ptable[sp_id];
	pcb->process_id = process_id;
	pcb->sp_id = sp_id;

	while (i < RESOURCES_MAX) {
		pcb->maximum[i] = rand() % (descriptor.resource[i] + 1);
		pcb->allocation[i] = 0;
		pcb->req[i] = 0;
		pcb->rel[i] = 0;
		i++;
	}
}

int findAvailablePID() {
	int i =0;
	while (i < PROCESSES_MAX)
	{
		if (p_ids[i] == 0) return i;
		i++;
	}
	return -1;
}

void advanceClock() {
	semLock(0);
	int r = rand() % (1 * 1000000) + 1;
	nextSpawn.nano_seconds += r;
	sys->clock.nano_seconds += r;
	while (sys->clock.nano_seconds >= (1000 * 1000000)) {
		sys->clock.seconds++;
		sys->clock.nano_seconds -= (1000 * 1000000);
	}

	semUnlock(0);
}

bool isSafe(Que *queue, int QueIndex) {
	int i, j, k, p;

	QueNode *nextNode = queue->head;
	if (nextNode == NULL) return true;

	int length = sizeOfQueue(queue);
	int max[length][RESOURCES_MAX];
	int alloc[length][RESOURCES_MAX];
	int req[RESOURCES_MAX];
	int my_need[length][RESOURCES_MAX];
	int avail[RESOURCES_MAX];
	i =0;

	while (i < length) {
		p = nextNode->QueIndex;
		j = 0;

		while (j < RESOURCES_MAX) {
			max[i][j] = sys->ptable[p].maximum[j];
			alloc[i][j] = sys->ptable[p].allocation[j];
			j++;
		}
		nextNode = (nextNode->nextNode != NULL) ? nextNode->nextNode : NULL;
		i++;
	}

	for (i = 0; i < length; i++)
		for (j = 0; j < RESOURCES_MAX; j++)
			my_need[i][j] = max[i][j] - alloc[i][j];

	i = 0;
	while (i < RESOURCES_MAX) {
		avail[i] = descriptor.resource[i];
		req[i] = sys->ptable[QueIndex].req[i];
		i++;
	}

	for (i = 0; i < length; i++)
		for (j = 0; j < RESOURCES_MAX; j++)
			avail[j] = avail[j] - alloc[i][j];

	nextNode = queue->head;
	while (nextNode != NULL) {
		if (nextNode->QueIndex == QueIndex) break;
		i++;
		nextNode = (nextNode->nextNode != NULL) ? nextNode->nextNode : NULL;
	}

	if (verbose) {
		matrixPrint("MAXIMUM", queue, max, length);
		matrixPrint("ALLOCATION ..", queue, alloc, length);
		char buf[BUFFER_LENGTH];
		sprintf(buf, "REQUEST.. p%-2d", QueIndex);
		vectorPrint(buf, req);
	}

	bool fin[length];
	int seq[length];
	memset(fin, 0, length * sizeof(fin[0]));

	int work[RESOURCES_MAX];
	i = 0;
	while (i < RESOURCES_MAX){
		work[i] = avail[i];
		i++;
	}

	j = 0;
	while (j  < RESOURCES_MAX) {
		if (my_need[i][j] < req[j] && j < descriptor.shared) {
			log("\t ASKED FOR MORE INITIAL MAXIMUM REQUEST... \n");

			if (verbose) {
				vectorPrint("AVAILABLE", avail);
				matrixPrint("NEED", queue, my_need, length);
			}

			return false;
		}

		if (req[j] <= avail[j] && j < descriptor.shared) {
			avail[j] -= req[j];
			alloc[i][j] += req[j];
			my_need[i][j] -= req[j];
		} else {
			log("\t NOT ENOUGH AVAILABLE RESOURCES ...\n");

			if (verbose) {
				vectorPrint("AVAILABLE", avail);
				matrixPrint("NEED", queue, my_need, length);
			}

			return false;
		}
		j++;
	}

	//using the bankers algorithms as in lecture
	i = 0;
	while (QueIndex < length) {
		bool found = false;
		for (p = 0; p < length; p++) {
			if (fin[p] == 0) {
				for (j = 0; j < RESOURCES_MAX; j++)
					if (my_need[p][j] > work[j] && descriptor.shared) break;

				if (j == RESOURCES_MAX) {
					for (k = 0; k < RESOURCES_MAX; k++)
						work[k] += alloc[p][k];

					seq[i++] = p;
					fin[p] = 1;
					found = true;
				}
			}
		}

		if (found == false) {
			log("SYSTEM IS IN UNSAFE MODE\n");
			return false;
		}
	}

	if (verbose) {
		vectorPrint("AVAILABLE", avail);
		matrixPrint("NEED", queue, my_need, length);
	}

	i = 0;
	int temp[length];
	nextNode = queue->head;
	while (nextNode != NULL) {
		temp[i++] = nextNode->QueIndex;
		nextNode = (nextNode->nextNode != NULL) ? nextNode->nextNode : NULL;
	}

	log("SYSTEM IS IN SAFE MODE, SAFE SEQUENCE IS : ");
	i = 0;

	while (i < length){
		log("%2d ", temp[seq[i]]);
		i++;
	}
	log("\n\n");

	return true;
}

void intialize(int argc, char **argv) {
	programName = argv[0];

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
}

void usage(int status) {
	if (status != EXIT_SUCCESS) fprintf(stderr, "Try '%s -h' for more INFO\n", programName);
	else {
		printf("Usage: %s [-v]\n", programName);
		printf("   -v : verbose on\n");
	}
	exit(status);
}

void registrationOfSignalHandlers() {
	struct sigaction sa;

	if (sigemptyset(&sa.sa_mask) == -1) crash("sigemptyset");
	sa.sa_handler = &signalHandler;
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGINT, &sa, NULL) == -1) crash("sigaction");
	if (sigemptyset(&sa.sa_mask) == -1) crash("sigemptyset");
	sa.sa_handler = &signalHandler;
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGALRM, &sa, NULL) == -1) crash("sigaction");
	timer(TIMEOUT);
	signal(SIGSEGV, signalHandler);
}

void signalHandler(int sig) {
	if (sig == SIGALRM) quit = true;
	else {
		printSummary();
		int i = 0;

		while (i < PROCESSES_MAX)
		{
			if (p_ids[i] > 0) kill(p_ids[i], SIGTERM);
			i++;
		}
		while (wait(NULL) > 0);

		freeIPC();
		exit(EXIT_SUCCESS);
	}
}

void timer(int duration) {
	struct itimerval val;
	val.it_value.tv_sec = duration;
	val.it_value.tv_usec = 0;
	val.it_interval.tv_sec = 0;
	val.it_interval.tv_usec = 0;
	if (setitimer(ITIMER_REAL, &val, NULL) == -1) crash("setitimer");
}

void initIPC() {
	key_t key;

	if ((key = ftok(KEY_PATHNAME, KEY_ID_SYSTEM)) == -1) crash("ftok");
	if ((shm_id = shmget(key, sizeof(System), IPC_EXCL | IPC_CREAT | PERMS)) == -1) crash("shmget");
	if ((sys = (System*) shmat(shm_id, NULL, 0)) == (void*) -1) crash("shmat");

	if ((key = ftok(KEY_PATHNAME, KEY_ID_MESSAGE_QUEUE)) == -1) crash("ftok");
	if ((msq_id = msgget(key, IPC_EXCL | IPC_CREAT | PERMS)) == -1) crash("msgget");

	if ((key = ftok(KEY_PATHNAME, KEY_ID_SEMAPHORE)) == -1) crash("ftok");
	if ((sem_id = semget(key, 1, IPC_EXCL | IPC_CREAT | PERMS)) == -1) crash("semget");
	if (semctl(sem_id, 0, SETVAL, 1) == -1) crash("semctl");
}

void freeIPC() {
	if (sys != NULL && shmdt(sys) == -1) crash("shmdt");
	if (shm_id > 0 && shmctl(shm_id, IPC_RMID, NULL) == -1) crash("shmdt");

	if (msq_id > 0 && msgctl(msq_id, IPC_RMID, NULL) == -1) crash("msgctl");

	if (sem_id > 0 && semctl(sem_id, 0, IPC_RMID) == -1) crash("semctl");
}

void error(char *fmt, ...) {
	char buf[BUFFER_LENGTH];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, BUFFER_LENGTH, fmt, args);
	va_end(args);
	
	fprintf(stderr, "%s: %s\n", programName, buf);
	
	freeIPC();
}

void crash(char *msg) {
	char buf[BUFFER_LENGTH];
	snprintf(buf, BUFFER_LENGTH, "%s: %s", programName, msg);
	perror(buf);
	
	freeIPC();
	
	exit(EXIT_FAILURE);
}

void log(char *fmt, ...) {
	FILE *file = fopen(PATH_LOG, "a+");
	if(file == NULL) crash("fopen");

	char buf[BUFFER_LENGTH];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, BUFFER_LENGTH, fmt, args);
	va_end(args);

	fprintf(stderr, buf);
	fprintf(file, buf);

	if (fclose(file) == EOF) crash("fclose");
}

void semLock(const int QueIndex) {
	struct sembuf sop = { QueIndex, -1, 0 };
	if (semop(sem_id, &sop, 1) == -1) crash("semop");
}

void semUnlock(const int QueIndex) {
	struct sembuf sop = { QueIndex, 1, 0 };
	if (semop(sem_id, &sop, 1) == -1) crash("semop");
}

void printDescriptor() {
	vectorPrint("TOTAL", descriptor.resource);
	log("AVAILABLE RESOURCES: %d\n", descriptor.shared);
}

void vectorPrint(char *title, int vector[RESOURCES_MAX]) {
	log("%s\n    ", title);

	int i = 0;

	while (i < RESOURCES_MAX) {
		log("%-2d", vector[i]);
		if (i < RESOURCES_MAX - 1) log(" ");
		i++;
	}
	log("\n");
}

void matrixPrint(char *title, Que *queue, int matrix[][RESOURCES_MAX], int length) {
	QueNode *nextNode;
	nextNode = queue->head;

	int i, j;
	log("%s\n", title);

	for (i = 0; i < length; i++) {
		log("p%-2d ", nextNode->QueIndex);
		for (j = 0; j < RESOURCES_MAX; j++) {
			log("%-2d", matrix[i][j]);
			if (j < RESOURCES_MAX - 1) log(" ");
		}
		log("\n");

		nextNode = (nextNode->nextNode != NULL) ? nextNode->nextNode : NULL;
	}
}

void printSummary() {
	//double mem_access_per_sec = (double) count_mem_acc / (double) sys->clock.seconds;

	log("\n <<< << STATISTICS >>  >>>\n");
	log(" ___________________________________________");
	log("\nTOTAL SYSTEM TIME  <<: %d.%d\n", sys->clock.seconds, sys->clock.nano_seconds);
	log("TOTAL PROCESS EXECUTED: %d\n", spawn_count);
	//log("Total memory access count: %d\n", count_mem_acc);
}