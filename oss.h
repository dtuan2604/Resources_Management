#include <sys/time.h>
#include "config.h"

#define sleeptime 250

const key_t key_shm = 4506;
const key_t key_sem = 6708;

//define process state
enum states { pREADY=1, pBLOCK, pTERM};

//request states
enum requestState { rACCEPT=0, rBLOCK, rWAIT, rDENY};

struct descriptor{
	int shareable; //if the descriptor is shareable 
	int max; //maximum instances of the resources
	int val; //number of instances left
};
struct pDescriptor{
	int curr;
	int max;
};
struct descriptorRequest{
	int id;
	int val;
	enum requestState state;
};

struct process{
	int pid, id; //processID and OSS ID
	
	enum states processState;
	
	struct descriptorRequest request; //indicate the current process request
	struct descriptor desc[descriptorCount]; //process resource descriptors 

};

struct oss{
	struct timeval time;
	struct process procs[processSize];

	struct descriptor desc[descriptorCount]; 
	int terminateFlag;

};
struct ossReport{
	struct pDescriptor pStart, pRun, pDone;
	unsigned int immediateAccept;
	unsigned int waitAccept;
	unsigned int deadlockRun;
	unsigned int successTerm;
};

