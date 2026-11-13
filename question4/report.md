# Question 4: Log Scanner with a Thread Pool

**Build:** `gcc -Wall -Wextra -Werror -pedantic -std=gnu89 -pthread -o logscan logscan.c`
(Ubuntu 24.04 in Docker, AArch64, **2 CPUs**, GCC 13.3).
**Evidence:** `./run_experiments.sh` generates the log files, runs
everything below and writes the files in `results/`.

## The program

`./logscan pattern report.txt <number_of_threads> file1.log file2.log ...`

- **Work queue.** The file names stay in `argv`. The queue is a
  `queue_t` struct: the list, the number of files, the index `next` of
  the first file nobody has taken, and a mutex. `next_file()` locks the
  mutex, takes `files[next]`, increases `next` and unlocks.
- **Pool.** `run_pool()` creates the worker threads with
  `pthread_create()` and waits with `pthread_join()`. Each worker loops:
  take a file, process it, take another, until `next_file()` returns
  `NULL`.
- **Processing a file.** `scan_file()` uses `fopen()`, reads line by
  line with `fgets()`, looks for the pattern with `strstr()`, counts the
  matching lines and all lines, and calls `fclose()`. A file that cannot
  be opened is reported with `perror()` and the worker goes on to the
  next file.
- **Three shared resources, three mutexes**, each inside its own
  struct: the queue (`queue.lock`), the report file (`report.lock`) and
  the grand totals (`totals.lock`). They are set up with
  `pthread_mutex_init()` and released with `pthread_mutex_destroy()`.
- **No mutex is held while a file is read.** Each one is held only for
  the short step it protects.
- **Summary.** After the joins, the main thread appends the summary
  line to the report and prints it, with the elapsed time measured by
  `clock_gettime(CLOCK_MONOTONIC)`. The baseline is the same program with
  1 thread.

Report written with 4 threads (`results/report_4.txt`):

```
app04.log: 3820 matching lines out of 30547 (thread 4)
app02.log: 1819 matching lines out of 15275 (thread 2)
app06.log: 5755 matching lines out of 45831 (thread 2)
app07.log: 47442 matching lines out of 381905 (thread 2)
app03.log: 57269 matching lines out of 458286 (thread 3)
app08.log: 9471 matching lines out of 76376 (thread 2)
app05.log: 76411 matching lines out of 611054 (thread 4)
app10.log: 15030 matching lines out of 122211 (thread 2)
app12.log: 18891 matching lines out of 152769 (thread 2)
app09.log: 38249 matching lines out of 305491 (thread 3)
app01.log: 95701 matching lines out of 763807 (thread 1)
app11.log: 28885 matching lines out of 229130 (thread 4)
Total matching lines across 12 files: 398743
```

A missing file (`results/errors.txt`):

```
$ ./logscan ERROR report.txt 2 app02.log missing.log app04.log
missing.log: No such file or directory
Total matching lines across 2 files: 5639
Elapsed time with 2 thread(s): 0.001704 seconds
exit status: 0
```

## The experiment

- `gen_logs.sh` generates **12 log files from 1 MB to 50 MB** (50, 1,
  30, 2, 40, 3, 25, 5, 20, 8, 15 and 10 MB, in that order), 209 MB and
  3,192,682 lines in total. The sizes are listed in `results/files.txt`.
- The pattern is `ERROR`.
- The program runs 3 times with 1, 2, 4 and 8 threads.
- The files are read once before the timed runs, so every run reads
  them from the page cache.

**Correctness** (`results/verify.txt`): the total is the same as grep,
with every thread count.

```
$ grep -c ERROR app*.log | awk -F: '{sum += $2} END {print sum}'
398743
$ tail -n 1 report_1.txt
Total matching lines across 12 files: 398743
$ tail -n 1 report_8.txt
Total matching lines across 12 files: 398743
```

## Results

| Threads | Run 1 (s) | Run 2 (s) | Run 3 (s) | Average (s) | Speedup (T1 / Tn) |
|---:|---:|---:|---:|---:|---:|
| 1 (baseline) | 0.111 | 0.113 | 0.110 | **0.111** | 1.00 |
| 2 | 0.059 | 0.057 | 0.055 | **0.057** | 1.95 |
| 4 | 0.058 | 0.059 | 0.059 | **0.059** | 1.89 |
| 8 | 0.062 | 0.060 | 0.056 | **0.059** | 1.87 |

