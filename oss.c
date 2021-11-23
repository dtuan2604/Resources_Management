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

static int qBlocked[processSize];
static int blockedLength = 0; //length of blocked queue

static void deallocateSHM(){
	if(ossptr != NULL){
		if(shmdt(ossptr) == -1){
			fprintf(stderr,"%s: failed to detach shared memory. ",prog);
                	perror("Error");
		}
	}
	if(shmID != -1){
		if(shmctl(shmID, IPC_RMID, NULL) == -1){
			fprintf(stderr,"%s: failed to delete shared memory. ",prog);
                        perror("Error");
		}
	}
	if(semID != -1){
		if(semctl(semID, 0, IPC_RMID) == -1){
			fprintf(stderr,"%s: failed to delete semaphore.",prog);
                        perror("Error");
		}	
	}
}	
static void initReport(){
	report.pStart.curr = 0;
	report.pStart.max = maxProcesses;
	report.pRun.curr = 0;
        report.pRun.max = processSize;
	report.pDone.curr = 0;
        report.pDone.max = maxProcesses;
	
	report.lineCount = 0;
	report.immediateAccept = 0;
	report.waitAccept = 0;
	report.deadlockRun = 0;
	report.successTerm = 0;
	//wait for prof confimation for the output

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
		if(sys[i].shareable == 1) //if it is shared resources, then populate it with instances
			sys[i].max = 1 + (rand() % descriptorMaxins);
		else
			sys[i].max = 1; //it is not shared resources, then it should have only one resource at a time
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
static int pidToIndex(const int pid){
	int i;
  	for (i = 0; i < processSize; i++){
    		if (ossptr->procs[i].pid == pid)
      			return i;
  	}
  	return -1;

}
static void waitChild(){
        pid_t pid;
        int status, idx;
        while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0){
                idx = pidToIndex(pid);
                if(idx == -1)
                        continue;
                printf("Master Child %i processDone code %d at time %lu:%i\n",
                        ossptr->procs[idx].id, WEXITSTATUS(status), ossptr->time.tv_sec, ossptr->time.tv_usec);
                report.lineCount++;

                bzero(&ossptr->procs[idx], sizeof(struct process));
                report.pRun.curr--;
                if(++report.pDone.curr >= report.pStart.max){
                        printf("Master encounters maximum processes. Stoping OSS now\n");
                        aliveFlag = 0;
                }
        }

}
static void sigHandler(const int sig){
        switch(sig){
                case SIGTERM:
                case SIGINT:
                        printf("Master received TERM/INT signal at time %lu:%06ld\n", ossptr->time.tv_sec, ossptr->time.tv_usec);
                        aliveFlag = 0;
                        break;
                case SIGALRM:
                        printf("Master received ALRM signal at time %lu:%06ld\n", ossptr->time.tv_sec, ossptr->time.tv_usec);
                        aliveFlag = 0;
                        break;
                case SIGCHLD:
                        waitChild();
                        break;
                default:
                        fprintf(stderr, "Error: Unknown signal received\n");
                        break;
        }

}
static void ossExit(){
        int i;
        if(ossptr){
                ossptr->time.tv_sec++;

                ossSemWait();
                for(i = 0; i < processSize; i++){
                        if ((ossptr->procs[i].request.state == rWAIT) ||
                                (ossptr->procs[i].request.state == rBLOCK))
                                ossptr->procs[i].request.state = rDENY;
                }
                ossSemPost();
                while (report.pRun.curr > 0)
                        waitChild();
        }
        printf("Master exit at time %lu:%06ld\n", ossptr->time.tv_sec, ossptr->time.tv_usec);
        deallocateSHM();
        exit(EXIT_SUCCESS);
}

