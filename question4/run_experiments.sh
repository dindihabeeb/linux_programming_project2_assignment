#!/bin/sh
# Builds logscan, generates the log files and runs the whole experiment.
# Run on GNU/Linux from the question4 folder.
#
#   results/results.md      table: 3 timed runs, average, speedup
#   results/runs.txt        every timed run
#   results/report_<n>.txt  the report file written with n threads
#   results/verify.txt      total of logscan compared with grep -c
#   results/errors.txt      what happens with a missing file
set -e

gcc -Wall -Wextra -Werror -pedantic -std=gnu89 -pthread -o logscan logscan.c

here=$(pwd)
out=$here/results
lab=/tmp/logscan_lab
pattern=ERROR
rm -rf "$lab" "$out"
mkdir -p "$out"
sh ./gen_logs.sh "$lab" > "$out/files.txt"
cp logscan "$lab"
cd "$lab"

cat app*.log > /dev/null	# so every run reads the files from the page cache

{
	echo "| Threads | Run 1 (s) | Run 2 (s) | Run 3 (s) | Average (s) | Speedup (T1 / Tn) |"
	echo "|---:|---:|---:|---:|---:|---:|"
} > "$out/results.md"

echo "CPUs: $(nproc)" > "$out/runs.txt"
t1=""
for n in 1 2 4 8
do
	times=""
	for run in 1 2 3
	do
		t=$(./logscan $pattern report.txt $n app*.log | awk '/Elapsed/ {print $(NF - 1)}')
		echo "$n threads, run $run: $t s" | tee -a "$out/runs.txt"
		times="$times $t"
	done
	cp report.txt "$out/report_$n.txt"
	avg=$(echo $times | awk '{printf "%.4f", ($1 + $2 + $3) / 3}')
	[ -n "$t1" ] || t1=$avg
	echo $times | awk -v n=$n -v a=$avg -v t1=$t1 \
		'{printf "| %d | %.3f | %.3f | %.3f | **%.3f** | %.2f |\n", n, $1, $2, $3, a, t1 / a}' >> "$out/results.md"
done

{
	echo "\$ grep -c $pattern app*.log | awk -F: '{sum += \$2} END {print sum}'"
	grep -c $pattern app*.log | awk -F: '{sum += $2} END {print sum}'
	echo "\$ wc -l app*.log | tail -n 1"
	wc -l app*.log | tail -n 1
	for n in 1 2 4 8
	do
		echo "\$ tail -n 1 report_$n.txt"
		tail -n 1 "$out/report_$n.txt"
	done
} | tee "$out/verify.txt"

{
	echo "\$ ./logscan $pattern report.txt 2 app02.log missing.log app04.log"
	./logscan $pattern report.txt 2 app02.log missing.log app04.log 2>&1
	echo "exit status: $?"
	echo '$ cat report.txt'
	cat report.txt
} > "$out/errors.txt"

cd "$here"
rm -rf "$lab"
cat "$out/results.md"
