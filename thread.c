#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

/* This is the wait queue structure */
struct wait_queue {
	struct listNode *queueHead;
};

enum { READY = 1,
	   RUNNING = 2,
	   BLOCKED = 3,
	   EXITED = 4, 
	   FREE = 5,
	   KILLED = 6
};

/* This is the thread control block */
struct threadStruct {
	Tid tid;								//thread ID is tid which is an int
	int threadState;						//threadstate is just an int (enum'ed above)
	void *stackPointer;						//pointer to a stack that will be malloced in thread create
	ucontext_t threadContext;				//Thread context
};

struct listNode {
	Tid tid;
	struct listNode* nextNode;
};

//Global Variables for thread section
struct threadStruct threadList[THREAD_MAX_THREADS];
Tid currentThreadID;
struct listNode *readyQueueHead = NULL;
struct listNode *exitQueueHead = NULL;
//End of Global Variables

void queueAppend(struct listNode *Node){
	int currentInterrupts = interrupts_off();
	if(readyQueueHead == NULL){
		readyQueueHead = Node;
		readyQueueHead->nextNode = NULL;
		interrupts_set(currentInterrupts);
		return;
	}
	struct listNode *tempNode = readyQueueHead;
	while(tempNode->nextNode != NULL){
		tempNode = tempNode->nextNode;
	}
	tempNode->nextNode = Node;
	tempNode->nextNode->nextNode = NULL;
	interrupts_set(currentInterrupts);
	return;
}

void exitQueueAppend(struct listNode *Node){
	int currentInterrupts = interrupts_off();
	if(exitQueueHead == NULL){
		exitQueueHead = Node;
		exitQueueHead->nextNode = NULL;
		interrupts_set(currentInterrupts);
		return;
	}
	struct listNode *tempNode = exitQueueHead;
	while(tempNode->nextNode != NULL){
		tempNode = tempNode->nextNode;
	}
	tempNode->nextNode = Node;
	tempNode->nextNode->nextNode = NULL;
	interrupts_set(currentInterrupts);
	return;
}

void waitQueueAppend(struct listNode *Node, struct wait_queue *waitQueue){
	int currentInterrupts = interrupts_off();
	if(waitQueue->queueHead == NULL){
		waitQueue->queueHead = Node;
		waitQueue->queueHead->nextNode = NULL;
		interrupts_set(currentInterrupts);
		return;
	}
	struct listNode *tempNode = waitQueue->queueHead;
	while(tempNode->nextNode != NULL){
		tempNode = tempNode->nextNode;
	}
	tempNode->nextNode = Node;
	tempNode->nextNode->nextNode = NULL;
	interrupts_set(currentInterrupts);
	return;
}

void queueRemove(Tid goalTid){
	//We don't delete the thread from memory because the assumption is that the thread is moving from this queue to a different queue. 
	//Thread will be deleted from memory in thread_destroy();
	int currentInterrupts = interrupts_off();
	struct listNode *tempNode = readyQueueHead;
	struct listNode *preTempNode = NULL;
	if(tempNode->tid == goalTid){
		readyQueueHead = readyQueueHead->nextNode;
		free(tempNode); 
		interrupts_set(currentInterrupts);
		return;
	}
	while(tempNode != NULL){
		if(tempNode->tid == goalTid){
			preTempNode->nextNode = tempNode->nextNode;
			free(tempNode); 
			interrupts_set(currentInterrupts);
			return;
		}
		preTempNode = tempNode;
		tempNode = tempNode->nextNode;
	}
	if(tempNode == NULL){
		//printf("Objective Tid not in list");
		interrupts_set(currentInterrupts);
		return;
	}
}

