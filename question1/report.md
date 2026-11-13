# Question 1: Three-Stage Pipeline and its System Calls

**Build:** `gcc -Wall -Wextra -Werror -pedantic -std=gnu89 -o pipeline pipeline.c`
(Ubuntu 24.04, AArch64, GCC 13.3, strace 6.8). **Evidence:** `./run_trace.sh`
builds the program and writes `output.txt`, `result.txt` and the two
traces in `traces/`.

## The program

`pipeline.c` runs `cat /etc/passwd | cut -d: -f1 | sort` with three
children. The brief allows an equivalent three-stage pipeline. I cut
field 1 (user names) instead of field 7 (shells) because this system has
only 3 different shells, which is not enough to show "first 5 lines".

```
            pipe A            pipe B            pipe C
  cat  ───────────────► cut ───────────────► sort ───────────────► parent ──► result.txt
child 1               child 2               child 3
```

1. The parent creates the three pipes with `pipe()`.
2. It calls `fork()` three times. Each child uses `dup2()` to put the
   pipe ends it needs on descriptors 0 and 1, closes all six pipe
   descriptors, and calls `execvp()`.
3. The parent closes every pipe end except the read end of pipe C.
4. It reads pipe C until EOF and saves the data in `result.txt` with
   `open()` and `write()`.
5. It opens `result.txt` again, reads it back, prints the first 5 lines
   and the number of lines.
6. It calls `waitpid()` for each child and prints the exit status.

Output (`output.txt`):

```
First 5 lines of result.txt:
_apt
backup
bin
daemon
games
Total lines: 24
Child 1 (cat, PID 236) exited with status 0
Child 2 (cut, PID 237) exited with status 0
Child 3 (sort, PID 238) exited with status 0
```

`run_trace.sh` also checks that `result.txt` is identical (`cmp`) to
the output of the same pipeline run by the shell.

## The traces

| File | Command |
|---|---|
| `traces/trace.log` | `strace -f -tt -o traces/trace.log ./pipeline` |
| `traces/trace_filtered.log` | `strace -f -y -e trace=clone,?clone3,?fork,execve,?pipe,pipe2,?dup2,dup3,?open,openat,read,write,close,wait4,exit_group -o traces/trace_filtered.log ./pipeline` |

- `-f` follows the children, `-tt` adds timestamps, and `-y` shows what
  each descriptor points to (for example `4<pipe:[1616059]>`).
- On AArch64 the kernel only has the newer calls, so the C functions
  appear under these names: `fork` → `clone`, `pipe` → `pipe2`,
  `dup2` → `dup3`, `open` → `openat`, `waitpid` → `wait4`,
  `exit` → `exit_group`. A `?` in the filter lets strace skip a name
  that does not exist on the machine.
- The excerpts below are from `trace_filtered.log`. The first column is
  the PID: **249** is the parent, **250** is cat, **251** is cut and
  **252** is sort. When another process runs in the middle of a call,
  strace prints the call in two parts (`<unfinished ...>` and
  `<... resumed>`).

## 1. Process creation

```
249   clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, child_tidptr=0xf60d64974fb0) = 250
249   <... clone resumed>, child_tidptr=0xf60d64974fb0) = 251
249   <... clone resumed>, child_tidptr=0xf60d64974fb0) = 252
```
```
252   execve("/usr/local/sbin/sort", ["sort"], 0xffffdeb3bd48 /* 4 vars */ <unfinished ...>
252   <... execve resumed>)             = -1 ENOENT (No such file or directory)
252   execve("/usr/bin/sort", ["sort"], 0xffffdeb3bd48 /* 4 vars */ <unfinished ...>
252   <... execve resumed>)             = 0
```

- **`clone` (fork).** Creates a child that is a copy of the parent,
  including its table of open descriptors. It returns the child's PID in
  the parent (250, 251, 252). The `SIGCHLD` flag asks the kernel to send
  the parent that signal when the child ends.
- **`execve` (execvp).** Replaces the program in the child with a new
  one. The PID and the open descriptors stay the same. This is why the
  redirection is done *before* it. `execvp()` tries each directory of
  `PATH` in turn, which gives the `ENOENT` errors until `/usr/bin/sort`
  is found. On success `execve` returns 0 in the trace and never returns
  to our code.

