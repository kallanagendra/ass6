#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include "shared.h"

union semun {
   int              val;    /* Value for SETVAL */
   struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
   unsigned short  *array;  /* Array for GETALL, SETALL */
   struct seminfo  *__buf;  /* Buffer for IPC_INFO
							   (Linux-specific) */
};

static int mid = -1;  //shared memory identifier
static int sid = -1;  //semaphore identifier
static struct sembuf sb = {.sem_num=0, .sem_op=0, .sem_flg=0};

struct shared_block* init_shared(const int flags){
	key_t key = ftok("shared.c", 1);
	if(key == -1){
		perror("ftok");
		return NULL;
	}

	mid = shmget(key, sizeof(struct shared_block), flags);
	if(mid == -1){
		perror("shmget");
		return NULL;
	}

	struct shared_block * shmaddr = (struct shared_block*) shmat(mid, NULL, 0);
	if(shmaddr == (void*)-1){
		perror("shmat");
		return NULL;
	}

	key = ftok("shared.c", 2);
	if(key == -1){
		perror("ftok");
		return NULL;
	}

	sid = semget(key, SEM_NSEMS, flags);
	if(sid == -1){
		perror("semget");
		return NULL;
	}


  if(flags){  //if we create the semaphore set

    //clear shared memory region
  	bzero(shmaddr, sizeof(struct shared_block));

    //initialize all semaphores as unlocked
  	union semun un;
  	int i;
  	for(i=0; i < SEM_NSEMS; i++){
  		un.val = 1;  //sem is unlocked
  		if(semctl(sid, i, SETVAL, un) ==-1){
  			perror("semctl");
  			return 0;
  		}
  	}
  }

  srand(getpid()); //not shared, but both processes do it

  return shmaddr;
}

void deinit_shared(struct shared_block * addr){
  semctl(sid, 0, IPC_RMID);
  shmctl(mid, IPC_RMID, NULL);
  shmdt(addr);
}

static void op(int sid, struct sembuf * sb){
	if (semop(sid, sb, 1) == -1) {
	   perror("semop");
	   exit(1);
	}
}

void WAIT(int num){
	sb.sem_op   = -1;
	sb.sem_num = num;
	op(sid, &sb);
}

void POST(int num){

	sb.sem_op   = 1;
	sb.sem_num = num;
	op(sid, &sb);
}
