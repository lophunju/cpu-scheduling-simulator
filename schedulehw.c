//
// 2020-2 Operating System
// CPU Schedule Simulator Homework
// Student Number : B511072
// Name : 박주훈 ( Park Juhun )
//
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>

#define SEED 10

// process states
#define S_IDLE 0			
#define S_READY 1
#define S_BLOCKED 2
#define S_RUNNING 3
#define S_TERMINATE 4

// #define DEBUG 1
// #define LOG 1

int NPROC, NIOREQ, QUANTUM;

struct ioDoneEvent {
	int procid;
	int doneTime;
	int len;
	struct ioDoneEvent *prev;
	struct ioDoneEvent *next;
} ioDoneEventQueue, *ioDoneEvent;

struct process {
	int id;
	int len;		// for queue
	int targetServiceTime;
	int serviceTime;
	int startTime;
	int endTime;
	char state;
	int priority;
	int saveReg0, saveReg1;
	struct process *prev;
	struct process *next;
} *procTable;

struct process idleProc;
struct process readyQueue;
struct process blockedQueue;
struct process *runningProc;

int cpuReg0, cpuReg1;
int currentTime = 0;
int *procIntArrTime, *procServTime, *ioReqIntArrTime, *ioServTime;

void compute() {
	// DO NOT CHANGE THIS FUNCTION
	cpuReg0 = cpuReg0 + runningProc->id;
	cpuReg1 = cpuReg1 + runningProc->id;
	// printf("In computer proc %d cpuReg0 %d\n",runningProc->id,cpuReg0);
}

void initProcTable() {
	int i;
	for(i=0; i < NPROC; i++) {
		procTable[i].id = i;
		procTable[i].len = 0;
		procTable[i].targetServiceTime = procServTime[i];
		procTable[i].serviceTime = 0;
		procTable[i].startTime = 0;
		procTable[i].endTime = 0;
		procTable[i].state = S_IDLE;
		procTable[i].priority = 0;
		procTable[i].saveReg0 = 0;
		procTable[i].saveReg1 = 0;
		procTable[i].prev = NULL;
		procTable[i].next = NULL;
	}
}

