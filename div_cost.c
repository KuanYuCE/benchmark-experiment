/* Measures the raw cost of one 64-bit vs. one 128-bit modulo operation. */
#include <stdint.h>
#include <stdio.h>
#include <time.h>

typedef unsigned __int128 u128;

#define N 50000000UL

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(void)
{
    volatile uint64_t a64 = 0xFEDCBA9876543210ULL;
    volatile uint64_t b64 = 0x123456789ABCDEFULL;
    volatile u128 a128 = ((u128)0xFEDCBA9876543210ULL << 64) | 0xABCDEF0123456789ULL;
    volatile u128 b128 = ((u128)0x123456789ABCDEFULL  << 64) | 0xFEDCBA9876543210ULL;

    volatile uint64_t sink64 = 0;
    volatile u128    sink128 = 0;

    double t0 = now_sec();
    for (size_t i = 0; i < N; ++i) sink64  ^= (a64  % b64);
    double t64 = now_sec() - t0;

    t0 = now_sec();
    for (size_t i = 0; i < N; ++i) sink128 ^= (a128 % b128);
    double t128 = now_sec() - t0;

    (void)sink64; (void)sink128;

    printf("64-bit  mod: %.3f s  →  %.1f ns/op\n",  t64,  t64  / N * 1e9);
    printf("128-bit mod: %.3f s  →  %.1f ns/op\n", t128, t128 / N * 1e9);
    printf("128-bit is %.1fx more expensive per modulo\n", t128 / t64);
    return 0;
}
