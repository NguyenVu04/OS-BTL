
#include "queue.h"
#include "sched.h"
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
int curr_slot;
int curr_queue;
static int32_t slot[MAX_PRIO];
#endif

int queue_empty(void) {
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if(!empty(&mlq_ready_queue[prio])) 
			return -1;
#endif
	return (empty(&ready_queue) && empty(&run_queue));
}

void init_scheduler(void) {
#ifdef MLQ_SCHED
	int i ;
	curr_slot = MAX_PRIO;
	curr_queue = 0;
	for (i = 0; i < MAX_PRIO; i ++){
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i; //Init slot for each queue in ready_queue
	}
		
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
	pthread_mutex_init(&queue_lock, NULL);
}

#ifdef MLQ_SCHED
/* 
 *  Stateful design for routine calling
 *  based on the priority and our MLQ policy
 *  We implement stateful here using transition technique
 *  State representation   prio = 0 .. MAX_PRIO, curr_slot = 0..(MAX_PRIO - prio)
 */
struct pcb_t * get_mlq_proc(void) 
{
	struct pcb_t * proc = NULL;
	pthread_mutex_lock(&queue_lock);
	if (empty(&mlq_ready_queue[curr_queue]) || slot[curr_queue] == 0){
		int i;
		for (i = 0; i < MAX_PRIO; i++) {
			if (i == curr_queue)
				continue;
			proc = dequeue(&mlq_ready_queue[i]);
			if (proc != NULL)
				break;
		}
		slot[curr_queue] = MAX_PRIO - curr_queue;
		if (proc == NULL) {
			proc = dequeue(&mlq_ready_queue[curr_queue]);
			curr_queue = proc == NULL ? 0 : curr_queue;
		} else {
			curr_queue = i;
		}
	} else {
		proc = dequeue(&mlq_ready_queue[curr_queue]);
	}
	pthread_mutex_unlock(&queue_lock);
	return proc;
}

void put_mlq_proc(struct pcb_t * proc) 
{
	pthread_mutex_lock(&queue_lock);
	slot[proc->prio]--;
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_mlq_proc(struct pcb_t * proc) 
{
	pthread_mutex_lock(&queue_lock);
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);	
}

struct pcb_t * get_proc(void) 
{
	return get_mlq_proc();
}

void put_proc(struct pcb_t * proc) 
{
	return put_mlq_proc(proc);
}

void add_proc(struct pcb_t * proc) 
{
	return add_mlq_proc(proc);
}
#else
struct pcb_t * get_proc(void) {
	struct pcb_t * proc = NULL;
	/*TODO: get a process from [ready_queue].
	 * Remember to use lock to protect the queue.
	 * */
	pthread_mutex_lock(&queue_lock);
	proc = dequeue(&ready_queue);
	pthread_mutex_unlock(&queue_lock);
	return proc;
}

void put_proc(struct pcb_t * proc) {
	pthread_mutex_lock(&queue_lock);
	enqueue(&run_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

void add_proc(struct pcb_t * proc) {
	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);	
}
#endif