void procExecSim(struct process *(*scheduler)()) {
	int pid, qTime=0, cpuUseTime = 0, nproc=0, termProc = 0, nioreq=0;
	char schedule = 0, nextState = S_IDLE;
	int nextForkTime, nextIOReqTime;
	
	nextForkTime = procIntArrTime[nproc];
	nextIOReqTime = ioReqIntArrTime[nioreq];

	idleProc.id = -1;
	runningProc = &idleProc;	// initial: idle process
	pid = 0;

	while(1) {
		currentTime++;
		qTime++;
		runningProc->serviceTime++;
		if (runningProc != &idleProc ) {
			cpuUseTime++;
			// MUST CALL compute() Inside While loop
			compute();
			// early save of register state
			runningProc->saveReg0 = cpuReg0;
			runningProc->saveReg1 = cpuReg1;
		}

		#ifdef LOG
			printf("[[cTime:%d, pid:%d, qTime:(%d/%d), ST:(%d/%d)]]\n", 
				currentTime, runningProc->id, qTime, QUANTUM, runningProc->serviceTime, runningProc->targetServiceTime);
			printf("[[cpuUseTime:%d, nextIOReqTime:%d, nextForkTime:%d]]\n", cpuUseTime, nextIOReqTime, nextForkTime);
		#endif
		
		if (currentTime == nextForkTime) { /* CASE 2 : a new process created */
			
			struct process* forked;
			struct process* i;	// pointer for traversing ready queue

			pid = nproc;
			nproc++;
			nextForkTime += procIntArrTime[nproc];	// update next fork time
			
			// find tail of ready queue
			i = &readyQueue;
			while (i->next != &readyQueue)
				i = i->next;
			
			// fork and set startTime
			forked = &procTable[pid];
			forked->startTime = currentTime;
			// push into ready queue
			i->next = forked;
			forked->prev = i;
			// keep circular queue structure
			forked->next = &readyQueue;
			readyQueue.prev = forked;

			forked->state = S_READY;
			readyQueue.len++;

			nextState = S_READY;	// mark next state of current running process

			schedule = 1;	//	set schedule flag on
			#ifdef LOG
				printf("%d-th process forked and passed to ready queue\n", pid);
			#endif
		}
		if (qTime == QUANTUM ) { /* CASE 1 : The quantum expires */

			if (runningProc != &idleProc){
				nextState = S_READY;	// mark next state of current running process
				schedule = 1;	//	set schedule flag on
			}
			#ifdef LOG
				printf("%d-th process quantum expired\n", runningProc->id);
			#endif
		}
		while (ioDoneEventQueue.next->doneTime == currentTime) { /* CASE 3 : IO Done Event */
			
			struct ioDoneEvent* doneEvent;
			struct process* targetProc;
			struct process* i;	// pointer for traversing ready queue

			pid = ioDoneEventQueue.next->procid;
			targetProc = &procTable[pid];
			#ifdef LOG
				printf("IO Done for %d-th process\n", pid);
				printf("%d-th process state: %d\n", pid, targetProc->state);
			#endif
			// pop from ioDoneEventQueue, loop condition changes here
			doneEvent = ioDoneEventQueue.next;
			ioDoneEventQueue.next = doneEvent->next;	//	if doneEvent was last, doneEvent->next is &ioDoneEventQueue
			(ioDoneEventQueue.next)->prev = &ioDoneEventQueue;
			ioDoneEventQueue.len--;

			if (targetProc->state == S_BLOCKED){	// if not terminated
				// find tail of ready queue
				i = &readyQueue;
				while (i->next != &readyQueue)
					i = i->next;
				// push into ready queue
				i->next = targetProc;
				targetProc->prev = i;
				// keep circular queue structure
				targetProc->next = &readyQueue;
				readyQueue.prev = targetProc;

				targetProc->state = S_READY;
				readyQueue.len++;
				#ifdef LOG
					printf("blocked %d-th process passed to ready queue\n", targetProc->id);
				#endif
			}

			nextState = S_READY;	// mark next state of current running process

			schedule = 1;	//	set schedule flag on
		}
		if (cpuUseTime == nextIOReqTime && runningProc != &idleProc) { /* CASE 5: reqest IO operations (only when the process does not terminate) */
			// it works only when running process exist
			
			struct ioDoneEvent *event, *temp;
			struct ioDoneEvent* i;	// pointer for traversing ioDoneEventQueue

			event = &ioDoneEvent[nioreq];
			event->procid = runningProc->id;
			event->doneTime = currentTime + ioServTime[nioreq];
			#ifdef LOG
				printf("IO request occurs for %d-th process, will done at %d\n", event->procid, event->doneTime);
			#endif
			/* when insert, considering order ascending with doneTime
			inserted timing order is guaranteed within same doneTime */
			// find insertion point
			i = &ioDoneEventQueue;
			while ((i->next)->doneTime <= event->doneTime)
				i = i->next;
			// insert into ioDoneEventQueue
			temp = i->next;
			i->next = event;
			event->prev = i;
			event->next = temp;
			temp->prev = event;
			ioDoneEventQueue.len++;

			nextState = S_BLOCKED;	// mark next state of current running process

			schedule = 1;	//	set schedule flag on

			nioreq++;
			nextIOReqTime += ioReqIntArrTime[nioreq];	// update nextIOReqTime
			#ifdef LOG
				printf("next IOReqTime: %d\n", nextIOReqTime);
			#endif
		}
		if (runningProc->serviceTime == runningProc->targetServiceTime) { /* CASE 4 : the process job done and terminates */
			// idle process avoid this if condition (idleProc.sT > 0, idleProc.tST == 0)
			runningProc->endTime = currentTime;

			nextState = S_TERMINATE;	// mark next state of current running process
			termProc++;

			schedule = 1;	//	set schedule flag on
			#ifdef LOG
				printf("%d-th process terminated (%d/%d)\n", runningProc->id, termProc, nproc);
			#endif
		}

		if (nproc == termProc && nproc == NPROC)	// if all processes are terminated
			break;	// just ignore remained IO events. They doesn't affect to printResult anymore.

		if (schedule){	// when scehdule flag is on
			struct process* i;

			// change into nextState
			runningProc->state = nextState;

			switch (nextState){
			case 1: // S_READY
			{
				if (runningProc != &idleProc){
					// find tail of ready queue
					i = &readyQueue;
					while (i->next != &readyQueue)
						i = i->next;
					// push into ready queue
					i->next = runningProc;
					runningProc->prev = i;
					// keep circular queue structure
					runningProc->next = &readyQueue;
					readyQueue.prev = runningProc;
					readyQueue.len++;
					#ifdef LOG
						printf("running %d-th process passed to ready queue\n", runningProc->id);
					#endif
				}
				break;
			}
			case 2: // S_BLOCKED
				// block queue is not needed, being able to handle with ioDoneEventQueue-procid and process state
				#ifdef LOG
					printf("running %d-th process is blocked\n", runningProc->id);
				#endif
				break;
			case 4: // S_TERMINATE
				// additional processing is not needed, changing state above is enough
				break;
			}
			#ifdef LOG
				printf("scheduler called\n");
			#endif
			// get new running process
			runningProc = scheduler();
			runningProc->state = S_RUNNING;
			// restore register state
			cpuReg0 = runningProc->saveReg0;
			cpuReg1 = runningProc->saveReg1;

			//reset variables
			qTime = 0;
			schedule = 0;
			nextState = S_IDLE;
		}

		// 각종 flag들 초기화 필요
		// 퀀텀, ioreq 표시하는 플래그 만들어서 priority 처리 하기
		// 주석 정리 필요
		
	} // while loop
}

