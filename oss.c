#include <string.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <strings.h>

#include "shared.h"

static struct shared_block *shb = NULL;

/* various counter variables */
static unsigned int output_lines = 0;
static unsigned int ref_count = 0, fault_count = 0;
static unsigned int user_count = 0, user_limit = N;

#define NS_IN_SEC 1000000000

struct queue{
  //each process scan make one request, then block
	int pb[N];
	int count;
};
static struct queue suspended;

static void timespec_add(struct timespec *t, const unsigned int nsec){
	t->tv_nsec += nsec;
	if(t->tv_nsec > NS_IN_SEC){
		t->tv_sec++;
		t->tv_nsec -= NS_IN_SEC;
	}
}

static void timespec_diff(struct timespec *t1, struct timespec *t2, struct timespec *t){
    if ((t2->tv_nsec - t2->tv_nsec) < 0) {
			t->tv_sec  = t2->tv_sec  - t1->tv_sec - 1;
      t->tv_nsec = t2->tv_nsec - t1->tv_nsec + NS_IN_SEC;
    } else {
        t->tv_sec  = t2->tv_sec  - t1->tv_sec;
        t->tv_nsec = t2->tv_nsec - t1->tv_nsec;
    }
}

static void frame_map(){

	fprintf(stdout, "Current memory layout at time %lu:%lu is:\n", shb->clock.tv_sec, shb->clock.tv_nsec);
	fprintf(stdout, "\t\t\t\t%10s\t%10s\t%10s\n", "Occupied", "refbits", "DirtyBit");

	int i;
	for(i=0; i < F; i++){
		struct frame * f = &shb->frame_table[i];
		if(f->page < 0){
			fprintf(stdout, "Frame %3d: %5s\t%10d\t%10d\n", i, "No", f->refbits, f->dirty);
		}else{
			fprintf(stdout, "Frame %3d: %5s\t%10d\t%10d\n", i, "Yes", f->refbits, f->dirty);
		}
	}

	output_lines+= F + 2;
}

//Show report on stderr, because stdout may be redirected to null
static void report(){
	fprintf(stdout, "--- Report ---\n\n");
	fprintf(stdout, "OSS Clock: %lu:%lu\n", shb->clock.tv_sec, shb->clock.tv_nsec);
	fprintf(stdout, "Total references: %u\n", ref_count);
	fprintf(stdout, "References per second: %.3f\n", (float) ref_count / shb->clock.tv_sec);
	fprintf(stdout, "Faults per reference: %.3f\n", (float)fault_count / ref_count);
}

static void before_exit(const int code){

	int i;
	for(i=0; i < N; i++){
		struct proc_block * pb = &shb->pb[i];
		if(pb->id > 0)
			kill(pb->pid, SIGTERM);
	}

	frame_map();
	report();

	deinit_shared(shb);
	exit(code);
}

static void signal_handler(int sig){
  if((sig == SIGTERM) || (sig == SIGINT)){
		int i;
		for(i=0; i < N; i++){
			struct proc_block * pb = &shb->pb[i];
			if(pb->id > 0){
				WAIT(i);
				pb->term = 1;
				pb->req.status = DENIED;
				POST(i);
			}
		}
  }
}

static int fork_child(){

	if(user_count >= N)
		return 0;

	//check bit vector if we have free block
	WAIT(SEM_SHARED);
	int bi;	//block index
	for(bi=0; bi < N; bi++){
		if(shb->pb[bi].id == 0)
			break;
	}
	POST(SEM_SHARED);

	if(bi == N) //no space or max users reached
		return 0;

	struct proc_block *pb = &shb->pb[bi];
	pb->id = ++user_count;

	char buf[10];
	snprintf(buf, sizeof(buf), "%d", bi);

	switch(fork()){
		case -1:
			perror("fork");
			return EXIT_FAILURE;
			break;

		case 0: //child process
			pb->pid = getpid();
			signal(SIGTERM, SIG_IGN);
			signal(SIGINT, SIG_IGN);
			signal(SIGCHLD, SIG_IGN);

			execl("./user", "./user", buf, NULL);  //run the user program
			perror("execl");
			exit(EXIT_FAILURE);
			break;

		default:
			++output_lines;
			printf("Master: Generating process P%d with PID %u at [%lu:%lu]\n", pb->id, pb->pid, shb->clock.tv_sec, shb->clock.tv_nsec);
			break;
	}

	return 1;
}

static void clock_update(const int sec, const int nsec){

	WAIT(SEM_SHARED);

	shb->clock.tv_sec  += sec;
	timespec_add(&shb->clock, nsec);

	POST(SEM_SHARED);

}

