/* Tests cetegorical mutual exclusion with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */
#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "lib/random.h" //generate random numbers
#include "devices/timer.h"

#define BUS_CAPACITY 3
#define SENDER 0
#define RECEIVER 1
#define NORMAL 0
#define HIGH 1
#define TASKDIR task.direction
#define TASKPRIO task.priority

/*
 *	initialize task with direction and priority
 *	call o
 * */
typedef struct {
	int direction;
	int priority;
} task_t;

static struct semaphore prioQueue[2];
static struct semaphore normQueue[2];
static struct lock lock;
static int freeSlots;
static int waitingPrio[2];
static int waitingNorm[2];
static int dir;

void batchScheduler(unsigned int num_tasks_send, unsigned int num_task_receive,
        unsigned int num_priority_send, unsigned int num_priority_receive);

void senderTask(void *);
void receiverTask(void *);
void senderPriorityTask(void *);
void receiverPriorityTask(void *);


void oneTask(task_t task);/*Task requires to use the bus and executes methods below*/
	void getSlot(task_t task); /* task tries to use slot on the bus */
	void transferData(task_t task); /* task processes data on the bus either sending or receiving based on the direction*/
	void leaveSlot(task_t task); /* task release the slot */



/* initializes semaphores */ 
void init_bus(void){ 
 
    random_init((unsigned int)123456789); 
    sema_init(&prioQueue[0], 0);
    sema_init(&prioQueue[1], 0);
    sema_init(&normQueue[0], 0);
    sema_init(&normQueue[1], 0);
    lock_init(&lock);
    freeSlots = BUS_CAPACITY;
    waitingPrio[0] = 0;
    waitingPrio[1] = 0;
    waitingNorm[0] = 0;
    waitingNorm[1] = 0;
    dir = SENDER;
}

/*
 *  Creates a memory bus sub-system  with num_tasks_send + num_priority_send
 *  sending data to the accelerator and num_task_receive + num_priority_receive tasks
 *  reading data/results from the accelerator.
 *
 *  Every task is represented by its own thread. 
 *  Task requires and gets slot on bus system (1)
 *  process data and the bus (2)
 *  Leave the bus (3).
 */

void batchScheduler(unsigned int num_tasks_send, unsigned int num_task_receive,
        unsigned int num_priority_send, unsigned int num_priority_receive)
{
  unsigned int i;

  /* Create the high priority tasks threads */
  for (i = 0; i < num_priority_send;i++) {
    thread_create("ps" + i, PRI_DEFAULT, &senderPriorityTask, NULL);
  }
  for (i = 0; i < num_priority_receive;i++) {
    thread_create("pr" + i, PRI_DEFAULT, &receiverPriorityTask, NULL);
  }

  /* Create the normal priority tasks threads */
  for (i = 0; i < num_tasks_send;i++) {
    thread_create("ns" + i, PRI_DEFAULT, &senderTask, NULL);
  }
  for (i = 0; i < num_task_receive;i++) {
    thread_create("nr" + i, PRI_DEFAULT, &receiverTask, NULL);
  }
}

/* Normal task,  sending data to the accelerator */
void senderTask(void *aux UNUSED){
        task_t task = {SENDER, NORMAL};
        oneTask(task);
}

/* High priority task, sending data to the accelerator */
void senderPriorityTask(void *aux UNUSED){
        task_t task = {SENDER, HIGH};
        oneTask(task);
}

/* Normal task, reading data from the accelerator */
void receiverTask(void *aux UNUSED){
        task_t task = {RECEIVER, NORMAL};
        oneTask(task);
}

/* High priority task, reading data from the accelerator */
void receiverPriorityTask(void *aux UNUSED){
        task_t task = {RECEIVER, HIGH};
        oneTask(task);
}

/* abstract task execution*/
void oneTask(task_t task) {
  getSlot(task);
  transferData(task);
  leaveSlot(task);
}


/* task tries to get slot on the bus subsystem */
void getSlot(task_t task) 
{
  lock_acquire(&lock);
  if (TASKPRIO == HIGH) {   /* TASKPRIO = task->priority */
    waitingPrio[TASKDIR]++;
    while (!freeSlots || dir != TASKDIR) {
      if (freeSlots != BUS_CAPACITY) {
        lock_release(&lock);
        sema_down(prioQueue[TASKDIR]);  /* TASKDIR = task->direction */
        lock_acquire(&lock);
      } else {
        dir = TASKDIR;
      }
    }
    waitingPrio[TASKDIR]--;
  } else {
    waitingNorm[TASKDIR]++;
    while (dir != TASKDIR || !freeSlots || (waitingPrio[0] + waitingPrio[1]) > 0) {
      if (!freeSlots || (waitingPrio[0] + waitingPrio[1]) > 0 || freeSlots != BUS_CAPACITY) {
        lock_release(&lock);
        sema_down(normQueue[TASKDIR]);
        lock_acquire(&lock);
      } else {
        dir = TASKDIR;
      }
    }
    waitingNorm[TASKDIR]--;
  }
  freeSlots--;
  lock_release(&lock);
}

/* task processes data on the bus send/receive */
void transferData(task_t task) 
{
  //printf("Thread %s started transfer", thread_current->name);
  timer_msleep(random_ulong() % 2000);
  //printf("Thread %s finished transfer", thread_current->name);
}

/* task releases the slot */
void leaveSlot(task_t task) 
{
  lock_acquire(&lock);
  freeSlots++;
  if (waitingPrio[TASKDIR]) {
    sema_up(prioQueue[TASKDIR]);
  } else if (!waitingPrio[1-TASKDIR] && waitingNorm[TASKDIR]) {
    sema_up(normQueue[TASKDIR]);
  } else if (freeSlots == BUS_CAPACITY) {
    dir = 1 - TASKDIR;
    int tmpFreeSlots = BUS_CAPACITY;
    int i;
    for (i = 0; i < waitingPrio[1-TASKDIR] && tmpFreeSlots > 0; i++) {
      tmpFreeSlots--;
      sema_up(prioQueue[1-TASKDIR]);
    }
    for (i = 0; i < waitingNorm[1-TASKDIR] && tmpFreeSlots > 0; i++) {
      tmpFreeSlots--;
      sema_up(normQueue[1-TASKDIR]);
    }
  }
  lock_release(&lock);
}