void exitQueueRemove(Tid goalTid){
	int currentInterrupts = interrupts_off();
	struct listNode *tempNode = exitQueueHead;
	struct listNode *preTempNode = NULL;
	if(tempNode->tid == goalTid){
		exitQueueHead = exitQueueHead->nextNode;
		free(tempNode);
		interrupts_set(currentInterrupts);
		return;
	}
	while(tempNode != NULL){
		if(tempNode->tid == goalTid){
			preTempNode->nextNode = tempNode->nextNode;
			free(tempNode); 
			interrupts_set(currentInterrupts);
			return;
		}
		preTempNode = tempNode;
		tempNode = tempNode->nextNode;
	}
	if(tempNode == NULL){
		//printf("Objective Tid not in list");
		interrupts_set(currentInterrupts);
		return;
	}
}

void waitQueueRemove(Tid goalTid, struct wait_queue *waitQueue){
	int currentInterrupts = interrupts_off();
	struct listNode *tempNode = waitQueue->queueHead;
	struct listNode *preTempNode = NULL;
	if(tempNode->tid == goalTid){
		waitQueue->queueHead = waitQueue->queueHead->nextNode;
		free(tempNode);
		interrupts_set(currentInterrupts);
		return;
	}
	while(tempNode != NULL){
		if(tempNode->tid == goalTid){
			preTempNode->nextNode = tempNode->nextNode;
			free(tempNode); 
			interrupts_set(currentInterrupts);
			return;
		}
		preTempNode = tempNode;
		tempNode = tempNode->nextNode;
	}
	if(tempNode == NULL){
		//printf("Objective Tid not in list");
		interrupts_set(currentInterrupts);
		return;
	}
}

//Print Linked List Code
void printList(struct listNode *tempNode){
	printf("\n\nList elements are - \n");
	while(tempNode != NULL) {
		printf("%d ->",tempNode->tid);
		tempNode = tempNode->nextNode;
	}
	return;
}

void thread_init(void)
{
	for(int i = 0; i < THREAD_MAX_THREADS; i++){
		threadList[i].tid = i;
		threadList[i].threadState = FREE;
	}
	threadList[0].threadState = RUNNING;
	currentThreadID = 0;
	return;
}

Tid thread_id()
{
	return currentThreadID;
}

void thread_stub(void (*thread_main)(void *), void *arg)
{
	//Tid ret;
	interrupts_on();
	thread_main(arg); // call thread_main() function with arg
	thread_exit();
}

Tid thread_create(void (*fn) (void *), void *parg)
{
	//Create a thread whose starting point is the function fn called with arguments parg
	//Iterate through threadList, if a thread is "free" then make the new thread there.
	/*Making the thread: 
		malloc the stack of size THREAD_MIN_STACK size. Return THREAD_NOMEMORY if stack allocation fails.
		thread structure:
			thread state = READY
			getcontext of current thread, saved in new thread structure
			change the key registers to be for the new thread
		Add thread to ready queue.
	*/
	int currentInterrupts = interrupts_off();

	Tid newTid;

	for(int i = 0; i < THREAD_MAX_THREADS; i++){
		if (threadList[i].threadState == FREE){
			newTid = i;
			break;
		}
		else if(i == THREAD_MAX_THREADS - 1){
			return THREAD_NOMORE;
		}
	}

	void *stackPointer = malloc(THREAD_MIN_STACK + 8);
	if(stackPointer == NULL){
		return THREAD_NOMEMORY;
	}

	threadList[newTid].stackPointer = stackPointer;
	threadList[newTid].threadState = READY;
	
	long long int addr = (long long int) stackPointer;
	addr += THREAD_MIN_STACK + 8;
	addr &= (-8);

	void *alignedPointer = (long long int *)addr;

	ucontext_t mycontext = { 0 };
	int err = getcontext(&mycontext);
	assert(!err);

	mycontext.uc_mcontext.gregs[REG_RSP] = (long long int) alignedPointer;
	mycontext.uc_mcontext.gregs[REG_RIP] = (long long int) thread_stub;
	mycontext.uc_mcontext.gregs[REG_RDI] = (long long int) fn;	
	mycontext.uc_mcontext.gregs[REG_RSI] = (long long int) parg;

	//edit mycontext first then do this last
	threadList[newTid].threadContext = mycontext;

	struct listNode *newNode;
	newNode = malloc(sizeof(struct listNode));
	newNode->tid = newTid;
	newNode->nextNode = NULL;
	queueAppend(newNode);

	interrupts_set(currentInterrupts);
	return newTid;
}