//RR,SJF(Modified),SRTN,Guaranteed Scheduling(modified),Simple Feed Back Scheduling
struct process* RRschedule() {

	struct process* selected;

	// if ready queue is empty, return idle process
	if (readyQueue.next == &readyQueue)
		return &idleProc;

	// pop from ready queue
	selected = readyQueue.next;
	readyQueue.next = selected->next;	// if selected was last, selected->next is &readyQueue
	(readyQueue.next)->prev = &readyQueue;
	readyQueue.len--;
	
	return selected;
}
struct process* SJFschedule() {

	struct process* selected;

	return selected;
}
struct process* SRTNschedule() {

	struct process* selected;

	return selected;
}
struct process* GSschedule() {

	struct process* selected;

	return selected;
}
struct process* SFSschedule() {

	struct process* selected;

	return selected;
}

void printResult() {
	// DO NOT CHANGE THIS FUNCTION
	int i;
	long totalProcIntArrTime=0,totalProcServTime=0,totalIOReqIntArrTime=0,totalIOServTime=0;
	long totalWallTime=0, totalRegValue=0;
	for(i=0; i < NPROC; i++) {
		totalWallTime += procTable[i].endTime - procTable[i].startTime;
		/*
		printf("proc %d serviceTime %d targetServiceTime %d saveReg0 %d\n",
			i,procTable[i].serviceTime,procTable[i].targetServiceTime, procTable[i].saveReg0);
		*/
		totalRegValue += procTable[i].saveReg0+procTable[i].saveReg1;
		/* printf("reg0 %d reg1 %d totalRegValue %d\n",procTable[i].saveReg0,procTable[i].saveReg1,totalRegValue);*/
	}
	for(i = 0; i < NPROC; i++ ) { 
		totalProcIntArrTime += procIntArrTime[i];
		totalProcServTime += procServTime[i];
	}
	for(i = 0; i < NIOREQ; i++ ) { 
		totalIOReqIntArrTime += ioReqIntArrTime[i];
		totalIOServTime += ioServTime[i];
	}
	
	printf("Avg Proc Inter Arrival Time : %g \tAverage Proc Service Time : %g\n", (float) totalProcIntArrTime/NPROC, (float) totalProcServTime/NPROC);
	printf("Avg IOReq Inter Arrival Time : %g \tAverage IOReq Service Time : %g\n", (float) totalIOReqIntArrTime/NIOREQ, (float) totalIOServTime/NIOREQ);
	printf("%d Process processed with %d IO requests\n", NPROC,NIOREQ);
	printf("Average Wall Clock Service Time : %g \tAverage Two Register Sum Value %g\n", (float) totalWallTime/NPROC, (float) totalRegValue/NPROC);
	
}