static int on_fault(struct proc_block * pb, const int page){

	fault_count++;

  //search for a free frame to load page in
	int i;
	for(i=0; i < F; i++){
		if(shb->frame_table[i].id == -1){	//if nobody is using the frame
			++output_lines;
			printf("Master: Using free frame %d for P%d page %d\n", i, pb->id, page);
			return i;
		}
	}

	//find the least referenced frame
	struct frame * frame = &shb->frame_table[0];
	int least_used = 0;
	for(i=1; i < F; i++){
		if(frame->refbits == 0)	//we can't go lower than 0
			break;

    if(shb->frame_table[i].refbits < frame->refbits){
			frame = &shb->frame_table[i];
			least_used = i;
		}
	}

	//-1 because index = id - 1
  struct page * ppage = &shb->pb[frame->id-1].page_table[frame->page];
  struct proc_block * swapped = &shb->pb[frame->id-1];

	++output_lines;
	printf("Master: Clearing frame %d and swapping P%d page %d for P%d page %d\n", ppage->frame, swapped->id, frame->page, pb->id, page);

	//clear the frame ,before its being reused
  frame->page = frame->id = -1;
	frame->used = frame->refbits = frame->dirty = 0;

	ppage->frame = -1;

  return least_used;  //return index of free frame
}

static enum status on_load(struct proc_block * pb){
	static int ref_counter = 0;
  enum status rv;

	struct request * req = &pb->req;
	struct frame * frame = NULL;

	ref_count++;

	const int page = req->addr / 1024;
  if(page > P){
		fprintf(stderr, "Error: Invalid address %d\n", req->addr);
		return DENIED;
	}

  struct page * p = &pb->page_table[page];

  if(p->frame == -1){	//if page is not in memory

		++output_lines;
		printf("Master: Address %d is not in a frame, pagefault\n", req->addr);

		p->frame = on_fault(pb, page); //get frame for page, so we can load it into memory
		if(p->frame == -1){ //if there is no space in memory
			fprintf(stderr, "No frame for page %d\n", page);
			return DENIED;
		}

		struct frame * frame = &shb->frame_table[p->frame];
		frame->used = 1;
		frame->page = page;
		frame->id   = pb->id;

		rv = BLOCKED;
		suspended.pb[suspended.count++] = pb->id;  //save procs index to queue

  }else{ //no page fault

		frame = &shb->frame_table[p->frame];

		if(req->type == READ){
		  ++output_lines;
			printf("Master: Address %d in frame %d, giving data to P%d at time %lu:%lu\n", req->addr, p->frame, pb->id, shb->clock.tv_sec, shb->clock.tv_nsec);

		}else if(req->type == WRITE){
			++output_lines;
			printf("Master: Address %d in frame %d, writing data to frame at time %lu:%lu\n", req->addr, p->frame, shb->clock.tv_sec, shb->clock.tv_nsec);

		  if(frame->dirty == 0){
			  timespec_add(&shb->clock, 25);

				++output_lines;
				printf("Master: Dirty bit of frame %d set, adding additional time to the clock\n", p->frame);
		  }
		}

		frame->refbits = (1<<7);	//set higest bit

		ref_counter++;
		if(ref_counter >= SHIFT_PERIOD){
			int fi;
			for(fi=0; fi < F; fi++){
		    shb->frame_table[fi].refbits = shb->frame_table[fi].refbits >> 1;	//shift left
			}
			ref_counter = 0;	//reset the counter
		}

	  //add 10ns to clock
	  timespec_add(&shb->clock, 10);

	  rv = GRANTED;
  }

  return rv;
}

static enum status on_read(struct proc_block * pb){

  ++output_lines;
	printf("Master: P%d requesting read of address %d at time %lu:%lu\n", pb->id, pb->req.addr, shb->clock.tv_sec, shb->clock.tv_nsec);

  return on_load(pb);
}

static enum status on_write(struct proc_block * pb){
  ++output_lines;
	printf("Master: P%d requesting write of address %d at time %lu:%lu\n", pb->id, pb->req.addr, shb->clock.tv_sec, shb->clock.tv_nsec);

  enum status rv = on_load(pb);

  return rv;
}

static enum status check_request(struct proc_block * pb){
	switch(pb->req.type){
		case READ:	return on_read(pb);	break;
		case WRITE:	return on_write(pb);break;
	}
	return DENIED;
}

