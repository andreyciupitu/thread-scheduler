/*
 * Ciupitu Andrei Valentin
 * 332CC
 */

#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>

#include "_test/so_scheduler.h"
#include "utils.h"

#define CAPACITY 1000

typedef enum {
	BLOCKED,
	READY,
	RUNNING,
	NEW,
	TERMINATED
} thread_status;

/**
 *  Thread struct
 */
typedef struct {
	/*
	 * Thread parameters
	 */
	tid_t tid;
	thread_status status;
	unsigned int time_left;
	unsigned int priority;
	so_handler *handler;
	unsigned int device;

	/*
	 * Synchronization stuff
	 */
	sem_t run;
} thread;

/**
 *  Scheduler struct
 */
typedef struct {
	int initialized;
	unsigned int events;
	unsigned int quantum;
	thread *running;

	/*
	 * Thread queue
	 */
	unsigned int num_threads;
	unsigned int queue_size;
	sem_t end;
	thread *queue[CAPACITY];
	thread *threads[CAPACITY];
} scheduler;

static scheduler s;

static void *thread_routine(void *args);
static void update_scheduler(void);
static void init_thread(thread *t, so_handler *f, unsigned int priority);
static void destroy_thread(thread *t);
static void start_thread(thread *t);
static void register_thread(thread *t);

int so_init(unsigned int time_quantum, unsigned int io)
{
	int ret;

	/*
	 * Initialized be zero first cause its global
	 */
	if (s.initialized)
		return -1;

	/*
	 * Parameter check
	 */
	if (time_quantum <= 0 || io > SO_MAX_NUM_EVENTS)
		return -2;

	s.initialized = 1;
	s.events = io;
	s.quantum = time_quantum;
	s.num_threads = 0;
	s.queue_size = 0;
	s.running = NULL;

	/*
	 * Init end sem
	 */
	ret = sem_init(&s.end, 0, 1);
	DIE(ret != 0, "end sem init");
	return 0;
}

tid_t so_fork(so_handler *func, unsigned int priority)
{
	int ret;
	thread *t;

	if (func == NULL || priority > SO_MAX_PRIO)
		return INVALID_TID;

	t = malloc(sizeof(thread));
	DIE(t == NULL, "malloc error");

	if (s.num_threads == 0) {
		ret = sem_wait(&s.end);
		DIE(ret != 0, "sem wait");
	}

	/*
	 * Init the thread struct
	 */
	init_thread(t, func, priority);

	/*
	 * Register the new thread
	 */
	s.threads[s.num_threads++] = t;
	register_thread(t);

	/*
	 * Call the scheduler
	 */
	if (s.running != NULL)
		so_exec();
	else
		update_scheduler();

	return t->tid;
}

void so_exec(void)
{
	int ret;
	thread *t;

	t = s.running;
	t->time_left--;

	/*
	 * Call the scheduler
	 */
	update_scheduler();

	/*
	 * Wait if the thread gets preeempted
	 */
	ret = sem_wait(&t->run);
	DIE(ret != 0, "sem_wait");
}

int so_wait(unsigned int io)
{
	if (io < 0 || io >= s.events)
		return -1;

	s.running->status = BLOCKED;
	s.running->device = io;

	so_exec();
	return 0;
}

int so_signal(unsigned int io)
{
	int i;
	int count;

	if (io < 0 || io >= s.events)
		return -1;

	count = 0;
	for (i = 0; i < s.num_threads; i++)
		if (s.threads[i]->device == io
				&& s.threads[i]->status == BLOCKED) {
			s.threads[i]->device = SO_MAX_NUM_EVENTS;
			s.threads[i]->status = READY;
			register_thread(s.threads[i]);
			count++;
		}

	so_exec();
	return count;
}

void so_end(void)
{
	int i;
	int ret;

	if (!s.initialized)
		return;

	ret = sem_wait(&s.end);
	DIE(ret != 0, "sem wait");

	/*
	 * Wait for threads to finish then destroy them
	 */
	for (i = 0; i < s.num_threads; i++) {
		ret = pthread_join(s.threads[i]->tid, NULL);
		DIE(ret != 0, "thread join");
	}

	for (i = 0; i < s.num_threads; i++)
		destroy_thread(s.threads[i]);

	/*
	 * Destroy scheduler
	 */
	s.initialized = 0;
	ret = sem_destroy(&s.end);
	DIE(ret != 0, "end sem destroy");
}

