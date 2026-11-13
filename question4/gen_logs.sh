#!/bin/sh
# Generates 12 log files of different sizes (1 MB to 50 MB, 209 MB in
# total) in the folder given as argument.
#
#   ./gen_logs.sh /tmp/logscan_lab
#
# The lines are random but repeatable: the file number is the seed.
set -e

dir=${1:-logs}
mkdir -p "$dir"

i=1
for mb in 50 1 30 2 40 3 25 5 20 8 15 10
do
	name=$(printf "%s/app%02d.log" "$dir" $i)
	awk -v seed=$i -v bytes=$((mb * 1024 * 1024)) 'BEGIN {
		srand(seed)
		split("INFO INFO INFO INFO DEBUG DEBUG WARN ERROR", level, " ")
		split("auth db cache api scheduler", service, " ")
		while (written < bytes)
		{
			line = sprintf("2025-11-%02d %02d:%02d:%02d [%s] %s: request %d finished in %d ms",
				1 + int(rand() * 28), int(rand() * 24),
				int(rand() * 60), int(rand() * 60),
				level[1 + int(rand() * 8)],
				service[1 + int(rand() * 5)],
				int(rand() * 1000000), int(rand() * 5000))
			print line
			written += length(line) + 1
		}
	}' > "$name"
	i=$((i + 1))
done
ls -l "$dir"
