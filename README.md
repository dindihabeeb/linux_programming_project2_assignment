# Linux Programming: Project 2

Four C programs about processes, pipes, file I/O and POSIX threads. Each
folder has the source, a script that builds the program and produces
the evidence, the saved evidence, and a `report.md`.

| Folder | Program | Topic | Run | Evidence | Report |
|---|---|---|---|---|---|
| `question1/` | `pipeline` | Three-stage pipeline with `fork`, `execvp`, `pipe`, `dup2`, traced with strace | `./run_trace.sh` | `output.txt`, `result.txt`, `traces/` | `report.md` |
| `question2/` | `copy_sys`, `copy_stdio` | File copy with system calls vs standard I/O, three buffer sizes | `./run_experiments.sh` | `results/` | `report.md` |
| `question3/` | `perfect` | Perfect numbers up to 100,000 with 8 threads and a mutex | `./run.sh` | `results/` | `report.md` |
| `question4/` | `logscan` | Thread pool with a shared work queue counting matching log lines | `./run_experiments.sh` | `results/` | `report.md` |

## Building and running

Everything needs GNU/Linux with `gcc` and `strace`. All programs build
with `-Wall -Wextra -Werror -pedantic -std=gnu89` (plus `-pthread` for
questions 3 and 4). Run each script from inside its folder:

```sh
cd question1 && ./run_trace.sh
cd question2 && ./run_experiments.sh    # creates a 100 MB file in /tmp
cd question3 && ./run.sh
cd question4 && ./run_experiments.sh    # creates 209 MB of logs in /tmp
```

The scripts for questions 2 and 4 work in `/tmp` and delete their large
files when they finish.

To run a program by hand:

```sh
./pipeline
./copy_sys big.bin out.bin 4096
./copy_stdio big.bin out.bin 4096
./perfect            # add -v to also print the CPU time of each thread
./logscan ERROR report.txt 4 app01.log app02.log app03.log
```

## Test environment

The saved results come from Ubuntu 24.04 in Docker on AArch64 with
2 CPUs, GCC 13.3 and strace 6.8. Times will differ on another machine.
The thread speedups in questions 3 and 4 are limited by the 2 CPUs.
# linux_programming_project2_assignment
