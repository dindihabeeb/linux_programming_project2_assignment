#!/bin/sh
# Builds pipeline and runs it three times. Run on GNU/Linux from the
# question4 folder.
#
#   output.txt                  what the program prints
#   traces/trace.log            full trace:  strace -f -tt
#   traces/trace_filtered.log   only the process, pipe and file calls
#
# In the filter, a leading "?" means "skip this name if this CPU does not
# have it": x86-64 has fork/pipe/dup2/open, AArch64 only has the newer
# clone/pipe2/dup3/openat.
set -e

gcc -Wall -Wextra -Werror -pedantic -std=gnu89 -o pipeline pipeline.c
mkdir -p traces

echo "--- Normal run ---"
./pipeline | tee output.txt

echo "--- Full trace ---"
strace -f -tt -o traces/trace.log ./pipeline > /dev/null

echo "--- Filtered trace ---"
strace -f -y \
	-e trace=clone,?clone3,?fork,execve,?pipe,pipe2,?dup2,dup3,?open,openat,read,write,close,wait4,exit_group \
	-o traces/trace_filtered.log ./pipeline > /dev/null

echo "--- Check against the shell ---"
cat /etc/passwd | cut -d: -f1 | sort | cmp - result.txt && echo "result.txt matches the shell pipeline"
wc -l traces/trace.log traces/trace_filtered.log
