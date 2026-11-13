#!/bin/sh
# Builds copy_sys and copy_stdio, makes a 100 MB test file and runs the
# whole experiment. Run on GNU/Linux from the question2 folder.
#
#   results/results.md   table: 3 timed runs, average, read/write counts
#   results/runs.txt     every timed run and its cmp check
#   results/verify.txt   sha256sum of the source and of one copy by each
#   results/errors.txt   what the programs print for a missing file
#   results/strace_<program>_<buffer>.txt   output of strace -c
#   results/sample_stdio_<buffer>.txt       first read/write calls of copy_stdio
set -e

CFLAGS="-Wall -Wextra -Werror -pedantic -std=gnu89"
gcc $CFLAGS -o copy_sys copy_sys.c
gcc $CFLAGS -o copy_stdio copy_stdio.c

here=$(pwd)
out=$here/results
lab=/tmp/copy_lab
rm -rf "$lab" "$out"
mkdir -p "$lab" "$out"
cp copy_sys copy_stdio "$lab"
cd "$lab"

dd if=/dev/urandom of=big.bin bs=1M count=100 2>/dev/null
ls -l big.bin
./copy_sys big.bin out.bin 65536 > /dev/null	# warm-up run, not timed

{
	echo "| Program | Buffer (bytes) | Run 1 (s) | Run 2 (s) | Run 3 (s) | Average (s) | read() calls | write() calls | Time under strace (s) |"
	echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|"
} > "$out/results.md"

for prog in copy_sys copy_stdio
do
	for size in 512 4096 65536
	do
		times=""
		for run in 1 2 3
		do
			sync	# finish writing the previous copy to disk first
			t=$(./$prog big.bin out.bin $size | awk '/Elapsed/ {print $3}')
			cmp big.bin out.bin
			echo "$prog $size run $run: $t s, cmp OK" | tee -a "$out/runs.txt"
			times="$times $t"
		done
		avg=$(echo $times | awk '{printf "%.3f", ($1 + $2 + $3) / 3}')

		sfile=$out/strace_${prog}_${size}.txt
		st=$(strace -c -o "$sfile" ./$prog big.bin out.bin $size | awk '/Elapsed/ {print $3}')
		reads=$(awk '$NF == "read" {print $4}' "$sfile")
		writes=$(awk '$NF == "write" {print $4}' "$sfile")

		echo $times | awk -v p=$prog -v s=$size -v a=$avg -v r=$reads \
			-v w=$writes -v st=$st '{printf "| %s | %d | %.3f | %.3f | %.3f | **%s** | %d | %d | %.3f |\n", p, s, $1, $2, $3, a, r, w, st}' >> "$out/results.md"
	done
done

# what the read() and write() calls of copy_stdio look like
for size in 512 65536
do
	strace -e trace=read,write -s 8 -o sample.tmp ./copy_stdio big.bin out.bin $size > /dev/null
	sed -n '2,9p' sample.tmp > "$out/sample_stdio_$size.txt"
done

./copy_sys big.bin out_sys.bin 4096 > /dev/null
./copy_stdio big.bin out_stdio.bin 4096 > /dev/null
sha256sum big.bin out_sys.bin out_stdio.bin | tee "$out/verify.txt"

{
	echo '$ ./copy_sys missing.bin out.bin 4096'
	./copy_sys missing.bin out.bin 4096 2>&1 || echo "exit status: $?"
	echo '$ ./copy_stdio missing.bin out.bin 4096'
	./copy_stdio missing.bin out.bin 4096 2>&1 || echo "exit status: $?"
} > "$out/errors.txt"

cd "$here"
rm -rf "$lab"
cat "$out/results.md"
