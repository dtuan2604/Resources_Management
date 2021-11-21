#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "oss.h"
#include <time.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/time.h>

static char* prog;
//define the memory and semaphore identifiers
static int shmID = -1, semID = -1;

static struct oss *ossptr = NULL; 
static struct process *user = NULL;


//initialize the maximum resources claims for each process
static void generateDesc(struct descriptor proc[descriptorCount], struct descriptor sys[descriptorCount]){
	int i;
	for(i = 0; i < descriptorCount; i++){
		if(sys[i].max <= 1){
			proc[i].max = sys[i].max;
		}else{
			proc[i].max = 1 + (rand() % sys[i].max);
		}
	}

}

//block critical section
static int ossSemWait(){
	struct sembuf sop = {.sem_num = 0, .sem_flg = 0, .sem_op = -1};

	if (semop(semID, &sop, 1) == -1){
		fprintf(stderr,"%s: ",prog);
		perror("semop");
		return -1;
	}
	return 0;
}
//unlock critical section
static int ossSemPost()
{
	struct sembuf sop = {.sem_num = 0, .sem_flg = 0, .sem_op = 1};

	if (semop(semID, &sop, 1) == -1){
		fprintf(stderr,"%s: ",prog);
                perror("semop");
		return -1;
	}
	return 0;
}

static int ossAttach(){
	shmID = shmget(key_shm, sizeof(struct oss), 0);
	if(shmID < 0){
                fprintf(stderr,"%s: failed to get id for shared memory. ",prog);
                perror("Error");
                return -1;
        }

	semID = semget(key_sem, 0, 0);
	if (semID < 0){
		fprintf(stderr,"%s: failed to get id for semaphore. ",prog);
                perror("Error");
                return -1;
	}
	ossptr = (struct oss *)shmat(shmID, NULL, 0);
	if (ossptr == (void *)-1){
                fprintf(stderr,"%s: failed to get pointer for shared memory. ",prog);
                perror("Error");
                return -1;
	}

	return 0;

}


int main(const int argc, char *const argv[]){
	prog = argv[0];
	if (argc != 2){
		fprintf(stderr, "%s: Missing arguments\n",prog);
		return EXIT_FAILURE;
	}
	if(ossAttach() < 0){
		fprintf(stderr, "%s: Failed to attach\n",prog);
                return EXIT_FAILURE;
	}

	user = &ossptr->procs[atoi(argv[1])];

	srand(time(NULL));

	generateDesc(user->desc, ossptr->desc);	
	
	struct timeval tstep, tcheck;
	tstep.tv_sec = 0;
	tstep.tv_usec = termCheckPeriod;

			
	ossSemWait();
	timeradd(&tstep, &ossptr->time, &tcheck);
	ossSemPost();

	return EXIT_SUCCESS;
}
