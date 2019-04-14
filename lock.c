#define _GNU_SOURCE
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "limits.h"

#define CACHE_LINE 64
#define TOTAL_WORK_TO_BE_DONE 1048576*4
#define RAND_WAIT_TIME_ARRAY_LENGTH 128
#define RAND_WAIT_TIME_UPPER_LIMIT 1000
#define ENABLE_FINE_GRAINED_TIMER_LOGS 0
#define ENABLE_MAX_SPIN_COMPUTATION 0
#define YIELD_THREAD 0
#define ARRAY_SIZE(_a) (sizeof(_a) / sizeof(_a[0]))
#define TIMESPEC_DIFF(_b, _e) \
			(((_e).tv_sec - (_b).tv_sec) * 1000000 + \
			((_e).tv_nsec - (_b).tv_nsec) / 1000)


#if ENABLE_MAX_SPIN_COMPUTATION
unsigned long long max_spins = 0;
#endif
// A global variable accessed in the critical section
int global_var = 0;
// Vary this variable to increase the amount of instructions in the critical section
volatile int critical_section_time = 128;
int padding[CACHE_LINE] __attribute__((aligned(CACHE_LINE)));

// Cumulative time taken by each thread to obtain a lock 
#if ENABLE_FINE_GRAINED_TIMER_LOGS
	unsigned long long *lock_times;
#endif
// Array of random numbers from 0 to RAND_WAIT_TIME_UPPER_LIMIT. This is used for specifying the number of iterations in the busy loop. This is used only in certain experiments for spinlocks.
int rand_delay[RAND_WAIT_TIME_ARRAY_LENGTH];
int padding2[CACHE_LINE] __attribute__((aligned(CACHE_LINE)));


pthread_spinlock_t pthread_global_lock;

static inline void thread_wait(void)
{
#if YIELD_THREAD
	pthread_yield();
#else
	asm volatile("pause");
#endif
}

struct spinlock {
	unsigned int locked __attribute__((aligned(CACHE_LINE)));
	int owner;
};

struct spinlock global_spinlock = {
	.owner = 0,
	.locked = 0,
};