Two threads are almost twice as fast as one. Four and eight threads are
not faster than two, and are slightly slower.

## Why each mutex is needed

Threads share memory, and a thread can be stopped between any two
instructions. Without protection, three things can go wrong.

**The work queue.** Taking a file is "read `next`, use it, add 1".
Without the mutex two workers could read the same value of `next`:

| Worker A | Worker B | `next` |
|---|---|---:|
| reads 3 | | 3 |
| | reads 3 | 3 |
| takes `files[3]`, stores 4 | | 4 |
| | takes `files[3]`, stores 4 | 4 |

The same file is processed twice. Its line appears twice in the report
and its matches are counted twice in the total. With another timing a
file could be skipped. This is a race condition: the result depends on
the order in which the threads run.

**The report file.** All workers write through the same `FILE`, which
has one buffer and one position. Each result line must go out as a whole.
`report.lock` makes the write of one line a single step, so two lines
cannot be mixed together (interleaved) and no line can be lost.

**The grand totals.** `totals.matching += matching` is three steps:
load, add, store. Two workers that load the same old value both store
"old + mine", and one addition is lost. The summary line would then show
a total that is too low, and not the same in every run.

**Why separate mutexes.** The three resources have nothing to do with
each other. With a single mutex, a worker writing its report line would
block another worker that only wants the next file name. With one mutex
per resource, a worker only waits when another worker is using *the same*
resource.

## Why a shared queue balances the load

The files are very different in size: `app01.log` is 50 MB and
`app02.log` is 1 MB.

**One fixed share per thread.** Suppose 4 threads each got every
fourth file. Thread 1 would get `app01`, `app05` and `app09` (50 + 40 +
20 = **110 MB**) and thread 2 would get `app02`, `app06` and `app10`
(1 + 3 + 8 = **12 MB**). Thread 2 would finish almost at once and then
do nothing, while thread 1 still had most of its work left. The total
time would be the time of the unluckiest thread.

**A shared queue.** No file belongs to a thread in advance. A worker
that finishes simply takes the next file. A worker with a small file
comes back sooner and takes more files. The report above shows it:

- **Thread 1** processed 1 file: `app01.log` (50 MB).
- **Thread 2** processed 6 files in the same time: `app02`, `app06`,
  `app07`, `app08`, `app10` and `app12` (1 + 3 + 25 + 5 + 8 + 10 MB).
- Thread 3 processed 2 files and thread 4 processed 3.

Nobody is idle while files are left in the queue, and nobody had to know
the file sizes beforehand. The same queue works when there are more files
than threads (12 files, 2 threads) or more threads than files.

## Why the speedup stops growing

**The number of CPUs.** This is the main limit here. The machine has 2
CPUs, so only two workers can run at the same moment. With 4 or 8 threads
the extra workers wait for a CPU. The kernel then has to switch between
them, which costs a little time and explains the small drop from 1.95 to
1.87.

**Uneven file sizes.** One file is processed by one thread, and it
cannot be split. `app01.log` alone is a quarter of all the data (50 of
209 MB). Even with many CPUs, the run cannot finish before that file is
done, so the speedup could not go much above 4, whatever the number of
threads. With 8 threads, some workers would have nothing to do after the
small files are gone.

**Disk I/O and the page cache.** In these runs the files are already
in the page cache (in memory), so the workers only use CPU and memory.
This is the best case for threads. If the files had to come from the
disk, all the threads would wait for the same device. A disk delivers a
limited number of bytes per second, and more threads would not make it
faster. Several threads reading different files could even make a
spinning disk slower, because of the head movements. Memory speed is also
shared: all threads read through the same memory bus.

**Mutex contention.** It is very small in this program and is not the
reason for the limit. Each file needs only three short locks (queue,
report, totals), so the whole run locks a mutex about 36 times, while
scanning 3.2 million lines without any lock. Contention *would* become
the limit with a different design, for example if a worker locked the
totals for every line. Then the workers would spend their time waiting
for each other instead of reading.

**The sequential part.** Starting the program, creating the threads
and writing the summary are done by one thread. That part does not get
faster with more threads (Amdahl's law).

In short: the speedup follows the number of CPUs up to 2, then stays
flat. More threads than CPUs do not help a job that only needs CPU.
