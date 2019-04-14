#!/bin/bash

make clean
make


echo "--------------------------------------------------------------------------------------------------------"
echo "Execution times for all locks"
echo "--------------------------------------------------------------------------------------------------------"
for threads in 2 4 8 16 24 32 48 64 96
do
	./lock -num_threads $threads
done

echo "--------------------------------------------------------------------------------------------------------"
echo "Execution times for all locks threads fixed to CPU"
echo "--------------------------------------------------------------------------------------------------------"
for threads in 2 4 8 16 24 32 48 64 96
do
	./lock -num_threads $threads -fix_cpu 1
done


echo "--------------------------------------------------------------------------------------------------------"
echo "Execution times for spin locks with different delay configurations while getting a lock"
echo "--------------------------------------------------------------------------------------------------------"
for current_delay in 0 500 -1
do
	echo "Delay = $current_delay"
	for threads in 2 4 8 16 24 32 48 64 96
	do
		./lock -num_threads $threads -delay $current_delay -lock_type 1
	done
done
