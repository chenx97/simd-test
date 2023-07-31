// Lab4 - skeleton for step 3
#include "nproc.h"
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <time.h>
#include <unistd.h>
#if defined(__SSE2__) || defined(__AVX2__)
#include <emmintrin.h>
#include <x86intrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__mips_msa)
#include <msa.h>
typedef union v128
{
    v2i64 pi64;
    v2f64 pf64;
} v128;
#endif

#define CYAN "\033[36m"
#define BLUE "\033[34m"
#define NORMAL "\033[0m"
#define PROG_LENGTH 128

// values of N,M, and L
#define N 2048
#define M 2048
#define L 2048

pthread_t thread;
sem_t *job_server;
// atomic counter of progress
pthread_mutex_t mutex;
volatile int counter;

jmp_buf env = {};

// A, B, C matrices
double __attribute__((aligned(32))) matrixA[N][M],
    __attribute__((aligned(32))) matrixB[M][L],
    __attribute__((aligned(32))) matrixC[N][L] = {0};

// function prototypes
void initializeMatrix(int r, int c, double matrix[r][c]);
void *multiplyRowSIMD(void *arg);
void *multiplyRowGPR(void *arg);
void printMatrix(int r, int c, double matrix[r][c]);
void printProgress(void *progress);

void handler(int sig) { longjmp(env, 130); }

int main(int argc, char **argv)
{
    int i;
    counter = 0;
    struct sigaction t = {handler, 0, 0};
    sigaction(SIGINT, &t, NULL);
    initializeMatrix(N, M, matrixA); // initialize matrixA with random values
    initializeMatrix(M, L, matrixB); // initialize matrixB with random values
    pthread_t printer;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_mutex_init(&mutex, NULL);
    pthread_create(&printer, NULL, (void *)printProgress, (void *)&counter);
    const int t_count = N;
    void *processor = (argc > 1) ? multiplyRowGPR : multiplyRowSIMD;
    sem_unlink("progressbar");

    int ideal_threads = get_nproc() << 1;
    if (sizeof(size_t) <= 4)
    {
        ideal_threads = ideal_threads > 200 ? 200 : ideal_threads;
    }
    job_server = sem_open("progressbar", O_CREAT, 0644, ideal_threads);
    int exit_code = setjmp(env);
    if (exit_code)
        goto cleanup;
    for (i = 0; i < t_count; i++)
    {
        sem_wait(job_server);
        pthread_create(&thread, &attr, processor, (void *)(size_t)i);
    }

    // Print Matrix A, B, and C
    // your code
    pthread_join(printer, NULL);
cleanup:
    sem_close(job_server);
    sem_unlink("progressbar");
    if (!exit_code)
    {
        t.sa_handler = SIG_DFL;
        sigaction(SIGINT, &t, NULL);
        printf("\n");
        printf("a=");
        printMatrix(N, M, matrixA);
        printf("b=");
        printMatrix(M, L, matrixB);
        printf("c=");
        printMatrix(N, L, matrixC);
    }
    else
    {
        fprintf(stderr, "\n");
    }
    return exit_code;
}

// Initialize matrixA and matrixB with random values
void initializeMatrix(int r, int c, double matrix[r][c])
{
    srand(time(NULL));
    // your code
    for (int i = 0; i < r; i++)
        for (int j = 0; j < c; j++)
            matrix[i][j] = rand() % 10;
}

