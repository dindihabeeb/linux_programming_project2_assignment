#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

/**
 * copy_fd - copies everything from one descriptor to another
 * @in_fd: descriptor to read from
 * @out_fd: descriptor to write to
 * @buf: work buffer
 * @size: size of the buffer in bytes
 *
 * Description: one read() system call per buffer, then write() until the
 * whole buffer is out (write() may write less than it was asked to).
 *
 * Return: number of bytes copied, or -1 on error (errno is set)
 */
static long copy_fd(int in_fd, int out_fd, char *buf, size_t size)
{
	ssize_t n, w, done;
	long total = 0;

	while ((n = read(in_fd, buf, size)) != 0)
	{
		if (n == -1)
			return (-1);
		for (done = 0; done < n; done += w)
		{
			w = write(out_fd, buf + done, n - done);
			if (w == -1)
				return (-1);
		}
		total += n;
	}
	return (total);
}

/**
 * copy_file - opens both files, copies the data and closes them
 * @src: source file path
 * @dst: destination file path
 * @size: buffer size in bytes
 *
 * Return: number of bytes copied, or -1 on error (already reported)
 */
static long copy_file(const char *src, const char *dst, size_t size)
{
	int in_fd, out_fd;
	long total;
	char *buf;

	in_fd = open(src, O_RDONLY);
	if (in_fd == -1)
	{
		perror(src);
		return (-1);
	}
	out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out_fd == -1)
	{
		perror(dst);
		close(in_fd);
		return (-1);
	}
	buf = malloc(size);
	if (!buf)
	{
		perror("malloc");
		close(in_fd);
		close(out_fd);
		return (-1);
	}
	total = copy_fd(in_fd, out_fd, buf, size);
	if (total == -1)
		perror("copy");
	free(buf);
	close(in_fd);
	if (close(out_fd) == -1)
	{
		perror(dst);
		return (-1);
	}
	return (total);
}

/**
 * main - Entry point, copies a file with open/read/write/close
 * @argc: number of arguments
 * @argv: source file, destination file, buffer size in bytes
 *
 * Return: EXIT_SUCCESS, or EXIT_FAILURE on error
 */
int main(int argc, char *argv[])
{
	struct timespec start, end;
	long size, total;
	double elapsed;

	size = argc == 4 ? atol(argv[3]) : 0;
	if (size <= 0)
	{
		fprintf(stderr, "Usage: %s source destination buffer_size\n",
			argv[0]);
		return (EXIT_FAILURE);
	}
	clock_gettime(CLOCK_MONOTONIC, &start);
	total = copy_file(argv[1], argv[2], (size_t)size);
	clock_gettime(CLOCK_MONOTONIC, &end);
	if (total == -1)
		return (EXIT_FAILURE);

	elapsed = (end.tv_sec - start.tv_sec) +
		  (end.tv_nsec - start.tv_nsec) / 1e9;
	printf("copy_sys: copied %ld bytes with a %ld-byte buffer\n",
	       total, size);
	printf("Elapsed time: %.6f seconds\n", elapsed);
	return (EXIT_SUCCESS);
}