int main(int argc, char *argv[]) {
	// DO NOT CHANGE THIS FUNCTION
	int i;
	int totalProcServTime = 0, ioReqAvgIntArrTime;
	int SCHEDULING_METHOD, MIN_INT_ARRTIME, MAX_INT_ARRTIME, MIN_SERVTIME, MAX_SERVTIME, MIN_IO_SERVTIME, MAX_IO_SERVTIME, MIN_IOREQ_INT_ARRTIME;
	
	if (argc < 12) {
		printf("%s: SCHEDULING_METHOD NPROC NIOREQ QUANTUM MIN_INT_ARRTIME MAX_INT_ARRTIME MIN_SERVTIME MAX_SERVTIME MIN_IO_SERVTIME MAX_IO_SERVTIME MIN_IOREQ_INT_ARRTIME\n",argv[0]);
		exit(1);
	}
	
	SCHEDULING_METHOD = atoi(argv[1]);
	NPROC = atoi(argv[2]);
	NIOREQ = atoi(argv[3]);
	QUANTUM = atoi(argv[4]);
	MIN_INT_ARRTIME = atoi(argv[5]);
	MAX_INT_ARRTIME = atoi(argv[6]);
	MIN_SERVTIME = atoi(argv[7]);
	MAX_SERVTIME = atoi(argv[8]);
	MIN_IO_SERVTIME = atoi(argv[9]);
	MAX_IO_SERVTIME = atoi(argv[10]);
	MIN_IOREQ_INT_ARRTIME = atoi(argv[11]);
	
	printf("SIMULATION PARAMETERS : SCHEDULING_METHOD %d NPROC %d NIOREQ %d QUANTUM %d \n", SCHEDULING_METHOD, NPROC, NIOREQ, QUANTUM);
	printf("MIN_INT_ARRTIME %d MAX_INT_ARRTIME %d MIN_SERVTIME %d MAX_SERVTIME %d\n", MIN_INT_ARRTIME, MAX_INT_ARRTIME, MIN_SERVTIME, MAX_SERVTIME);
	printf("MIN_IO_SERVTIME %d MAX_IO_SERVTIME %d MIN_IOREQ_INT_ARRTIME %d\n", MIN_IO_SERVTIME, MAX_IO_SERVTIME, MIN_IOREQ_INT_ARRTIME);
	
	srandom(SEED);
	
	// allocate array structures
	procTable = (struct process *) malloc(sizeof(struct process) * NPROC);
	ioDoneEvent = (struct ioDoneEvent *) malloc(sizeof(struct ioDoneEvent) * NIOREQ);
	procIntArrTime = (int *) malloc(sizeof(int) * NPROC);
	procServTime = (int *) malloc(sizeof(int) * NPROC);
	ioReqIntArrTime = (int *) malloc(sizeof(int) * NIOREQ);
	ioServTime = (int *) malloc(sizeof(int) * NIOREQ);

	// initialize queues
	readyQueue.next = readyQueue.prev = &readyQueue;
	
	blockedQueue.next = blockedQueue.prev = &blockedQueue;
	ioDoneEventQueue.next = ioDoneEventQueue.prev = &ioDoneEventQueue;
	ioDoneEventQueue.doneTime = INT_MAX;
	ioDoneEventQueue.procid = -1;
	ioDoneEventQueue.len  = readyQueue.len = blockedQueue.len = 0;
	
	// generate process interarrival times
	for(i = 0; i < NPROC; i++ ) { 
		procIntArrTime[i] = random()%(MAX_INT_ARRTIME - MIN_INT_ARRTIME+1) + MIN_INT_ARRTIME;
	}
	
	// assign service time for each process
	for(i=0; i < NPROC; i++) {
		procServTime[i] = random()% (MAX_SERVTIME - MIN_SERVTIME + 1) + MIN_SERVTIME;
		totalProcServTime += procServTime[i];	
	}
	
	ioReqAvgIntArrTime = totalProcServTime/(NIOREQ+1);
	assert(ioReqAvgIntArrTime > MIN_IOREQ_INT_ARRTIME);
	
	// generate io request interarrival time
	for(i = 0; i < NIOREQ; i++ ) { 
		ioReqIntArrTime[i] = random()%((ioReqAvgIntArrTime - MIN_IOREQ_INT_ARRTIME)*2+1) + MIN_IOREQ_INT_ARRTIME;
	}
	
	// generate io request service time
	for(i = 0; i < NIOREQ; i++ ) { 
		ioServTime[i] = random()%(MAX_IO_SERVTIME - MIN_IO_SERVTIME+1) + MIN_IO_SERVTIME;
	}
	
#ifdef DEBUG
	// printing process interarrival time and service time
	printf("Process Interarrival Time :\n");
	for(i = 0; i < NPROC; i++ ) { 
		printf("%d ",procIntArrTime[i]);
	}
	printf("\n");
	printf("Process Target Service Time :\n");
	for(i = 0; i < NPROC; i++ ) { 
		printf("%d ",procTable[i].targetServiceTime);
	}
	printf("\n");
#endif
	
	// printing io request interarrival time and io request service time
	printf("IO Req Average InterArrival Time %d\n", ioReqAvgIntArrTime);
	printf("IO Req InterArrival Time range : %d ~ %d\n",MIN_IOREQ_INT_ARRTIME,
			(ioReqAvgIntArrTime - MIN_IOREQ_INT_ARRTIME)*2+ MIN_IOREQ_INT_ARRTIME);
			
#ifdef DEBUG		
	printf("IO Req Interarrival Time :\n");
	for(i = 0; i < NIOREQ; i++ ) { 
		printf("%d ",ioReqIntArrTime[i]);
	}
	printf("\n");
	printf("IO Req Service Time :\n");
	for(i = 0; i < NIOREQ; i++ ) { 
		printf("%d ",ioServTime[i]);
	}
	printf("\n");
#endif
	
	struct process* (*schFunc)();
	switch(SCHEDULING_METHOD) {
		case 1 : schFunc = RRschedule; break;
		case 2 : schFunc = SJFschedule; break;
		case 3 : schFunc = SRTNschedule; break;
		case 4 : schFunc = GSschedule; break;
		case 5 : schFunc = SFSschedule; break;
		default : printf("ERROR : Unknown Scheduling Method\n"); exit(1);
	}
	initProcTable();
	procExecSim(schFunc);
	printResult();

}