# Question 2: File Copy with System Calls vs Standard I/O

**Build:**
`gcc -Wall -Wextra -Werror -pedantic -std=gnu89 -o copy_sys copy_sys.c` and
`gcc -Wall -Wextra -Werror -pedantic -std=gnu89 -o copy_stdio copy_stdio.c`
(Ubuntu 24.04 in Docker, AArch64, 2 CPUs, GCC 13.3, strace 6.8).
**Evidence:** `./run_experiments.sh` does everything below and writes
the files in `results/`.

## The programs

Both take the same arguments: `./copy_sys src.bin dst.bin 4096`.

| | `copy_sys` | `copy_stdio` |
|---|---|---|
| Functions used | `open()`, `read()`, `write()`, `close()` | `fopen()`, `fread()`, `fwrite()`, `fclose()` |
| The buffer-size argument is | the size of every `read()` | the size of every `fread()` |
| Timing | `clock_gettime(CLOCK_MONOTONIC)` before the open and after the close | the same |
| Errors | `perror()`, exit status 1 | the same |

Error handling (`results/errors.txt`):

```
$ ./copy_sys missing.bin out.bin 4096
missing.bin: No such file or directory
exit status: 1
$ ./copy_stdio missing.bin out.bin 4096
missing.bin: No such file or directory
exit status: 1
```

## The experiment

- Test file: `dd if=/dev/urandom of=big.bin bs=1M count=100`
  (104,857,600 bytes).
- Each program runs 3 times with each buffer size: 512 B, 4 KB (4096)
  and 64 KB (65536).
- **Every one of the 18 copies is checked with `cmp big.bin out.bin`**
  (`results/runs.txt`). One copy by each program is also checked with
  `sha256sum` (`results/verify.txt`): all three hashes are
  `e570ed87…cb710dda`.
- The system calls are counted with `strace -c ./copy_sys big.bin
  out.bin 4096`, and the same for the other program and sizes
  (`results/strace_*.txt`).
- The test file is in the page cache (it was just written), so the runs
  measure the cost of the calls and not the speed of the disk.

## Results

Elapsed times printed by the programs, without strace, and the call
counts from `strace -c`:

| Program | Buffer (bytes) | Run 1 (s) | Run 2 (s) | Run 3 (s) | Average (s) | `read` calls | `write` calls |
|---|---:|---:|---:|---:|---:|---:|---:|
| copy_sys | 512 | 0.185 | 0.144 | 0.138 | **0.156** | 204,802 | 204,801 |
| copy_sys | 4096 | 0.091 | 0.054 | 0.046 | **0.064** | 25,602 | 25,601 |
| copy_sys | 65536 | 0.045 | 0.038 | 0.035 | **0.039** | 1,602 | 1,601 |
| copy_stdio | 512 | 0.082 | 0.064 | 0.046 | **0.064** | 25,602 | 25,601 |
| copy_stdio | 4096 | 0.080 | 0.048 | 0.042 | **0.057** | 25,602 | 25,601 |
| copy_stdio | 65536 | 0.043 | 0.033 | 0.040 | **0.039** | 1,602 | 3,200 |

The `calls`, `seconds` and `% time` columns of `strace -c` for `read`
and `write`, and the program's own time while it was being traced:

