#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#define RANGE_START 1
#define RANGE_END 100000
#define NUM_THREADS 8
#define MAX_PERFECT 16

/**
 * struct segment_s - What one thread is asked to do
 *
 * @start: First number to check
 * @end: Last number to check (included)
 * @seconds: Filled by the thread: CPU time its segment took
 */
typedef struct segment_s
{
	long	start;
	long	end;
	double	seconds;
} segment_t;

/* Shared by all threads: only touched while holding lock */
static long perfect_numbers[MAX_PERFECT];
static int stored;
static int total_count;
static pthread_mutex_t lock;

/**
 * seconds_since - time passed since an earlier reading of a clock
 * @clock: CLOCK_MONOTONIC (real time) or CLOCK_THREAD_CPUTIME_ID (CPU
 * time used by the calling thread only)
 * @start: the earlier reading
 *
 * Return: the time passed, in seconds
 */
static double seconds_since(clockid_t clock, const struct timespec *start)
{
	struct timespec now;

	clock_gettime(clock, &now);
	return ((now.tv_sec - start->tv_sec) +
		(now.tv_nsec - start->tv_nsec) / 1e9);
}

/**
 * is_perfect - checks if a number equals the sum of its proper divisors
 * @n: the number to check
 *
 * Description: divisors come in pairs (i, n / i) with i <= sqrt(n), so
 * the loop only runs while i * i <= n and adds both halves of each pair.
 *
 * Return: 1 if n is perfect, 0 otherwise
 */
static int is_perfect(long n)
{
	long i, sum = 1;

	if (n < 2)
		return (0);
	for (i = 2; i * i <= n; i++)
	{
		if (n % i == 0)
		{
			sum += i;
			if (i != n / i)
				sum += n / i;
		}
	}
	return (sum == n);
}

/**
 * search_segment - thread function: checks every number of one segment
 * @arg: pointer to the segment_t of this thread
 *
 * Description: the count is kept in a local variable and added to the
 * shared counter once, at the end. The mutex is taken only then, and
 * each time a perfect number is stored in the shared array.
 *
 * Return: NULL
 */
static void *search_segment(void *arg)
{
	segment_t *seg = arg;
	struct timespec t0;
	long n;
	int local_count = 0;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);
	for (n = seg->start; n <= seg->end; n++)
	{
		if (!is_perfect(n))
			continue;
		local_count++;
		pthread_mutex_lock(&lock);
		if (stored < MAX_PERFECT)
			perfect_numbers[stored++] = n;
		pthread_mutex_unlock(&lock);
	}
	pthread_mutex_lock(&lock);
	total_count += local_count;
	pthread_mutex_unlock(&lock);
	seg->seconds = seconds_since(CLOCK_THREAD_CPUTIME_ID, &t0);
	return (NULL);
}

/**
 * run_threads - splits the range into equal segments, one per thread
 * @segs: array of NUM_THREADS segments, filled here
 *
 * Return: 0 on success, -1 if a thread could not be created
 */
static int run_threads(segment_t segs[NUM_THREADS])
{
	pthread_t tids[NUM_THREADS];
	long size = (RANGE_END - RANGE_START + 1) / NUM_THREADS;
	int i, created = 0, ret = 0;

	for (i = 0; i < NUM_THREADS; i++)
	{
		segs[i].start = RANGE_START + i * size;
		segs[i].end = (i == NUM_THREADS - 1) ?
			RANGE_END : segs[i].start + size - 1;
		if (pthread_create(&tids[i], NULL, search_segment,
				   &segs[i]) != 0)
		{
			fprintf(stderr, "pthread_create failed\n");
			ret = -1;
			break;
		}
		created++;
	}
	for (i = 0; i < created; i++)
		pthread_join(tids[i], NULL);
	return (ret);
}

/**
 * compare_longs - qsort() comparison for two longs, ascending
 * @a: pointer to the first value
 * @b: pointer to the second value
 *
 * Return: negative, zero or positive
 */
static int compare_longs(const void *a, const void *b)
{
	long x = *(const long *)a, y = *(const long *)b;

	return ((x > y) - (x < y));
}

/**
 * main - Entry point, finds the perfect numbers between 1 and 100000
 * @argc: number of arguments
 * @argv: any extra argument (e.g. -v) also prints each thread's CPU time
 *
 * Return: EXIT_SUCCESS, or EXIT_FAILURE on error
 */
int main(int argc, char *argv[])
{
	segment_t segs[NUM_THREADS];
	struct timespec t0;
	double single_time, multi_time;
	long n;
	int i, single_count = 0;

	(void)argv;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (n = RANGE_START; n <= RANGE_END; n++)
		single_count += is_perfect(n);
	single_time = seconds_since(CLOCK_MONOTONIC, &t0);

	pthread_mutex_init(&lock, NULL);
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (run_threads(segs) == -1)
		return (EXIT_FAILURE);
	multi_time = seconds_since(CLOCK_MONOTONIC, &t0);
	pthread_mutex_destroy(&lock);

	qsort(perfect_numbers, stored, sizeof(perfect_numbers[0]),
	      compare_longs);
	printf("Perfect numbers found:");
	for (i = 0; i < stored; i++)
		printf(" %ld", perfect_numbers[i]);
	printf("\nThe synchronized total number of perfect numbers between "
	       "%d and %d is %d\n", RANGE_START, RANGE_END, total_count);
	printf("Single-threaded time: %.6f seconds\n", single_time);
	printf("Multi-threaded time:  %.6f seconds\n", multi_time);
	for (i = 0; argc > 1 && i < NUM_THREADS; i++)
		printf("Thread %d: %6ld to %6ld used %.6f s of CPU\n", i + 1,
		       segs[i].start, segs[i].end, segs[i].seconds);
	if (single_count != total_count)
		fprintf(stderr, "Mismatch: single-threaded found %d\n",
			single_count);
	return (single_count == total_count ? EXIT_SUCCESS : EXIT_FAILURE);
}