static void scan_requests(){
	int i;

	struct timespec t1,t2,t3;
	clock_gettime(CLOCK_REALTIME, &t1);

	for(i=0; i < N; i++){
		struct proc_block * pb = &shb->pb[i];

		//if we don't have a process or request is not waiting
		WAIT(i);
		if((pb->id == 0) || (pb->req.status != WAITING)){
			POST(i);
			continue;
		}

		pb->req.status = check_request(pb);

		POST(i);
	}

	clock_gettime(CLOCK_REALTIME, &t2);

	timespec_diff(&t2, &t1, &t3);
	clock_update(t3.tv_sec, t3.tv_nsec);
}

static int scan_suspended(){

	//if queue is full, update clock
	if(suspended.count == user_limit){
		const int j = suspended.pb[0];	//index of the suspended process
		struct request * req = &shb->pb[j].req;	//requst

		shb->clock = req->load_time;
	}

	int i;
	for(i=0; i < suspended.count; i++){

		const int j = suspended.pb[i] - 1;	//index of the suspended process
		struct request * req = &shb->pb[j].req;	//requst

		//if request time is in future
		if(	(req->load_time.tv_sec > shb->clock.tv_sec) ||
				((req->load_time.tv_sec == shb->clock.tv_sec) && (req->load_time.tv_nsec > shb->clock.tv_nsec)) ){
				//stop because requests are ordered by time
				break;
		}

		++output_lines;
		if(req->type == READ){
			fprintf(stdout, "Master: Indicating to P%d that write has happened to address %d\n", shb->pb[j].id, req->addr);
		}else if(req->type == WRITE){
			fprintf(stdout, "Master: Indicating to P%d that write has happened to address %d\n", shb->pb[j].id, req->addr);
		}

		const int page = req->addr / 1024;
		const int frame = shb->pb[j].page_table[page].frame;
		shb->frame_table[frame].dirty = 1;	//written frames become dirty

		req->status = GRANTED;
		POST(j);	//release the waiting user
	}

	if(i != 0){
		//shift request with the amount of processed
		suspended.count -= i;
		memmove(suspended.pb, &suspended.pb[i], (suspended.count*sizeof(int)));
	}

	return i;
}

static void check_line_count(){
	static int to_null = 0;

	if(to_null)
		return;


	if(output_lines > LMAX){	//if log file is larger thatn 10 000 lines
		fprintf(stderr, "OSS: Discarding output after %d lines\n", LMAX);

		stdout = freopen("/dev/null", "w", stdout);
		if(stdout == NULL){
			perror("fopen");
			exit(1);
		}
		to_null = 1;
	}
}

//simulate a process scheduler
static void mem_simulation(){

	int last_frame_map = 0;
	while(shb->num_terminated != N){ //loop until all procs are done

		clock_update(1, rand() % 500);
		fork_child();	//fork a user process

		scan_requests();
		scan_suspended();

		//on every 100 requests show frame map
		if((ref_count - last_frame_map) > 100){
			frame_map();
			last_frame_map = ref_count;
		}

		check_line_count();
	}

	++output_lines;
	printf("Master: Finished at [%lu:%lu]\n", shb->clock.tv_sec, shb->clock.tv_nsec);
}

static int init_sim(){

	//initialize available resources
	shb = init_shared(IPC_CREAT | IPC_EXCL | S_IRWXU);
	if(shb == NULL)
		return 0;

	int i;
	for(i=0; i < N; i++){	//for each user
		int j;
		for(j=0; j < P; j++){	//for each page
			shb->pb[i].page_table[j].frame = -1;	//no frame for this page
		}
	}

	//clear frames in frame table
  for(i=0; i < F; i++){
		shb->frame_table[i].id = -1;     //no page in this frame
    shb->frame_table[i].page = -1;     //no page in this frame
    shb->frame_table[i].used = 0;
		shb->frame_table[i].dirty = 0;
		shb->frame_table[i].refbits = 0;
  }

	return 1;
}

static int parse_args(const int argc, char * const argv[]){
	int opt;
	while ((opt = getopt(argc, argv, "vn")) != -1) {
    switch (opt) {
			case 'n':
				user_limit = atoi(optarg);
				break;
			default:
				fprintf(stderr, "Usage: %s [-v] -n user_limit\n", argv[0]);
				return 0;
				break;
		}
	}

	//redirect stdout to log file
	stdout = freopen("oss.txt", "w", stdout);
	if(stdout == NULL){
		perror("fopen");
		return 0;
	}

	return 1;
}

int main(const int argc, char * const argv[]){


	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGCHLD, SIG_IGN);

	if(	(parse_args(argc, argv) == 0) ||
			(init_sim() == 0) )
		return EXIT_FAILURE;

	mem_simulation();
	before_exit(EXIT_SUCCESS);

	return EXIT_SUCCESS;
}