| Program | Buffer | `read` calls | `read` seconds | `read` % time | `write` calls | `write` seconds | `write` % time | Elapsed under strace (s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| copy_sys | 512 | 204,802 | 0.810 | 47.09 | 204,801 | 0.900 | 52.31 | 13.427 |
| copy_sys | 4096 | 25,602 | 0.098 | 42.98 | 25,601 | 0.121 | 52.98 | 1.625 |
| copy_sys | 65536 | 1,602 | 0.017 | 35.00 | 1,601 | 0.020 | 42.70 | 0.145 |
| copy_stdio | 512 | 25,602 | 0.107 | 41.92 | 25,601 | 0.135 | 53.16 | 1.639 |
| copy_stdio | 4096 | 25,602 | 0.090 | 43.19 | 25,601 | 0.109 | 52.43 | 1.478 |
| copy_stdio | 65536 | 1,602 | 0.014 | 27.82 | 3,200 | 0.028 | 53.61 | 0.173 |

Reading the counts:

- **copy_sys makes exactly one `read` and one `write` per buffer.**
  104,857,600 / 512 = 204,800. The two extra `read` calls are the last
  one, which returns 0 (EOF), and one made by the loader to read the C
  library. The one extra `write` is the `printf()` output.
- **copy_stdio with 512 bytes makes 8 times fewer calls** than copy_sys
  (25,602 instead of 204,802), and exactly as many as with 4096 bytes.
- In every row `read` and `write` together are 78 % to 99 % of the time
  spent in system calls. The program does almost nothing else.

**strace slows the programs down.** strace stops the program at the
start and at the end of every system call. `copy_sys` with 512 bytes
takes 0.156 s alone and **13.4 s** under strace, 86 times longer. So
the elapsed times in the first table come from normal runs, and strace is
only used for counting. The slowdown is proportional to the number of
calls, so under strace the differences look much bigger than they are.

## How `fread()`/`fwrite()` buffering reduces the calls

`fread()` and `fwrite()` are C library functions, not system calls.
Each `FILE` has its own buffer in the program's memory, 4096 bytes here.

- **`fread()`** copies bytes out of that buffer. Only when the buffer
  is empty does the library call `read()`, asking for 4096 bytes at once.
- **`fwrite()`** copies bytes into the buffer. Only when it is full
  does the library call `write()`, with 4096 bytes. `fclose()` writes
  what is left.

So with a 512-byte buffer, eight `fread()` calls are served by one
`read()`, and eight `fwrite()` calls become one `write()`. This is
visible with `strace -e trace=read,write ./copy_stdio … 512`
(`results/sample_stdio_512.txt`): every call is 4096 bytes although the
program asks for 512.

```
read(3, "\230\376\26$\271?\273E"..., 4096) = 4096
read(3, "\227`\265\225\33\330\35:"..., 4096) = 4096
write(4, "\230\376\26$\271?\273E"..., 4096) = 4096
```

When the program's buffer is bigger than the library's (64 KB), the
library does not use its small buffer for reading: it calls `read()`
for the full 65536 bytes. For writing, after the first block, it sends
each 64 KB block as two calls (`results/sample_stdio_65536.txt`). That is
why this row has 3,200 writes instead of 1,601.

```
read(3, "\355\223uS?=\333\223"..., 65536) = 65536
write(4, "\355\223uS?=\333\223"..., 4096) = 4096
write(4, "\222\252fDZ\321I\t"..., 61440) = 61440
```

## Why each system call adds overhead

A normal function call is a jump inside the program. A system call is
much more:

1. The CPU switches from **user mode to kernel mode** (the `svc`
   instruction on AArch64, `syscall` on x86-64).
2. The kernel saves the program's registers and checks the arguments: is
   the descriptor valid, is the buffer address allowed?
3. It does the work: here, copying bytes between the program's buffer and
   the page cache.
4. It restores the registers and switches back to user mode.

Steps 1, 2 and 4 cost the same whether the call moves 512 bytes or
64 KB. They are a fixed price paid on every call. From the first table,
`copy_sys` with 512 bytes makes about 358,000 more calls than
`copy_stdio` with 512 bytes and takes 0.092 s longer: roughly
**0.26 µs per call**.

## Why the gap shrinks as the buffer grows

| Buffer | copy_sys (s) | copy_stdio (s) | copy_sys / copy_stdio | Total `read` + `write` calls (sys / stdio) |
|---:|---:|---:|---:|---|
| 512 | 0.156 | 0.064 | 2.4 | 409,603 / 51,203 |
| 4096 | 0.064 | 0.057 | 1.1 | 51,203 / 51,203 |
| 65536 | 0.039 | 0.039 | 1.0 | 3,203 / 4,802 |

The time has two parts:

- **Moving the 100 MB of data.** This is the same for both programs and
  every buffer size.
- **The fixed price per system call**, multiplied by the number of
  calls.

1. **At 512 bytes** `copy_sys` pays the fixed price 409,603 times.
   `copy_stdio` hides the small buffer behind its own 4096-byte buffer
   and pays it only 51,203 times. It is 2.4 times faster.
2. **At 4096 bytes** the program's buffer is as big as the library's.
   Both make the same 51,203 calls and the times are almost equal.
   Buffering has nothing left to save.
3. **At 64 KB** both make only a few thousand calls. The fixed price is
   now tiny compared with moving the data, so both take the same time.
   `copy_stdio` even makes more calls here and it does not matter.

Going from 512 bytes to 64 KB makes `copy_sys` 4 times faster (0.156 →
0.039 s), and `copy_stdio` only 1.6 times faster (0.064 → 0.039 s),
because it never made the very small calls in the first place.

In short: buffered I/O protects a program that reads or writes in small
pieces. With a large buffer the program is already doing what the library
would do for it, and direct system calls are just as fast.

**Limits of the measurement.** The runs are short (under 0.2 s) and
some first runs are slower than the next two, so differences of about
0.01 s between two rows are not meaningful. The call counts are exact and
do not change between runs.
