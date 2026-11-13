# Question 3: Perfect Numbers with 8 POSIX Threads

**Build:** `gcc -Wall -Wextra -Werror -pedantic -std=gnu89 -pthread -o perfect perfect.c`
(Ubuntu 24.04 in Docker, AArch64, **2 CPUs**, GCC 13.3).
**Evidence:** `./run.sh` builds the program and writes the files in
`results/`.

## The program

- **Check.** `is_perfect(n)` adds the divisors in pairs: if `i` divides
  `n`, so does `n / i`. One of the two is always ≤ √n, so the loop runs
  only while `i * i <= n`. For 100,000 that is 316 steps instead of
  99,999.
- **Single-threaded version.** `main()` checks 1 to 100,000 in one
  loop and times it.
- **Multi-threaded version.** `run_threads()` splits the range into 8
  segments of 12,500 numbers. Each thread gets its `start` and `end` in a
  `segment_t` struct, through `pthread_create()`. The main thread waits
  with `pthread_join()`.
- **Shared data.** One counter (`total_count`) and one array
  (`perfect_numbers`), both protected by one `pthread_mutex_t`, set up
  with `pthread_mutex_init()` and released with
  `pthread_mutex_destroy()`.
- **In each thread.** A perfect number is counted in the local variable
  `local_count` and stored in the shared array, with the mutex held. When
  the segment is finished, the thread locks the mutex once and adds
  `local_count` to `total_count`.
- **Output.** The array is sorted with `qsort()` and printed. Both
  times are measured with `clock_gettime(CLOCK_MONOTONIC)`.

Output (`results/output.txt`):

```
Perfect numbers found: 6 28 496 8128
The synchronized total number of perfect numbers between 1 and 100000 is 4
Single-threaded time: 0.036596 seconds
Multi-threaded time:  0.020908 seconds
```

The program also compares the two counts and exits with an error if the
single-threaded and multi-threaded versions disagree.

## Why the mutex is needed

Threads share the same memory, and the scheduler can stop a thread
between any two machine instructions.

**The counter.** `total_count += local_count` looks like one step but
is three: load the value, add, store the result. Without a mutex:

| Thread A | Thread B | `total_count` |
|---|---|---:|
| loads 0 | | 0 |
| | loads 0 | 0 |
| adds 4, stores 4 | | 4 |
| | adds 0, stores 0 | **0** |

Thread A's update is lost and the program prints a total of 0 instead of
4. This is a **race condition**: the result depends on the order in
which the threads happen to run, so the bug may appear only once in many
runs.

**The array.** `perfect_numbers[stored++] = n` reads `stored`, writes
into that slot and then increases `stored`. Two threads could read the
same value of `stored` and write into the same slot. One number would be
overwritten and lost.

With the mutex, only one thread at a time can be between
`pthread_mutex_lock()` and `pthread_mutex_unlock()`. The others wait, so
the three steps can no longer be mixed.

One honest note: in this range all four perfect numbers (6, 28, 496,
8128) are in the first segment, so only thread 1 ever writes to the
array. The counter is still updated by all 8 threads. The mutex is what
makes the program correct for any range, not only for this one.

## Why a local count and one lock per thread

Locking is not free. Each lock and unlock costs time even when nobody
else wants the mutex. When several threads want it, all but one must
wait.

| Design | Number of locks |
|---|---:|
| Lock for every number checked | 100,000 |
| Local count, lock once at the end (this program) | 8 + 4 = **12** |

(8 final additions, plus 4 perfect numbers stored.)

If every thread locked the mutex for every number, the threads would
spend their time waiting for each other. Only one could make progress at
a time, and the program could become slower than the single-threaded
version. With a local variable, each thread works on its own data, which
needs no protection, and touches the shared data only once.

## Single-threaded vs multi-threaded time

Five runs (`results/runs.txt`):

| Run | Single-threaded (s) | Multi-threaded (s) | Speedup |
|---:|---:|---:|---:|
| 1 | 0.035984 | 0.022372 | 1.61 |
| 2 | 0.036246 | 0.021569 | 1.68 |
| 3 | 0.038144 | 0.021240 | 1.80 |
| 4 | 0.035472 | 0.018476 | 1.92 |
| 5 | 0.035531 | 0.019050 | 1.87 |
| **Average** | **0.036275** | **0.020541** | **1.77** |

The multi-threaded version is about **1.8 times faster**, not 8 times.

- **The machine has 2 CPUs.** Only two threads can run at the same
  moment, so the best possible speedup is 2. The other six threads wait
  for a CPU. More threads than CPUs gives no extra speed.
- **The job is very small.** The whole search takes 36 ms. Creating
  and joining 8 threads takes a fixed amount of time that is not
  negligible against 20 ms.
- **The work is not equal** (next section), so some threads finish
  early.
- **The mutex is not the limit.** It is locked only 12 times.

On a machine with 8 CPUs the speedup would be higher, but still below 8
for the last two reasons.

## Workload balance

The segments have the same **size** but not the same **work**. The
loop in `is_perfect(n)` runs about √n times: 111 steps for 12,500 and 316
steps for 100,000. Numbers in the last segment cost almost three times as
much as numbers at the end of the first.

`./perfect -v` prints the CPU time each thread used, measured with
`clock_gettime(CLOCK_THREAD_CPUTIME_ID)` (`results/threads.txt`):

| Thread | Segment | CPU time (s) | Compared with thread 1 |
|---:|---|---:|---:|
| 1 | 1 to 12,500 | 0.002140 | 1.0 |
| 2 | 12,501 to 25,000 | 0.002915 | 1.4 |
| 3 | 25,001 to 37,500 | 0.004545 | 2.1 |
| 4 | 37,501 to 50,000 | 0.005168 | 2.4 |
| 5 | 50,001 to 62,500 | 0.006342 | 3.0 |
| 6 | 62,501 to 75,000 | 0.006031 | 2.8 |
| 7 | 75,001 to 87,500 | 0.006349 | 3.0 |
| 8 | 87,501 to 100,000 | 0.005717 | 2.7 |

- The upper segments need about 3 times the CPU time of the first. (The
  values are a few milliseconds each, so they vary a little between
  runs.)
- The 8 CPU times add up to 0.039 s, about the same as the
  single-threaded time. The threads do the same total work. They do not
  do less work, they share it between CPUs.
- With 8 CPUs, thread 1 would finish early and its CPU would sit idle
  while the threads with the high segments were still working. The total
  time would be set by the slowest thread, not by the average.

A better split would give equal *work*: smaller segments for the large
numbers, or many small chunks that idle threads take one after another
(the work-queue idea used in Question 4).
