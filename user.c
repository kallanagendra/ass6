#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <strings.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/types.h>

#include "shared.h"

static struct shared_block *shb = NULL;  //shared block
static struct proc_block *pb = NULL;	//process block
static unsigned int rsn = 0;	//request semaphore number

//Take a random resource
static enum status mem_request(){

	WAIT(rsn);

		pb->req.type 	 = ((rand() % 100) > 60) ? WRITE : READ;
		pb->req.addr   = rand() % (1024 * P);	//access random address in one of our pages
		pb->req.status = WAITING;

		printf("Request: %d 0x%d -> ", pb->req.type, pb->req.addr);
		while(	(pb->req.status == WAITING) ||
						(pb->req.status == BLOCKED) ){	//while request is not processed
			POST(rsn);						//release sem, so master can get it and check request
			usleep(100);
			WAIT(rsn);						//get semaphore before checking req.type
		}
	POST(rsn);

	switch(pb->req.status){
		case GRANTED:	printf("GRANTED\n");	break;
		case DENIED:	printf("DENIED\n");
			break;
		default:
			pb->req.status = DENIED;
			printf("Error: unprocessed request code\n");
			break;
	}

	return pb->req.status;
}

static void sim_user(){

	printf("ID=%d\n", pb->id);

	while(1){

		if(mem_request() == DENIED)
			break;

		if(pb->term)	 //check if we should terminate
			break;
  }

	WAIT(SEM_SHARED);
		pb->id = 0;				
		shb->num_terminated++;
		printf("[%lu:%lu] Exit\n", shb->clock.tv_sec, shb->clock.tv_nsec);
	POST(SEM_SHARED);
}

static struct proc_block* parse_args(const int argc, char * const argv[]){

	if(argc < 2){
		printf("Usage: user [-v] <id>\n");
		return NULL;
	}

	rsn = atoi(argv[1]);
	stdout = freopen("/dev/null", "w", stdout);

	return &shb->pb[rsn];
}

int main(const int argc, char * const argv[]){

	shb = init_shared(0);
	pb = parse_args(argc, argv);
	if(pb == NULL)
		return EXIT_FAILURE;

	sim_user();

	shmdt(shb);
	return EXIT_SUCCESS;
}