Tid thread_yield(Tid want_tid)
{	
	int currentInterrupts = interrupts_off();

	Tid returnTid;
	if(want_tid >= THREAD_MAX_THREADS || want_tid < -2){
		return THREAD_INVALID;
	}
	if(want_tid >= 0 && want_tid != currentThreadID){
		if(threadList[want_tid].threadState != READY && threadList[want_tid].threadState != KILLED){
			return THREAD_INVALID;
		}
	}
	int setcontext_called = 0;
	ucontext_t mycontext = { 0 };
	int err = getcontext(&mycontext);
	assert(!err);

	if (setcontext_called == 1) {
		//New thread is running now babyy
		while(exitQueueHead != NULL /*&& exitQueueHead->tid != currentThreadID*/){
			Tid exitTid = exitQueueHead->tid;
			if(threadList[exitTid].threadState == EXITED){
				free(threadList[exitTid].stackPointer);
				threadList[exitTid].threadState = FREE;
				ucontext_t newContext = { 0 };
				threadList[exitTid].threadContext = newContext;
			}
			exitQueueRemove(exitTid);
		}
		if(threadList[currentThreadID].threadState == KILLED){
			thread_exit();
		}
		interrupts_set(currentInterrupts);
		return returnTid;
	}
	struct listNode *currThreadNode;
	if(threadList[currentThreadID].threadState != EXITED){
		//Make node that represents current thread to put on ready queue.
		currThreadNode = malloc(sizeof(struct listNode));
		currThreadNode->tid = currentThreadID;
		currThreadNode->nextNode = NULL;
		queueAppend(currThreadNode);
		threadList[currentThreadID].threadState = READY;
		threadList[currentThreadID].threadContext = mycontext;
	}

	Tid newTid;

	if(want_tid >= 0){
		newTid = want_tid;
	}
	 
	else if(want_tid == THREAD_ANY){
		newTid = readyQueueHead->tid;
		if(newTid == currentThreadID || readyQueueHead == NULL){
			if(threadList[currentThreadID].threadState != EXITED){
				free(currThreadNode);
			}
			return THREAD_NONE;
		}
	}

	else if(want_tid == THREAD_SELF){
		newTid = currentThreadID;
	}

	queueRemove(newTid);				//Frees the malloc of the newTid node as well :)
	if(threadList[newTid].threadState != KILLED){
		threadList[newTid].threadState = RUNNING;
	}
	currentThreadID = newTid;
	returnTid = currentThreadID;
	ucontext_t newContext = threadList[newTid].threadContext;
	setcontext_called = 1;
	err = setcontext(&newContext);
	assert(!err);
	assert(0);
	//printf("I don't think we should ever get here\n");

	//Suspend the caller
	//Put caller on tail of readyqueue
	//Case 1: Tid is specified, run want_tid next
	//Case 2: THREAD_ANY run the top of the ready queue
	//Case 3: THREAD_SELF explicitly switch to self thread for debugging purposes.
	//Return identifier of thread that took control as a result of the call.
	//If function fails, caller resumes immediately.
	//Reasons for failure: THREAD_INVALID or THREAD_NONE
}

void thread_exit()
{
	//If there are any other threads in the system, set this thread to exited and switch to one of them
	//If there are no other threads in the system, the program should exit. 
	//A thread created later should be able to use this thread's identifier, but only if the thread has been DESTROYED
	struct listNode *currThreadNode;
	currThreadNode = malloc(sizeof(struct listNode));
	currThreadNode->tid = currentThreadID;
	currThreadNode->nextNode = NULL;
	threadList[currentThreadID].threadState = EXITED;
	exitQueueAppend(currThreadNode);

	Tid err;
	err = thread_yield(THREAD_ANY);
	if(err == THREAD_NONE){
		free(currThreadNode);
		Tid exitTid = currentThreadID;
		free(threadList[exitTid].stackPointer);
		exitQueueRemove(exitTid);
		exit(0);
	}
}

