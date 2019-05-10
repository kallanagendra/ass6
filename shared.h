/* Size of virtual page in Kb */
#define PAGE_SIZE 1
/* Size of memory in Kb */
#define MEM_SIZE 256
/* Number of pages in process */
#define P 32
/* Number of frames in system*/
#define F 256
/* Number of references, after which we shift refbits*/
#define SHIFT_PERIOD 20

/* Maximum running processes*/
#define N 18
/* Log maximum lines */
#define LMAX 10000

enum status{ GRANTED=0, BLOCKED, WAITING, DENIED };
enum type {READ=0, WRITE};

struct page {
	int frame;									//frame/hardware index
};

struct frame{
	int id;						//user[] index of process using the frame
	int page;						//index of page, using the frame

	int used;
	int dirty;
	unsigned char refbits;
};

struct request{
	int type;
	int addr;

	struct timespec load_time;	//if request is suspended, this show when it will load
	enum status status;
};

struct proc_block{
	pid_t pid;
	int term;
	unsigned int id;

	struct page page_table[P];
	struct request req;
};

struct resource{
	int available;	//current value
	int max;				//maximum value
	int shared;			//is it shareable
};

struct shared_block{
	struct proc_block 	pb[P];
	struct frame				frame_table[F];
	struct timespec   	clock;

	unsigned int num_terminated;	//user process updates this, when it exists
};

enum sem_names{
	/*SEM_PCB=[0-P]*/	//used for request sync
	SEM_SHARED=P,			//clock, num_terminated

	SEM_NSEMS					//number of semaphores in our set
};


struct shared_block* init_shared(const int flags);
void deinit_shared(struct shared_block * addr);

void WAIT(int num);
void POST(int num);
