#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "oss.h"
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>

static char* prog;
static char* logfile = "output.txt";

static int shmID = -1, semID = -1;
static struct oss *ossptr = NULL;

static struct ossReport report; 
static struct timeval clockTick;

static int aliveFlag = 1;

static void ossatexit(){
	printf("The program will exit\n");
	exit(EXIT_SUCCESS);
}
static void initReport(){
	report.pStart.curr = 0;
	report.pStart.max = maxProcesses;
	report.pRun.curr = 0;
        report.pRun.max = processSize;
	report.pDone.curr = 0;
        report.pDone.max = maxProcesses;

	report.immediateAccept = 0;
	report.waitAccept = 0;
	report.deadlockRun = 0;
	report.successTerm = 0;
	//wait for prof confimation for the output

}
static void sigHandler(){
	printf("Signal is called\n");
	exit(1);

}
static void initDesc(struct descriptor sys[descriptorCount]){
	int i;
	int shared = descriptorCount * sharedDesc / 100;
	
	//generate random ID description
	while(shared > 0){
		const int descID = rand() % descriptorCount;
		if(sys[descID].shareable == 0){
			sys[descID].shareable = 1;
			--shared;
		}

	}
	
	for(i = 0; i < descriptorCount; i++){
		if(sys[i].shareable == 0)
			sys[i].max = 1 + (rand() % descriptorMaxins);
		else
			sys[i].max = 1;
		sys[i].val = sys[i].max;
	}
}
static int ossSemWait(){
  	struct sembuf sop = {.sem_num = 0, .sem_flg = 0, .sem_op = -1};

  	sigset_t mask, oldmask;
  	sigemptyset(&mask);
  	sigaddset(&mask, SIGCHLD);
  	sigprocmask(SIG_BLOCK, &mask, &oldmask);

  	if (semop(semID, &sop, 1) == -1){
    		fprintf(stderr,"%s: ",prog);
		perror("semop");
    		sigprocmask(SIG_SETMASK, &oldmask, NULL);
    		exit(EXIT_FAILURE);
  	}

  	sigprocmask(SIG_SETMASK, &oldmask, NULL);

  	return 0;
}
static int ossSemPost(){
  	struct sembuf sop = {.sem_num = 0, .sem_flg = 0, .sem_op = 1};

  	sigset_t mask, oldmask;
  	sigemptyset(&mask);
  	sigaddset(&mask, SIGCHLD);
  	sigprocmask(SIG_BLOCK, &mask, &oldmask);

  	if (semop(semID, &sop, 1) == -1){
    		fprintf(stderr,"%s: ",prog);
		perror("semop");
    		exit(EXIT_FAILURE);
  	}	

  	sigprocmask(SIG_SETMASK, &oldmask, NULL);

  	return 0;
}
static int ossTimeUpdate(struct timeval *update)
{
  	struct timeval tv;

  	if(ossSemWait() < 0){
    		return -1;
  	}	

  	timeradd(&ossptr->time, update, &tv);
  	ossptr->time = tv;

  	return ossSemPost();
}
static int ossAttach(){
	const unsigned short unlocked = 1;
	shmID = shmget(key_shm, sizeof(struct oss), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
	if(shmID < 0){
                fprintf(stderr,"%s: failed to get id for shared memory. ",prog);
                perror("Error");
                return -1;
        }

	semID = semget(key_sem, 1, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
	if(semID < 0){
		fprintf(stderr,"%s: failed to get id for semaphore. ",prog);
                perror("Error");
                return -1;
	}
	ossptr = (struct oss *)shmat(shmID, NULL, 0);
	if(ossptr == (void *)-1){
                fprintf(stderr,"%s: failed to get pointer for shared memory. ",prog);
                perror("Error");
                return -1;
	}
	
	//clear the memory
	bzero(ossptr, sizeof(struct oss));

	//unlock semaphore
	if (semctl(semID, 0, SETVAL, unlocked) == -1){
    		fprintf(stderr,"%s: ",prog);
                perror("semctl");
                return -1;
  	}

  	return 0;
}
int main(const int argc, char *const argv[]){
	prog = argv[0];
	struct timeval next_start = {.tv_sec = 1, .tv_usec = 500000};

	atexit(ossatexit);
	stdout = freopen(logfile,"w",stdout);

	initReport();
	signal(SIGALRM, sigHandler);	
	if(ossAttach() < 0)
		ossatexit();	

	clockTick.tv_sec = 0;
  	clockTick.tv_usec = 100;

	alarm(maxRuntime);

	initDesc(ossptr->desc);
	

	while(!aliveFlag){
		if(ossTimeUpdate(&clockTick) < 0)
      			break;
		if((timercmp(&ossptr->time, &next_start, >=) != 0) && (report.pStart.curr < report.pStart.max)){
			//ossProcess();

			next_start.tv_sec = ossptr->time.tv_sec;
        		next_start.tv_usec = ossptr->time.tv_usec + ((rand() % 499000) + 1000);

		}
		
		
	}	
	






	return EXIT_SUCCESS;

}