Tid thread_kill(Tid tid)
{
	int currentInterrupts = interrupts_off();

	//Set the tid thread's control block so that it will be killed. 
	if(tid == currentThreadID){
		return THREAD_INVALID;
	}
	if(tid < 0 || tid >= THREAD_MAX_THREADS){
		return THREAD_INVALID;
	}
	if(threadList[tid].threadState != READY && threadList[tid].threadState != BLOCKED){
		return THREAD_INVALID;
	}
	threadList[tid].threadState = KILLED;

	interrupts_set(currentInterrupts);
	return tid;
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */

//struct wait_queue *

struct wait_queue *wait_queue_create()
{
	int currentInterrupts = interrupts_off();

	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	interrupts_set(currentInterrupts);
	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{	
	int currentInterrupts = interrupts_off();

	if(wq->queueHead == NULL){
		free(wq);
		interrupts_set(currentInterrupts);
		return;
	}

	else{
		interrupts_set(currentInterrupts);
		printf("wait_queue_destroy called but queue is not empty");
		return;
	}
}

Tid
thread_sleep(struct wait_queue *queue)
{
	int currentInterrupts = interrupts_off();
	//Suspend the caller by running some other thread (thread yield). 
	//Put the caller thread in the wait_queue passed as an argument.
	//Upon success, return the identifier of the thread that took control.
	//If it fails, the calling thread continues running and says:
	//THREAD_INVALID: the queue is NULL
	//THREAD_NONE: there are no other threads to run. This is the last one. 
	if(queue == NULL){
		interrupts_set(currentInterrupts);
		return THREAD_INVALID;
	}

	struct listNode *currThreadNode;
	currThreadNode = malloc(sizeof(struct listNode));
	currThreadNode->tid = currentThreadID;
	currThreadNode->nextNode = NULL;
	threadList[currentThreadID].threadState = BLOCKED;
	waitQueueAppend(currThreadNode, queue);

	Tid err;
	err = thread_yield(THREAD_ANY);
	if(err == THREAD_NONE){
		free(currThreadNode);
		threadList[currentThreadID].threadState = RUNNING;
		waitQueueRemove(currentThreadID, queue);
		interrupts_set(currentInterrupts);
		return THREAD_NONE;
	}

	interrupts_set(currentInterrupts);
	return err;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{	
	int currentInterrupts = interrupts_off();
	//Move one or all threads from wait queue to ready queue. Calling thread continues to run. 
	if(queue == NULL || queue->queueHead == NULL){
		interrupts_set(currentInterrupts);
		return 0;
	}

	struct listNode *tempNode;
	if(all == 0){
		tempNode = queue->queueHead;
		queue->queueHead = queue->queueHead->nextNode;

		struct listNode *newNode;
		newNode = malloc(sizeof(struct listNode));
		newNode->tid = tempNode->tid;
		newNode->nextNode = NULL;
		waitQueueRemove(tempNode->tid, queue);
		queueAppend(newNode);
		interrupts_set(currentInterrupts);
		return 1;
	}
	
	int count = 0;
	if(all == 1){
		while(queue->queueHead != NULL){
			count ++;
			tempNode = queue->queueHead;
			queue->queueHead = queue->queueHead->nextNode;

			struct listNode *newNode;
			newNode = malloc(sizeof(struct listNode));
			newNode->tid = tempNode->tid;
			newNode->nextNode = NULL;
			waitQueueRemove(tempNode->tid, queue);
			queueAppend(newNode);
		}
	}
	interrupts_set(currentInterrupts);
	return count;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	TBD();
	return 0;
}

struct lock {
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv {
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}
