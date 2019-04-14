lock: lock.c
	gcc -O3 lock.c -o lock -lpthread -lm

clean:
	rm lock