void spinlock_dumb_lock(struct spinlock *spin_lock, int owner)
{
	unsigned int prev;
	unsigned long long count = 0;
	unsigned int val = 0;

	while (!__atomic_compare_exchange_n(&spin_lock->locked, &val, 1, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
	{
#if ENABLE_MAX_SPIN_COMPUTATION
		count++;
#endif
		thread_wait();
		val = 0;
	}

	spin_lock->owner = owner;
#if ENABLE_MAX_SPIN_COMPUTATION
	if (count > max_spins)
		max_spins = count;
#endif

}

void spinlock_lock(struct spinlock *spin_lock, int owner)
{
	unsigned int prev;
	unsigned long long count = 0;
	unsigned int val = 0;

	while (!__atomic_compare_exchange_n(&spin_lock->locked, &val, 1, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
	{
		do
		{
			count++;
			thread_wait();
			// Each thread busy loops locally for a random/zero iterations before checking the lock again
			if (rand_delay[(count+owner) % RAND_WAIT_TIME_ARRAY_LENGTH]) {
				volatile int temp = rand_delay[count % RAND_WAIT_TIME_ARRAY_LENGTH];
				for(int j=0; j<temp; j++);
			}
			val = __atomic_load_n (&spin_lock->locked, __ATOMIC_RELAXED);
		} while (val != 0);
	}

	spin_lock->owner = owner;
#if ENABLE_MAX_SPIN_COMPUTATION
	if (count > max_spins)
		max_spins = count;
#endif

}

void spinlock_unlock(struct spinlock *spin_lock)
{
	spin_lock->owner = 0;
	__atomic_store_n(&spin_lock->locked, 0, __ATOMIC_RELEASE);
}

struct ticketlock {
	unsigned int current __attribute__((aligned(CACHE_LINE)));
	int owner;
	unsigned int last __attribute__((aligned(CACHE_LINE)));
};

struct ticketlock global_ticketlock = {
	.current = 0,
	.owner = 0,
	.last = 0,
};

void ticketlock_lock(struct ticketlock *ticket_lock, int owner)
{
	unsigned int ticket;
#if ENABLE_MAX_SPIN_COMPUTATION
	unsigned long long count;
#endif

	ticket = __atomic_fetch_add(&ticket_lock->last, 1, __ATOMIC_RELAXED);
	while (__atomic_load_n(&ticket_lock->current, __ATOMIC_ACQUIRE) !=
			ticket) {
#if ENABLE_MAX_SPIN_COMPUTATION
		count++;
#endif
		thread_wait();
	}
#if ENABLE_MAX_SPIN_COMPUTATION
	if (count > max_spins)
		max_spins = count;
#endif
}

void ticketlock_unlock(struct ticketlock *ticket_lock)
{
	__atomic_fetch_add(&ticket_lock->current, 1, __ATOMIC_RELEASE);
}

struct mcslock {
	unsigned int locked __attribute__((aligned(CACHE_LINE)));
	int owner;
	struct mcslock *next __attribute__((aligned(CACHE_LINE)));
};

struct mcslock global_mcs = {
	.locked = 0,
	.owner = 0,
	.next = NULL,
};

void mcslock_lock(struct mcslock *mcslock,
				struct mcslock *local, int owner)
{
	struct mcslock *prev = NULL;
	unsigned int locked;
#if ENABLE_MAX_SPIN_COMPUTATION
	unsigned long long count;
#endif

	local->locked = 1;
	local->owner = owner;
	local->next = NULL;

	prev = __atomic_exchange_n(&mcslock->next, local,
							__ATOMIC_ACQUIRE);

	if (prev == NULL)
		return;

	__atomic_store_n(&prev->next, local, __ATOMIC_RELEASE);

	do {
		locked = __atomic_load_n(&local->locked, __ATOMIC_ACQUIRE);
		if (!locked)
			break;
#if ENABLE_MAX_SPIN_COMPUTATION
		count++;
#endif
		thread_wait();
	} while (1);
	mcslock->owner = owner;
#if ENABLE_MAX_SPIN_COMPUTATION
	if (count > max_spins)
		max_spins = count;
#endif
	
}

void mcslock_unlock(struct mcslock *mcslock,
				struct mcslock *local)
{
	struct mcslock *next = local;

	mcslock->owner = 0;
	__atomic_compare_exchange_n(&mcslock->next,
		&next, NULL, false,
		__ATOMIC_RELEASE,
		__ATOMIC_RELAXED);

	if (next == local)
		return;

	while (!__atomic_load_n(&local->next, __ATOMIC_RELAXED));

	__atomic_store_n(&local->next->locked, 0, __ATOMIC_RELEASE);
}

struct targ {
	int tid;
	int num;
	cpu_set_t cpuset;
	struct mcslock local_mcs;
};

void pthread_lock(struct targ *t)
{
	(void) t;
	pthread_spin_lock(&pthread_global_lock);
}

void pthread_unlock(struct targ *t)
{
	(void) t;
	pthread_spin_unlock(&pthread_global_lock);
}

void spin_dumb_lock(struct targ *t)
{
	spinlock_dumb_lock(&global_spinlock, t->tid);
}

void spin_lock(struct targ *t)
{
	spinlock_lock(&global_spinlock, t->tid);
}

void spin_unlock(struct targ *t)
{
	(void) t;
	spinlock_unlock(&global_spinlock);
}

void ticket_lock(struct targ *t)
{
	ticketlock_lock(&global_ticketlock, t->tid);
}

void ticket_unlock(struct targ *t)
{
	(void) t;
	ticketlock_unlock(&global_ticketlock);
}

void mcs_lock(struct targ *t)
{
	mcslock_lock(&global_mcs, &t->local_mcs, t->tid);
}

void mcs_unlock(struct targ *t)
{
	mcslock_unlock(&global_mcs, &t->local_mcs);
}

void (*lock)(struct targ *) = pthread_lock;
void (*unlock)(struct targ *) = pthread_unlock;

void *thread_func(void *arg)
{
	struct targ *t = arg;
	int num = t->num;
	int tid = t->tid;
	int i;
#if ENABLE_FINE_GRAINED_TIMER_LOGS		
	struct timespec begin_time;
	struct timespec end_time;
	struct timespec delay_time = { .tv_sec = 0 };
#endif

	for (i = 0; i < num; i++) {
#if ENABLE_FINE_GRAINED_TIMER_LOGS		
		clock_gettime(CLOCK_MONOTONIC, &begin_time);
#endif
		lock(t);
#if ENABLE_FINE_GRAINED_TIMER_LOGS		
		clock_gettime(CLOCK_MONOTONIC, &end_time);
#endif
		for(int j=0; j<critical_section_time; j++);
		global_var++;
		unlock(t);
#if ENABLE_FINE_GRAINED_TIMER_LOGS		
		lock_times[tid - 1] += TIMESPEC_DIFF(begin_time, end_time);
#endif
	}
	return NULL;
}

static int num_threads = 8;
static int lock_type = 5;
static int runs = 5;
static int delay = 0;
static int fix_cpu = 0;


struct cmd_args {
	const char *str;
	int *val;
} valid_args[] = {
	{ "-num_threads", &num_threads, },
	{ "-lock_type", &lock_type, },
	{ "-delay", &delay, },
	{ "-fix_cpu", &fix_cpu, },
	{ "-runs", &runs, },
};

int parse_arg(struct cmd_args *cmd_arg,
			int argc, char *argv[], int pos)
{
	int val;
	int i = 1;

	if (pos + i >= argc)
		return -1;

	val = atoi(argv[pos + i]);
	*cmd_arg->val = val;
	i++;
	return i;
}

int parse_args(int argc, char *argv[])
{
	int i;
	int pos = 1;
	int status = 0;

	while (argc > pos) {
		status = -1;
		for (i = 0; i < ARRAY_SIZE(valid_args); i++) {
			if (!strcmp(valid_args[i].str, argv[pos])) {
				status = parse_arg(&valid_args[i],
								argc, argv, pos);
			}
		}
		if (status < 0)
			return status;
		pos += status;
	}
	return status < 0 ? -1 : 0;
}

void do_computation();
pthread_t *threads;
struct targ *t;

int main(int argc, char *argv[])
{
	int ret;
	ret = parse_args(argc, argv);
	if (ret) {
		printf("Bad args\n");	
		return -1;
	}
	if (num_threads <= 0) {
		printf("Specified %d threads\n", num_threads);
		return -1;
	}
	printf("Command line arguments\n");
	printf("num_threads = %d\n", num_threads);
	printf("lock_type = %d\n", lock_type);
	printf("runs = %d\n", runs);
	printf("delay = %d\n", delay);
	printf("fix_cpu = %d\n", fix_cpu);

	// Allocate dynamic variables
	threads = calloc(num_threads, sizeof(*threads));
	t = calloc(num_threads, sizeof(*t));
#if ENABLE_FINE_GRAINED_TIMER_LOGS
	lock_times = calloc(num_threads, sizeof(*lock_times));
#endif
	switch (lock_type) {
		case 0:
			lock = pthread_lock;
			unlock = pthread_unlock;
			pthread_spin_init(&pthread_global_lock, 0);	
			printf("\nPthread spinlock\n");
			do_computation();
			break;
		case 1:
			lock = spin_dumb_lock;
			unlock = spin_unlock;
			printf("\nDumb spinlock\n");
			do_computation();
			break;
		case 2:
			lock = spin_lock;
			unlock = spin_unlock;
			printf("\nModified spinlock\n");
			do_computation();
			break;
		case 3:
			lock = ticket_lock;
			unlock = ticket_unlock;
			printf("\nTicket lock\n");
			do_computation();
			break;
		case 4:
			lock = mcs_lock;
			unlock = mcs_unlock;
			printf("\nMCS lock\n");
			do_computation();
			break;
		default:
			// p_thread spinlock
			lock = pthread_lock;
			unlock = pthread_unlock;
			pthread_spin_init(&pthread_global_lock, 0);	
			printf("\nPthread spinlock\n");
			do_computation();
			// Dumb spinlock
			lock = spin_dumb_lock;
			unlock = spin_unlock;
			printf("\nDumb spinlock\n");
			do_computation();
			// Custom spinlock
			lock = spin_lock;
			unlock = spin_unlock;
			printf("\nModified spinlock\n");
			do_computation();
			// Ticket lock
			lock = ticket_lock;
			unlock = ticket_unlock;
			printf("\nTicket lock\n");
			do_computation();
			break;
			// MCS lock
			lock = mcs_lock;
			unlock = mcs_unlock;
			printf("\nMCS lock\n");
			do_computation();
	}

	free(threads);
	free(t);
#if ENABLE_FINE_GRAINED_TIMER_LOGS
	free(lock_times);
#endif
	return 0;
}


void do_computation(){
	int current_run, i;	
	struct timespec start, finish;
#if ENABLE_FINE_GRAINED_TIMER_LOGS
	long max_time;
	long min_time;
	long avg_time;
	long sum_time;
	long sum_var;
	long sd;
#endif
	unsigned int seed;
	void *retval;

	for(current_run = 0; current_run < runs; current_run++){
		printf("Run : %d\n", current_run+1);
		// Update the random delays array
		seed = time(NULL);
		srand(seed);
		for (i = 0; i < RAND_WAIT_TIME_ARRAY_LENGTH; i++) {
			if (delay == -1)
				rand_delay[i] = rand() % RAND_WAIT_TIME_UPPER_LIMIT;
			else
				rand_delay[i] = delay;
		}

		for (i = 0; i < num_threads; i++) {
#if ENABLE_FINE_GRAINED_TIMER_LOGS
			lock_times[i] = 0;
#endif
			t[i].tid = i + 1;
			t[i].num = TOTAL_WORK_TO_BE_DONE/num_threads;
			CPU_ZERO(&t[i].cpuset);
			CPU_SET(i, &t[i].cpuset);
		}

		// Start each thread
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (i = 0; i < num_threads; i++) {
			pthread_create(&threads[i], NULL,
						thread_func, &t[i]);
			if (fix_cpu) {
				int s = pthread_setaffinity_np(threads[i],
						sizeof(cpu_set_t), &t[i].cpuset);

				if (s != 0)
					printf("Set affinity for thread %d failed\n", i);
			}
		}

		for (i = 0; i < num_threads; i++) {
			pthread_join(threads[i], &retval);
		}
		clock_gettime(CLOCK_MONOTONIC, &finish);
#if ENABLE_MAX_SPIN_COMPUTATION
		printf("Max spins: %lld\n", max_spins);
#endif
		printf("Time taken : %ld microseconds\n", TIMESPEC_DIFF(start, finish));
#if ENABLE_FINE_GRAINED_TIMER_LOGS
		max_time = 0;
		min_time = 0;
		avg_time = 0;
		sum_time = 0;
		for (i = 0; i < num_threads; i++) {
			if (lock_times[i] > max_time)
				max_time = lock_times[i];
			if ((min_time == 0) || (lock_times[i] < min_time))
				min_time = lock_times[i];
			sum_time += lock_times[i];
		}
		avg_time = sum_time / num_threads;
		sum_var = 0;
		for (i = 0; i < num_threads; i++) {
			sum_var += ((lock_times[i] - avg_time) * (lock_times[i] - avg_time)) / (num_threads - 1);
		}
		sd = sqrt((double) sum_var);
		printf("Std deviation: %ld\n", sd);
#endif
	}	
}

