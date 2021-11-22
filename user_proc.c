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

	return 0;

}
static int releaseRes(const struct descriptor proc[descriptorCount]){
	int i, len = 0, list[descriptorCount];

	for(i = 0; i < descriptorCount; i++){
		//save the desc id if user currently hold it
		if(proc[i].val > 0)
			list[len++] = i;
	}

	//return a random resources id to be released
	return (len == 0) ? -1 : list[rand() % len];
}
static int requestRes(const struct descriptor proc[descriptorCount]){
	int i, len = 0, list[descriptorCount];

	for(i = 0; i < descriptorCount; i++){
		//save the desc id if user want to request this
		if(proc[i].max > 0)
			list[len++] = i;

	} 
	return (len == 0) ? -1 : list[rand() % len];
}
static int waitReq(struct descriptorRequest *req){
	while((req->state == rWAIT) || (req->state == rBLOCK)){
		//this function will wait until the request is executed
		if(ossSemPost() < 0){
			return -1;
		}
		
		usleep(sleeptime);

		if(ossSemWait() < 0){
			return -1;
		}

	}
	if(req->state == rDENY)
		return -1; 
	return 0;

}
static int userProcess(){
	int descID;
	static int max_prob = 100;

	int action = ((rand() % max_prob) < B) ? 0 : 1;

	switch(action){
		case 0: 
			//releasing
			descID = releaseRes(user->desc);
			if(descID == -1){
				//Currently dont have any resources to release
				//Ask for resources
				action = 1;
				descID = requestRes(user->desc);
				if(descID == -1)
					return -1;
		 
			}
			break;
		case 1:
			//requesting
			descID = requestRes(user->desc);
			if(descID == -1){
				//max_prob = B 
				action = 0;
				descID = releaseRes(user->desc);
				if(descID == -1)
					return -1;	
			}	
			break; 
	}

	user->request.id = descID;
	user->request.val = (action == 0) ? -1 * user->desc[descID].val : user->desc[descID].max;
	user->request.state = rWAIT;
	
	return waitReq(&user->request);

}
int main(const int argc, char *const argv[]){
	prog = argv[0];
	if(argc != 2){
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
	tstep.tv_usec = rand() % termCheckPeriod;

			
	ossSemWait();
	timeradd(&tstep, &ossptr->time, &tcheck);
	ossSemPost();

	int stop = 0;
	while(!stop){
		if(ossSemWait() < 0){
			break;
		}
		
		if(timercmp(&ossptr->time, &tcheck, >)){
			//if it is time to terminate
			stop = ossptr->terminateFlag;
			tstep.tv_usec = rand() % termCheckPeriod;
			timeradd(&tstep, &ossptr->time, &tcheck);
		}else{
			stop = userProcess();
		}
		if(ossSemPost < 0){
			break;
		}
	}

	// now releasing all
	ossSemWait();
	user->request.id = -1;
	user->request.val = 0;
	user->request.state = rWAIT;
	waitReq(&user->request);
	ossSemPost();	

	if(shmdt(ossptr) == -1){
        	fprintf(stderr,"%s: failed to detach shared memory. ",prog);
       	        perror("Error");
        }

	return EXIT_SUCCESS;
}
