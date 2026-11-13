#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define LINE_LEN 4096
#define MAX_THREADS 64

/**
 * struct queue_s - The shared work queue: the list of files to process
 *
 * @files: File names (they are the last command-line arguments)
 * @count: Number of files
 * @next: Index of the next file nobody has taken yet
 * @lock: Protects @next
 */
typedef struct queue_s
{
	char		**files;
	int		count;
	int		next;
	pthread_mutex_t	lock;
} queue_t;

/**
 * struct report_s - The shared report file
 *
 * @fp: The open report file
 * @lock: Protects writes to @fp
 */
typedef struct report_s
{
	FILE		*fp;
	pthread_mutex_t	lock;
} report_t;

/**
 * struct totals_s - The shared grand totals
 *
 * @matching: Matching lines found in all files so far
 * @files_done: Number of files processed so far
 * @lock: Protects @matching and @files_done
 */
typedef struct totals_s
{
	long		matching;
	int		files_done;
	pthread_mutex_t	lock;
} totals_t;

/**
 * struct worker_s - What one worker thread is given
 *
 * @id: Number of the worker, starting at 1
 */
typedef struct worker_s
{
	int	id;
} worker_t;

/* Shared by all workers: each one has its own mutex */
static queue_t queue;
static report_t report;
static totals_t totals;
static const char *pattern;

/**
 * next_file - takes the next unprocessed file from the queue
 *
 * Return: the file name, or NULL when the queue is empty
 */
static char *next_file(void)
{
	char *name = NULL;

	pthread_mutex_lock(&queue.lock);
	if (queue.next < queue.count)
		name = queue.files[queue.next++];
	pthread_mutex_unlock(&queue.lock);
	return (name);
}

/**
 * scan_file - counts the lines of a file, and those holding the pattern
 * @name: file to read
 * @matching: where to store the number of matching lines
 * @total: where to store the total number of lines
 *
 * Description: a line longer than the buffer comes back from fgets() in
 * several pieces. It is counted once, when the piece ending in '\n' (or
 * the end of the file) arrives.
 *
 * Return: 0 on success, -1 if the file could not be read
 */
static int scan_file(const char *name, long *matching, long *total)
{
	char line[LINE_LEN];
	int found = 0, pending = 0, failed;
	FILE *fp = fopen(name, "r");

	if (!fp)
	{
		perror(name);
		return (-1);
	}
	*matching = 0;
	*total = 0;
	while (fgets(line, sizeof(line), fp))
	{
		if (strstr(line, pattern))
			found = 1;
		pending = 1;
		if (strchr(line, '\n'))
		{
			*total += 1;
			*matching += found;
			found = 0;
			pending = 0;
		}
	}
	if (pending)	/* last line without a newline */
	{
		*total += 1;
		*matching += found;
	}
	failed = ferror(fp);
	if (failed)
		perror(name);
	fclose(fp);
	return (failed ? -1 : 0);
}

/**
 * worker - thread function: processes files until the queue is empty
 * @arg: pointer to the worker_t of this thread
 *
 * Description: the slow part, reading the file, is done without holding
 * any mutex. Each mutex is held only for the one short step it protects.
 *
 * Return: NULL
 */
static void *worker(void *arg)
{
	worker_t *me = arg;
	char *name;
	long matching, total;

	while ((name = next_file()) != NULL)
	{
		if (scan_file(name, &matching, &total) == -1)
			continue;

		pthread_mutex_lock(&report.lock);
		fprintf(report.fp,
			"%s: %ld matching lines out of %ld (thread %d)\n",
			name, matching, total, me->id);
		pthread_mutex_unlock(&report.lock);

		pthread_mutex_lock(&totals.lock);
		totals.matching += matching;
		totals.files_done++;
		pthread_mutex_unlock(&totals.lock);
	}
	return (NULL);
}

/**
 * run_pool - creates the worker threads and waits for all of them
 * @num_threads: number of workers
 *
 * Return: 0 on success, -1 if no thread could be created
 */
static int run_pool(int num_threads)
{
	pthread_t tids[MAX_THREADS];
	worker_t workers[MAX_THREADS];
	int i, created = 0;

	for (i = 0; i < num_threads; i++)
	{
		workers[i].id = i + 1;
		if (pthread_create(&tids[i], NULL, worker, &workers[i]) != 0)
		{
			fprintf(stderr, "pthread_create failed\n");
			break;
		}
		created++;
	}
	for (i = 0; i < created; i++)
		pthread_join(tids[i], NULL);
	return (created > 0 ? 0 : -1);
}

/**
 * finish - writes the summary line and releases the shared resources
 * @ok: 0 if the pool ran
 * @elapsed: time the pool took, in seconds
 * @num_threads: number of workers that were asked for
 *
 * Return: EXIT_SUCCESS or EXIT_FAILURE
 */
static int finish(int ok, double elapsed, int num_threads)
{
	if (ok == 0)
	{
		fprintf(report.fp,
			"Total matching lines across %d files: %ld\n",
			totals.files_done, totals.matching);
		printf("Total matching lines across %d files: %ld\n",
		       totals.files_done, totals.matching);
		printf("Elapsed time with %d thread(s): %.6f seconds\n",
		       num_threads, elapsed);
	}
	if (fclose(report.fp) == EOF)
	{
		perror("report file");
		ok = -1;
	}
	pthread_mutex_destroy(&queue.lock);
	pthread_mutex_destroy(&report.lock);
	pthread_mutex_destroy(&totals.lock);
	return (ok == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

/**
 * main - Entry point, counts matching lines in many files with a pool
 * @argc: number of arguments
 * @argv: pattern, report file, number of threads, then the files
 *
 * Return: EXIT_SUCCESS, or EXIT_FAILURE on error
 */
int main(int argc, char *argv[])
{
	struct timespec start, end;
	int num_threads, ok;

	num_threads = argc >= 5 ? atoi(argv[3]) : 0;
	if (num_threads < 1 || num_threads > MAX_THREADS || argv[1][0] == '\0')
	{
		fprintf(stderr, "Usage: %s pattern report.txt "
			"<number_of_threads (1-%d)> file1.log file2.log ...\n",
			argv[0], MAX_THREADS);
		return (EXIT_FAILURE);
	}
	pattern = argv[1];
	queue.files = &argv[4];
	queue.count = argc - 4;
	report.fp = fopen(argv[2], "w");
	if (!report.fp)
	{
		perror(argv[2]);
		return (EXIT_FAILURE);
	}
	pthread_mutex_init(&queue.lock, NULL);
	pthread_mutex_init(&report.lock, NULL);
	pthread_mutex_init(&totals.lock, NULL);

	clock_gettime(CLOCK_MONOTONIC, &start);
	ok = run_pool(num_threads);
	clock_gettime(CLOCK_MONOTONIC, &end);
	return (finish(ok, (end.tv_sec - start.tv_sec) +
		       (end.tv_nsec - start.tv_nsec) / 1e9, num_threads));
}