static int emptyProc(struct process *procs){
	int i;
	for(i  = 0; i < processSize; i++){
		if(procs[i].pid == 0)
			return i;
	}
	return -1;
}
static int listDeadlocked(const int pdone[processSize]){
	int i, have_deadlocked = 0;
  	for (i = 0; i < processSize; i++){
    		if (!pdone[i]){
			have_deadlocked = 1;
			break;
		}
	}

	//REMINDER: implement verbose mode
	if(have_deadlocked > 0){
		printf("\tProcesses ");
    		for (i = 0; i < processSize; i++){
      			if (!pdone[i])
        			printf("P%d ", ossptr->procs[i].id);
    		}
		printf("deadlocked.\n");
		report.lineCount++;
	} 
	
	return have_deadlocked;

}
static int detectDeadlock(){
	int i, j, avail[descriptorCount], pdone[processSize];
	
	//array to mark if the process involved in deadlock state
	for (i = 0; i < processSize; i++){
    		pdone[i] = 0;
  	}
	
	//avaliable resources matrix
	for (i = 0; i < descriptorCount; i++){
    		avail[i] = ossptr->desc[i].val;
  	}
	
	i = 0;
	while( i != processSize){
		for(i = 0; i < processSize; i++){
			struct process *proc = &ossptr->procs[i];
      			if (proc->pid == 0){
       	 			pdone[i] = 1;
      			}

      			if (pdone[i] == 1){
        			continue;
      			}
			int can_complete = 1;
      			for (j = 0; j < descriptorCount; j++){
				if (ossptr->desc[j].val < (proc->desc[j].max - proc->desc[j].val)){
          				can_complete = 0; //can't complete
          				break;
        			}
			}
			if ((proc->request.val < 0) ||
          			(proc->request.state != rWAIT) ||
          			can_complete){
				pdone[i] = 1;

				for (j = 0; j < descriptorCount; j++){
          				avail[j] += proc->desc[j].val;
        			}
        			break;
			}
		}
	}
	
	return listDeadlocked(pdone);
	
}
static int blockProc(const int processID){
	if(blockedLength < processSize){
		qBlocked[blockedLength++] = processID;
		return 0;
	}
	return -1;
}
static int unblockProc(const int index){
	int i;
	if(index >= blockedLength)
		return -1;
	const int processID = qBlocked[index];
	blockedLength--;
	
	//shift the queue
	for (i = index; i < blockedLength; ++i){
    		qBlocked[i] = qBlocked[i + 1];
  	}
  	qBlocked[i] = -1; //clear last queue slot

  	return processID;
}
static int onReq(struct process *proc){
	//REMINDER: add number of request count
	
	if(proc->request.val < 0){
		//releasing resources
		proc->request.state = rACCEPT; //request is accepted
		
		//now returning resources to the system
		ossptr->desc[proc->request.id].val += -1 * proc->request.val;
    		proc->desc[proc->request.id].val += proc->request.val;

		 printf("Master has acknowledged Process P%u releasing R%d:%d at time %lu:%06ld\n",
           proc->id, proc->request.id, -1 * proc->request.val, ossptr->time.tv_sec, ossptr->time.tv_usec);
		report.lineCount++;
	}else if(proc->request.id == -1){
		//user want to return all resources
		int i;
		proc->request.state = rACCEPT;
		printf("Master has acknowledged Process P%u releasing all resources: ", proc->id);
		
		for(i = 0; i < descriptorCount; i++){
			if (proc->desc[i].val > 0){
        			printf("R%d:%d ", i, proc->desc[i].val);
				
				//now returning resources to the system
				ossptr->desc[i].val += proc->desc[i].val;
        			proc->desc[i].val = 0;
			}
		}
		
		printf("\n");
		report.lineCount++;
	}else if (ossptr->desc[proc->request.id].val >= proc->request.val){
		//user want to request resources
		printf("Master running deadlock detection at time %lu:%06ld\n", ossptr->time.tv_sec, ossptr->time.tv_usec);
		report.lineCount++;
		//REMINDER: increment the number of deadlock running
		
		//run deadlock algorithm
		if(detectDeadlock()){
			printf("Unsafe state after granting request; request not granted\n");
      			report.lineCount++;

      			proc->request.state = rDENY;
			//REMINDER: Implement blocked queue to push this process onto blocked queue
		}else{
			proc->request.state = rACCEPT;
			//REMINDER: increment the number of immediate granted request
			
			printf("\tSafe state after granting request\n"); 
    			printf("\tMaster granting P%d request R%d:%d at time %lu:%06ld\n", proc->id, proc->request.id, proc->request.val,
             		ossptr->time.tv_sec, ossptr->time.tv_usec);
			report.lineCount += 2;

			//update system
			ossptr->desc[proc->request.id].val -= proc->request.val;
		 	proc->desc[proc->request.id].val += proc->request.val;
			proc->desc[proc->request.id].max -= proc->request.val;
		}
	}else{
		//not enough system resources, user get blocked
		if(blockProc(proc->id) == 0){
			proc->request.state = rBLOCK;
      			printf("\tP%d added to wait queue, waiting on R%d=%d\n", proc->id, proc->request.id, proc->request.val);
		}else{
			proc->request.state = rDENY;
      			printf("\tP%d wans't added to wait queue, queue is full\n", proc->id);
		}
		report.lineCount++;
	}

	return 0;

}
static int execReq(){
	int i;
	sigset_t mask, oldmask;
	struct timeval tv;

	//block child signal
	sigemptyset(&mask);
  	sigaddset(&mask, SIGCHLD);
  	sigprocmask(SIG_BLOCK, &mask, &oldmask);

	//lock the shared region
	ossSemWait();
	for(i = 0; i < processSize; i++){
		struct process *proc = &ossptr->procs[i];
		if (proc->request.state == rWAIT)
      			onReq(proc); //if there is a request

	}
	ossSemPost();

	//re-allow the child signal
	sigprocmask(SIG_SETMASK, &oldmask, NULL);

	//update timer with dispatch time
	tv.tv_sec = 0;
  	tv.tv_usec = rand() % 100;
  	if (ossTimeUpdate(&tv) < 0)
    		return -1;

	//REMINDER: Might need to print out the dispatch time
	return 0;

}
static int idToIndex(const int id){
  	int i;
  	for (i = 0; i < processSize; i++){
    		if (ossptr->procs[i].id == id)
      			return i;
    	}
  	return -1;
}
static int unblock(const int b, const enum requestState newSt){
	const int id = unblockProc(b);
	const int idx = idToIndex(id);
	struct process *proc = &ossptr->procs[idx];

	ossSemWait();
	proc->processState = pREADY;
  	proc->request.state = newSt;
	
	if (proc->request.state == rDENY){
    		printf("Master Denied process with PID %d waiting on R%d=%d at time %lu:%06ld\n",
           		proc->id, proc->request.id, proc->request.val, ossptr->time.tv_sec, ossptr->time.tv_usec);
	}else{
		//REMINDER: increment the number of accepted request after waiting
		ossptr->desc[proc->request.id].val -= proc->request.val;
    		proc->desc[proc->request.id].val += proc->request.val;
    		proc->desc[proc->request.id].max -= proc->request.val;
		printf("Master Unblocked process with PID %d waiting on R%d=%d at time %lu:%06ld\n",
           		proc->id, proc->request.id, proc->request.val, ossptr->time.tv_sec, ossptr->time.tv_usec);
	}
	report.lineCount++;
	ossSemPost();
	return 0;
}
static void listAllocatedRes(){
	int i, j;
	printf("System available resources at time %li.%06ld\n", ossptr->time.tv_sec, ossptr->time.tv_usec);
  	report.lineCount++;
	printf("\t");
	for(i = 0; i < descriptorCount; i++){
		printf("R%d\t",i);
	}
	printf("\n");
	report.lineCount++;
	for(i = 0; i < processSize; i++){
		struct process *proc = &ossptr->procs[i];
		printf("P%d\t",proc->id);
		for(j = 0; j < descriptorCount; j++){
			printf("%d\t",proc->desc[j].val);
		}
		printf("\n");
		report.lineCount++;
	}
	fflush(stdout);
	return;
}
static int wakeupProc(){
	int i, n = 0;
  	for (i = 0; i < blockedLength; i++){
		const int id = qBlocked[i];
		const int idx = idToIndex(id);
		struct process *proc = &ossptr->procs[idx];

		if(ossptr->desc[proc->request.id].val >= proc->request.val){
			unblock(i, rACCEPT);
			n++;
		}
	}

	if(n == 0){
		if ((blockedLength > 0) && (blockedLength == report.pRun.curr)){
			unblock(0,rDENY); //unblock but deny to avoid deadlock
		}else{
			return 0; //nothing to unblock	
		}
	}

	return 1;
}
static int startProcess(){
	char argID[5];
	const int index = emptyProc(ossptr->procs);
	if(index == -1)
		return -1;
	struct process *proc = &ossptr->procs[index];
	snprintf(argID, 5, "%d", index);

	const pid_t pid = fork();
	if(pid == -1){
		fprintf(stderr,"%s: ",prog);
                perror("fork");
                exit(EXIT_FAILURE);
	}	
	
	if(pid == 0){
		execl("./user_proc","./user_proc",argID,NULL);
		fprintf(stderr,"%s: ",prog);
                perror("execl");
                return -1;

	}else{
		proc->pid = pid;
		proc->id = report.pStart.curr++;
		proc->processState = pREADY;

		report.pRun.curr++;
		printf("Master generating process with ID %u at time %lu:%06ld\n", proc->id, ossptr->time.tv_sec, ossptr->time.tv_usec);
		report.lineCount++;
	}
	return 0;
}
static void printReport(){
	listAllocatedRes();
	//REMINDER: implement log file	

}
int main(const int argc, char *const argv[]){
	prog = argv[0];
	struct timeval next_start = {.tv_sec = 1, .tv_usec = 500000};

	atexit(ossExit);
	stdout = freopen(logfile,"w",stdout);

	initReport();
	signal(SIGALRM, sigHandler);	
	if(ossAttach() < 0)
		ossExit();	

	signal(SIGINT, sigHandler);
  	signal(SIGTERM, sigHandler);
  	signal(SIGCHLD, sigHandler);
  	signal(SIGALRM, sigHandler);

	clockTick.tv_sec = 0;
  	clockTick.tv_usec = 100;

	alarm(maxRuntime);

	initDesc(ossptr->desc);
	

	while(!aliveFlag){
		if(ossTimeUpdate(&clockTick) < 0)
      			break;
		if((timercmp(&ossptr->time, &next_start, >=) != 0) && (report.pStart.curr < report.pStart.max)){
			startProcess();

			//update future timer to fork another process
			next_start.tv_sec = ossptr->time.tv_sec;
        		next_start.tv_usec = ossptr->time.tv_usec + ((rand() % 499000) + 1000);
		}
		
		execReq(); //execute request from child 
		
		wakeupProc();	
		//REMINDER: check line limit
		if(report.lineCount >= maxLine){
			raise(SIGINT);
		}			
	}	

	printReport();
	return EXIT_SUCCESS;

}
