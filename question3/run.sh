#!/bin/sh
# Builds perfect and runs it. Run on GNU/Linux from the question3 folder.
#
#   results/output.txt   one normal run
#   results/threads.txt  one run with -v: CPU time used by each thread
#   results/runs.txt     the two timing lines of 5 more runs
set -e

gcc -Wall -Wextra -Werror -pedantic -std=gnu89 -pthread -o perfect perfect.c
mkdir -p results

./perfect | tee results/output.txt
./perfect -v > results/threads.txt

echo "CPUs: $(nproc)" > results/runs.txt
for run in 1 2 3 4 5
do
	echo "Run $run" >> results/runs.txt
	./perfect | grep time >> results/runs.txt
done
cat results/threads.txt results/runs.txt