## 2. Pipe setup and redirection

```
249   pipe2([3<pipe:[1616059]>, 4<pipe:[1616059]>], 0) = 0
249   pipe2([5<pipe:[1616060]>, 6<pipe:[1616060]>], 0) = 0
249   pipe2([7<pipe:[1616061]>, 8<pipe:[1616061]>], 0) = 0
```
```
250   dup3(4<pipe:[1616059]>, 1</dev/null>, 0) = 1<pipe:[1616059]>
251   <... dup3 resumed>)               = 0<pipe:[1616059]>
251   <... dup3 resumed>)               = 1<pipe:[1616060]>
252   <... dup3 resumed>)               = 0<pipe:[1616060]>
252   <... dup3 resumed>)               = 1<pipe:[1616061]>
```

- **`pipe2` (pipe).** Creates a one-way buffer inside the kernel and
  returns two descriptors: the first to read from it, the second to write
  into it. Pipe A is descriptors 3 and 4, pipe B is 5 and 6, pipe C is 7
  and 8. They are created before the forks so that every child inherits
  them.
- **`dup3` (dup2).** `dup2(old, new)` makes descriptor `new` point to
  the same thing as `old`. `dup3(4, 1)` in cat makes descriptor 1
  (standard output) point to pipe A. cat still just writes to "1", but
  the data now goes into the pipe.
- **`close`.** Removes one descriptor. After `dup2()` the original
  number is a spare copy, so each child closes all six pipe descriptors
  (3 to 8) and keeps only 0 and 1.

### How the descriptors change

| Process | After `fork()` | `dup2()` calls | After closing | Result |
|---|---|---|---|---|
| Parent (249) | 3, 4, 5, 6, 7, 8 | none | closes 3, 4, 5, 6, 8 | **7** = read end of pipe C |
| Child 1, cat (250) | same six | `dup2(4, 1)` | closes 3 to 8 | 0 = unchanged, **1** = write end of A |
| Child 2, cut (251) | same six | `dup2(3, 0)`, `dup2(6, 1)` | closes 3 to 8 | **0** = read end of A, **1** = write end of B |
| Child 3, sort (252) | same six | `dup2(5, 0)`, `dup2(8, 1)` | closes 3 to 8 | **0** = read end of B, **1** = write end of C |

In the end each pipe end is open in exactly one process. The parent then
opens `result.txt` and gets descriptor **3**: the kernel always gives the
lowest free number, and 3 is free again.

### If the unused pipe ends are not closed

A reader gets EOF from a pipe only when **every** copy of the write end
is closed, in every process. `fork()` copies all the descriptors, so
there are many copies.

- If the **parent** kept descriptor 8 (write end of pipe C), its own
  `read(7)` would never return 0. The parent would wait forever for data
  that only it could write. This is a deadlock.
- If the parent or sort kept descriptor 4 (write end of pipe A), cut
  would never see EOF and never exit. Then sort would never see EOF
  either, because sort prints nothing until its input ends. The whole
  pipeline would hang.
- An unused *read* end is also a problem. A writer normally gets
  `SIGPIPE` when nobody can read the pipe any more. With a forgotten
  read end still open, the writer would instead block once the pipe is
  full (64 KB).

## 3. File operations

Data moving through the three pipes:

```
250   openat(AT_FDCWD</work/project2_assignment/question1>, "/etc/passwd", O_RDONLY) = 3</etc/passwd>
250   read(3</etc/passwd>, "root:x:0:0:root:/root:/bin/bash\n"..., 131072) = 1209
250   write(1<pipe:[1616059]>, "root:x:0:0:root:/root:/bin/bash\n"..., 1209) = 1209
251   read(0<pipe:[1616059]>, "root:x:0:0:root:/root:/bin/bash\n"..., 4096) = 1209
251   read(0<pipe:[1616059]>, "", 4096) = 0
251   write(1<pipe:[1616060]>, "root\ndaemon\nbin\nsys\nsync\ngames\nm"..., 171) = 171
252   read(0<pipe:[1616060]>, "root\ndaemon\nbin\nsys\nsync\ngames\nm"..., 131072) = 171
252   read(0<pipe:[1616060]>, "", 126976) = 0
```