/**
 *  @brief Funtion that initializes new threads
 *
 *  @param [in] t        Pointer to a thread struct
 *  @param [in] f        Handler executed by the new thread
 *  @param [in] priority Priority of the new thread
 */
static void init_thread(thread *t, so_handler *f, unsigned int priority)
{
	int ret;

	t->tid = INVALID_TID;
	t->status = NEW;
	t->time_left = s.quantum;
	t->priority = priority;
	t->handler = f;
	t->device = SO_MAX_NUM_EVENTS;

	/*
	 * Init semaphore
	 */
	ret = sem_init(&t->run, 0, 0);
	DIE(ret != 0, "sem init");

	/*
	 * Start thread
	 */
	ret = pthread_create(&t->tid, NULL, &thread_routine, (void *)t);
	DIE(ret != 0, "thread create");
}

/**
 *  @brief Destroys a thread struct and frees memory
 *
 *  @param [in] t Pointer to a thread struct
 */
static void destroy_thread(thread *t)
{
	int ret;

	ret = sem_destroy(&t->run);
	DIE(ret != 0, "sem destroy");

	free(t);
}

/**
 *  @brief Signals a thread to run
 *
 *  @param [in] t Pointer to the thread struct
 */
static void start_thread(thread *t)
{
	int ret;

	s.queue[--s.queue_size] = NULL;
	t->status = RUNNING;
	t->time_left = s.quantum;
	ret = sem_post(&t->run);
	DIE(ret != 0, "sem_post");
}

/**
 *  @brief Adds a thread to the thread queue
 *
 *  @param [in] t Pointer to a thread structure
 */
static void register_thread(thread *t)
{
	int i, j;

	/*
	 * Search for the correct position to insert the thread
	 */
	i = 0;
	while (i < s.queue_size && s.queue[i]->priority < t->priority)
		i++;

	/*
	 * Make space to insert
	 */
	for (j = s.queue_size; j > i; j--)
		s.queue[j] = s.queue[j - 1];

	s.queue_size++;
	s.queue[i] = t;
	s.queue[i]->status = READY;
}

/**
 *  @brief Updates the scheduler, and makes it plan the next thread
 */
static void update_scheduler(void)
{
	int ret;
	thread *next;
	thread *current;

	current = s.running;
	if (s.queue_size == 0) {
		/*
		 * Can stop scheduler if the last thread finished
		 */
		if (current->status == TERMINATED) {
			ret = sem_post(&s.end);
			DIE(ret != 0, "sem post");
		}
		
		/*
		 * No other thread to run
		 */
		sem_post(&current->run);
		return;
	}

	/*
	 * Get first thread in queue
	 */
	next = s.queue[s.queue_size - 1];

	/*
	 * If no thread is running
	 */
	if (s.running == NULL) {
		s.running = next;
		start_thread(next);
		return;
	}

	/*
	 * Check if thread is blocked or finished
	 */
	if (current->status == BLOCKED || current->status == TERMINATED) {
		s.running = next;
		start_thread(next);
		return;
	}

	/*
	 * Check if there is a higher priority thread available
	 */
	if (current->priority < next->priority) {
		register_thread(current);
		s.running = next;
		start_thread(next);
		return;
	}

	/*
	 * Check if the thread's quantum has expired
	 */
	if (current->time_left <= 0) {
		/*
		 * Round robin
		 */
		if (current->priority == next->priority) {
			register_thread(current);
			s.running = next;
			start_thread(next);
			return;
		}
		current->time_left = s.quantum;
	}

	sem_post(&current->run);
}

/**
 *  @brief Routine used by every new thread
 *
 *  @param [in] args Void pointer to a thread struct
 */
static void *thread_routine(void *args)
{
	int ret;
	thread *t;

	t = (thread *)args;

	/*
	 * Wait for scheduler
	 */
	ret = sem_wait(&t->run);
	DIE(ret != 0, "sem wait");

	/*
	 * Run handler
	 */
	t->handler(t->priority);

	/*
	 * Announce scheduler that the thread is finished
	 */
	t->status = TERMINATED;
	update_scheduler();

	return NULL;
}
