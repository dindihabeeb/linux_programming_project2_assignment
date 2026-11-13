#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/**
 * copy_stream - copies everything from one stream to another
 * @in: stream to read from
 * @out: stream to write to
 * @buf: work buffer
 * @size: size of the buffer in bytes
 *
 * Description: fread() and fwrite() are library calls. They move data to
 * and from the stream's own buffer and only make a read() or write()
 * system call when that buffer is empty or full.
 *
 * Return: number of bytes copied, or -1 on error (errno is set)
 */
static long copy_stream(FILE *in, FILE *out, char *buf, size_t size)
{
	size_t n;
	long total = 0;

	while ((n = fread(buf, 1, size, in)) > 0)
	{
		if (fwrite(buf, 1, n, out) != n)
			return (-1);
		total += n;
	}
	if (ferror(in))
		return (-1);
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
	FILE *in, *out;
	long total;
	char *buf;

	in = fopen(src, "rb");
	if (!in)
	{
		perror(src);
		return (-1);
	}
	out = fopen(dst, "wb");
	if (!out)
	{
		perror(dst);
		fclose(in);
		return (-1);
	}
	buf = malloc(size);
	if (!buf)
	{
		perror("malloc");
		fclose(in);
		fclose(out);
		return (-1);
	}
	total = copy_stream(in, out, buf, size);
	if (total == -1)
		perror("copy");
	free(buf);
	fclose(in);
	if (fclose(out) == EOF)	/* fclose() writes what is still buffered */
	{
		perror(dst);
		return (-1);
	}
	return (total);
}

/**
 * main - Entry point, copies a file with fopen/fread/fwrite/fclose
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
	printf("copy_stdio: copied %ld bytes with a %ld-byte buffer\n",
	       total, size);
	printf("Elapsed time: %.6f seconds\n", elapsed);
	return (EXIT_SUCCESS);
}
