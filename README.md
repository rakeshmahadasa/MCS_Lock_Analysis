# MCS_Lock_Analysis
- Spin Lock
- Pthread Lock
- Ticket Lock
- MCS lock

Compile with the provided Makefile:
	make

Run it with:
	./lock

Commandline options:
	-num_threads: Number of threads to run (default 8)
	-lock_type: (default 5)
			0: Pthread spinlock
			1: Dumb spinlock
			2: Modified spinlock
			3: Ticket lock
			4: MCS lock
			5: All locks
	-delay: Backoff delay (applicable to spinlock only, default 5)
	-fix_cpu: Fix the threads to CPUs (default 0)
	-runs: Number of runs (default 5)

Macros:
TOTAL_WORK_TO_BE_DONE: Number of times the lock is acquired and released in total
ENABLE_FINE_GRAINED_TIMER_LOGS: Enable measurement of cumulative lock time for each thread

Use the runnerScript.sh file to get runs for 2, 4, 8, 16, 24, 32, 48, 64 and 96 threads.

We used gcc version 7.3.0 on an Ubuntu 18.04 machine with a 4.15 kernel. It should work for other gcc versions on other versions of GNU/Linux as well.
