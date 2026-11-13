#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NUM_STAGES 3
#define RESULT_FILE "result.txt"
#define BUF_SIZE 4096
#define SHOW_LINES 5

/* The pipeline: cat /etc/passwd | cut -d: -f1 | sort */
static char *cmd_cat[] = {"cat", "/etc/passwd", NULL};
static char *cmd_cut[] = {"cut", "-d:", "-f1", NULL};
static char *cmd_sort[] = {"sort", NULL};
static char **stages[NUM_STAGES] = {cmd_cat, cmd_cut, cmd_sort};

/**
 * close_pipes - closes both ends of every pipe
 * @pipes: pipes[i] carries the output of stage i
 *
 * Return: nothing
 */
static void close_pipes(int pipes[NUM_STAGES][2])
{
	int i;

	for (i = 0; i < NUM_STAGES; i++)
	{
		close(pipes[i][0]);
		close(pipes[i][1]);
	}
}

/**
 * spawn_stage - forks a child that runs one stage of the pipeline
 * @i: index of the stage (0 = cat, 1 = cut, 2 = sort)
 * @pipes: pipes[i] carries the output of stage i
 *
 * Description: the child reads from the previous pipe (stage 0 keeps the
 * terminal), writes into its own pipe, then closes every pipe descriptor
 * it inherited: the two it needs now live on as 0 and 1.
 *
 * Return: PID of the child, or -1 if fork() failed
 */
static pid_t spawn_stage(int i, int pipes[NUM_STAGES][2])
{
	pid_t pid = fork();

	if (pid != 0)
		return (pid);

	if (i > 0 && dup2(pipes[i - 1][0], STDIN_FILENO) == -1)
	{
		perror("dup2 stdin");
		_exit(126);
	}
	if (dup2(pipes[i][1], STDOUT_FILENO) == -1)
	{
		perror("dup2 stdout");
		_exit(126);
	}
	close_pipes(pipes);
	execvp(stages[i][0], stages[i]);
	perror(stages[i][0]);
	_exit(127);
}

/**
 * write_all - writes a whole buffer, retrying after short writes
 * @fd: file descriptor to write to
 * @buf: data to write
 * @len: number of bytes to write
 *
 * Return: 0 on success, -1 on error
 */
static int write_all(int fd, const char *buf, size_t len)
{
	ssize_t n;

	while (len > 0)
	{
		n = write(fd, buf, len);
		if (n == -1)
		{
			if (errno == EINTR)
				continue;
			return (-1);
		}
		buf += n;
		len -= n;
	}
	return (0);
}

/**
 * save_output - reads the pipe until EOF and saves everything to a file
 * @in_fd: read end of the last pipe
 *
 * Description: read() returns 0 (EOF) only when the pipe is empty and
 * every write end is closed, in every process.
 *
 * Return: 0 on success, -1 on error
 */
static int save_output(int in_fd)
{
	char buf[BUF_SIZE];
	ssize_t n;
	int out_fd, ok = 0;

	out_fd = open(RESULT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out_fd == -1)
	{
		perror(RESULT_FILE);
		return (-1);
	}
	for (;;)
	{
		n = read(in_fd, buf, sizeof(buf));
		if (n == 0)
			break;
		if (n == -1 && errno == EINTR)
			continue;
		if (n == -1 || write_all(out_fd, buf, n) == -1)
		{
			perror("saving the output");
			ok = -1;
			break;
		}
	}
	if (close(out_fd) == -1)
	{
		perror(RESULT_FILE);
		ok = -1;
	}
	return (ok);
}

/**
 * show_result - reads the file back, prints the first lines and the count
 *
 * Return: 0 on success, -1 on error
 */
static int show_result(void)
{
	char buf[BUF_SIZE];
	ssize_t n, i;
	long lines = 0;
	char last = '\n';
	int fd = open(RESULT_FILE, O_RDONLY);

	if (fd == -1)
	{
		perror(RESULT_FILE);
		return (-1);
	}
	printf("First %d lines of %s:\n", SHOW_LINES, RESULT_FILE);
	while ((n = read(fd, buf, sizeof(buf))) != 0)
	{
		if (n == -1 && errno == EINTR)
			continue;
		if (n == -1)
		{
			perror(RESULT_FILE);
			close(fd);
			return (-1);
		}
		for (i = 0; i < n; i++)
		{
			if (lines < SHOW_LINES)
				putchar(buf[i]);
			if (buf[i] == '\n')
				lines++;
			last = buf[i];
		}
	}
	close(fd);
	if (last != '\n')	/* a last line with no newline still counts */
		lines++;
	printf("Total lines: %ld\n", lines);
	return (0);
}

/**
 * wait_children - collects every child and prints its exit status
 * @pids: PIDs of the children, -1 for a child that was never started
 *
 * Return: 0 if every child exited with status 0, -1 otherwise
 */
static int wait_children(pid_t pids[NUM_STAGES])
{
	int i, status, ok = 0;

	for (i = 0; i < NUM_STAGES; i++)
	{
		if (pids[i] == -1)
			continue;
		while (waitpid(pids[i], &status, 0) == -1)
		{
			if (errno == EINTR)
				continue;
			perror("waitpid");
			return (-1);
		}
		if (WIFEXITED(status))
			printf("Child %d (%s, PID %ld) exited with status %d\n",
			       i + 1, stages[i][0], (long)pids[i],
			       WEXITSTATUS(status));
		else if (WIFSIGNALED(status))
			printf("Child %d (%s, PID %ld) killed by signal %d\n",
			       i + 1, stages[i][0], (long)pids[i],
			       WTERMSIG(status));
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
			ok = -1;
	}
	return (ok);
}

/**
 * main - Entry point, runs cat /etc/passwd | cut -d: -f1 | sort
 *
 * Description: the parent reads the output of the last stage through a
 * pipe, saves it to result.txt, shows the first lines and the line count,
 * then collects the three children.
 *
 * Return: EXIT_SUCCESS, or EXIT_FAILURE if anything failed
 */
int main(void)
{
	int pipes[NUM_STAGES][2];
	pid_t pids[NUM_STAGES] = {-1, -1, -1};
	int i, ok = 0;

	for (i = 0; i < NUM_STAGES; i++)
	{
		if (pipe(pipes[i]) == -1)
		{
			perror("pipe");
			return (EXIT_FAILURE);
		}
	}
	for (i = 0; i < NUM_STAGES && ok == 0; i++)
	{
		pids[i] = spawn_stage(i, pipes);
		if (pids[i] == -1)
		{
			perror("fork");
			ok = -1;
		}
	}
	/* the parent keeps only the read end of the last pipe */
	for (i = 0; i < NUM_STAGES; i++)
	{
		close(pipes[i][1]);
		if (i < NUM_STAGES - 1)
			close(pipes[i][0]);
	}
	if (ok == 0)
		ok = save_output(pipes[NUM_STAGES - 1][0]);
	close(pipes[NUM_STAGES - 1][0]);
	if (ok == 0)
		ok = show_result();
	if (wait_children(pids) == -1)
		ok = -1;
	return (ok == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