The parent saving the result, then reading it back:

```
249   <... openat resumed>)             = 3</work/project2_assignment/question1/result.txt>
249   <... read resumed>"_apt\nbackup\nbin\ndaemon\ngames\nirc"..., 4096) = 171
249   <... write resumed>)              = 171
249   <... read resumed>"", 4096)       = 0
249   <... close resumed>)              = 0
249   <... openat resumed>)             = 3</work/project2_assignment/question1/result.txt>
249   read(3</work/project2_assignment/question1/result.txt>, "_apt\nbackup\nbin\ndaemon\ngames\nirc"..., 4096) = 171
249   read(3</work/project2_assignment/question1/result.txt>, "", 4096) = 0
249   close(3</work/project2_assignment/question1/result.txt>) = 0
```

- **`openat` (open).** Opens a file and returns a descriptor.
  `O_WRONLY|O_CREAT|O_TRUNC, 0644` creates `result.txt` or empties it.
  The second `openat` uses `O_RDONLY` to read it back.
- **`read`.** Copies up to N bytes from a file or pipe into the
  program's buffer and returns how many it got. On an empty pipe it
  waits. A return value of 0 means end of data.
- **`write`.** Copies bytes from the program's buffer to a file or
  pipe. The same 171 bytes go from sort into pipe C, then from the
  parent into `result.txt`.

The 1209 bytes of `/etc/passwd` become 171 bytes of user names after
cut, and sort writes the same 171 bytes in sorted order.

### How the parent detects EOF

The parent loops on `read(7, buf, 4096)`:

1. The first `read` returns **171**: the data sort wrote.
2. sort then closes its descriptor 1 and exits. That was the last open
   write end of pipe C, because the parent and the other children closed
   their copies at the start.
3. The next `read` returns **0**. The pipe is empty and no process can
   write to it any more, so the kernel reports EOF and the loop stops.

The same thing happens along the chain: cat exits → cut's `read(0)`
returns 0 → cut exits → sort's `read(0)` returns 0 → sort writes its
output and exits → the parent's `read(7)` returns 0.

## 4. Process termination

From `traces/trace.log` (other run, so the PIDs are 242 to 245):

```
243   22:37:01.681912 +++ exited with 0 +++
242   22:37:01.682086 --- SIGCHLD {si_signo=SIGCHLD, si_code=CLD_EXITED, si_pid=243, si_uid=0, si_status=0, si_utime=0, si_stime=0} ---
244   22:37:01.683173 +++ exited with 0 +++
245   22:37:01.684307 +++ exited with 0 +++
242   22:37:01.685504 wait4(243, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 243
242   22:37:01.685639 wait4(244, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 244
242   22:37:01.685758 wait4(245, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0, NULL) = 245
242   22:37:01.685881 write(1, "First 5 lines of result.txt:\n_ap"..., 207) = 207
242   22:37:01.685997 exit_group(0)     = ?
242   22:37:01.686148 +++ exited with 0 +++
```

- **`exit_group` (exit).** Ends the process and closes all its
  descriptors. Closing them is what gives the next stage its EOF. The
  children end in pipeline order: cat, cut, sort.
- **`SIGCHLD`.** The kernel tells the parent that a child ended. The
  parent ignores it (the default), and the `read` it interrupted is
  restarted automatically (`ERESTARTSYS` in the trace).
- **`wait4` (waitpid).** A child that has ended stays in the process
  table as a *zombie* until its parent collects it. `waitpid(pid,
  &status, 0)` collects one child, removes the zombie and fills `status`.
  The program calls it once per child, in order.

### How the exit statuses are collected

`status` is not the exit code itself. The program decodes it with the
macros that strace also shows:

- `WIFEXITED(status)` is true if the child ended by itself, and then
  `WEXITSTATUS(status)` is its exit code. All three are 0 here.
- `WIFSIGNALED(status)` is true if a signal killed it, and then
  `WTERMSIG(status)` is the signal number.

If `execvp()` fails, the child exits with 127, and the parent prints
that status. The parent itself exits with 0 only if all three children
exited with 0.

The single `write(1, ..., 207)` at the end is all of the parent's
`printf()` output. In this run standard output was not a terminal, so
the C library kept the text in its buffer and wrote it once at exit.