// thread function: multiply row of A by column of B --> cell of C
void *multiplyRowSIMD(void *arg)
{
#if defined(USE_AVX2) || defined(__SSE2__) || defined(__ARM_NEON) || \
    defined(__mips_msa)
    // your code
    int r = (size_t)arg;
    int i, j;

    // for all columns of matrixB
#if defined(USE_AVX2) && defined(__AVX2__)
    for (i = 0; i < L; i += 4)
#else
    for (i = 0; i < L; i += 2)
#endif /* USE_AVX2 */
    {
#if defined(USE_AVX2) && defined(__AVX2__)
        __m256d result = _mm256_load_pd((void *)&matrixC[r][i]);
#elif defined(__SSE2__)
        __m128d result = _mm_load_pd((void *)&matrixC[r][i]);
#elif defined(__ARM_NEON)
        float64x2_t result = vld1q_f64((void *)&matrixC[r][i]);
#elif defined(__mips_msa)
        v128 result = {.pi64 = __msa_ld_d(&matrixC[r][i], 0)};
#endif /* USE_AVX2 && __AVX2__ */
#if defined(USE_AVX2) && defined(__AVX2__)
        for (j = 0; j < M; j += 4)
        {
            __m256d b0 = _mm256_load_pd(&matrixB[j][i]);
            __m256d b1 = _mm256_load_pd(&matrixB[j + 1][i]);
            __m256d b2 = _mm256_load_pd(&matrixB[j + 2][i]);
            __m256d b3 = _mm256_load_pd(&matrixB[j + 3][i]);
            result = _mm256_fmadd_pd(_mm256_broadcast_sd(&matrixA[r][j]), b0,
                                     result);
            result = _mm256_fmadd_pd(_mm256_broadcast_sd(&matrixA[r][j + 1]),
                                     b1, result);
            result = _mm256_fmadd_pd(_mm256_broadcast_sd(&matrixA[r][j + 2]),
                                     b2, result);
            result = _mm256_fmadd_pd(_mm256_broadcast_sd(&matrixA[r][j + 3]),
                                     b3, result);
        }
        _mm256_store_pd(&matrixC[r][i], result);
#else
        // compute for a pair of matrixC
        for (j = 0; j < M; j += 2)
        {
#ifdef __SSE2__
            __m128d a = _mm_load_pd(&matrixA[r][j]);
            __m128d b0 = _mm_load_pd(&matrixB[j][i]);
            __m128d b1 = _mm_load_pd(&matrixB[j + 1][i]);
            result =
                _mm_add_pd(result, _mm_mul_pd(_mm_shuffle_pd(a, a, 0x0), b0));
            result =
                _mm_add_pd(result, _mm_mul_pd(_mm_shuffle_pd(a, a, 0x3), b1));
#elif defined(__ARM_NEON)
            float64x2_t b0 = vld1q_f64(&matrixB[j][i]);
            float64x2_t b1 = vld1q_f64(&matrixB[j + 1][i]);
            result = vfmaq_f64(result, vdupq_n_f64(matrixA[r][j]), b0);
            result = vfmaq_f64(result, vdupq_n_f64(matrixA[r][j + 1]), b1);
#elif defined(__mips_msa)
            v128 b0 = {.pi64 = __msa_ld_d(&matrixB[j][i], 0)};
            v128 b1 = {.pi64 = __msa_ld_d(&matrixB[j + 1][i], 0)};
            v128 a0 = {.pi64 = __msa_fill_d(*(uint64_t *)&matrixA[r][j])};
            v128 a1 = {.pi64 = __msa_fill_d(*(uint64_t *)&matrixA[r][j + 1])};
            result.pf64 = __msa_fmadd_d(result.pf64, a0.pf64, b0.pf64);
            result.pf64 = __msa_fmadd_d(result.pf64, a1.pf64, b1.pf64);
#endif /* __SSE2__ */
        }
#ifdef __SSE2__
        _mm_store_pd(&matrixC[r][i], result);
#elif defined(__ARM_NEON)
        vst1q_f64(&matrixC[r][i], result);
#elif defined(__mips_msa)
        __msa_st_d(result.pi64, &matrixC[r][i], 0);
#endif /* __SSE2__ */
#endif /* USE_AVX2 && __AVX2__ */
    }
    pthread_mutex_lock(&mutex);
    counter++;
    pthread_mutex_unlock(&mutex);
    sem_post(job_server);
    pthread_exit(0);
#else
    // fallback to gpr
    multiplyRowGPR(arg);
    return NULL;
#endif
}

void *multiplyRowGPR(void *arg)
{
    // your code
    int r = (size_t)arg;
    int i, j;

    // for all columns of matrixB
    for (i = 0; i < L; i++)
    {
        double result = matrixC[r][i];
        // compute for a pair of matrixC
        for (j = 0; j < M; j++)
        {
            result += matrixA[r][j] * matrixB[j][i];
        }
        matrixC[r][i] = result;
    }
    pthread_mutex_lock(&mutex);
    counter++;
    pthread_mutex_unlock(&mutex);
    sem_post(job_server);
    pthread_exit(0);
}

// Print matrices
void printMatrix(int r, int c, double matrix[r][c])
{
    int i, j;
    for (i = 0; i < r; i++)
    {
        for (j = 0; j < c; j++)
            printf("%.0lf ", matrix[i][j]);
        printf("\n");
    }
}

// print progress until no jobs remain
//
// prints to stderr so that you can
// still see the progress while
// redirecting stdout elsewhere
void printProgress(void *progress)
{
    int p = 0;
    while (p < N)
    {
        // refresh every 12ms
        usleep(12000);
        // read progress
        p = *(int *)progress;
        int highlightLength = PROG_LENGTH * p / N;
        int remainder = PROG_LENGTH - highlightLength;
        int i;

        // move to the beginning
        fprintf(stderr, "\r");

        // set character color to cyan
        fprintf(stderr, "[%s", CYAN);

        // print progress
        for (i = 0; i < highlightLength; i++)
            fprintf(stderr, "#");

        // set character color to blue
        fprintf(stderr, BLUE);

        // print the rest of the progress bar
        for (i = 0; i < remainder; i++)
            fprintf(stderr, "-");

        // reset colors here
        fprintf(stderr, "%s]", NORMAL);

        // print fractional progress
        fprintf(stderr, "%d/%d", p, N);
        fflush(stderr);
    }
    fprintf(stderr, "\n");
    pthread_exit(0);
}
