/*
 * GCD Benchmark: Euclidean vs. Binary vs. Hybrid (GMP-backed)
 *
 * Section 1 — Native (fixed-width)
 *   8-bit … 64-bit  : euc64 vs bin64 (hardware div & shifts)
 *   128-bit          : euc128 vs bin128 (__uint128_t, software div triggers)
 *
 * Section 2 — GMP (arbitrary precision, mpz_t interface)
 *   Compares mpz_gcd (GMP built-in: Lehmer + HGCD) against hybrid_gcd.
 *
 *   hybrid_gcd dispatch:
 *     both ≤ 64-bit  → euc64        (hardware div, no GMP overhead)
 *     both ≤ 128-bit → bin128       (shifts/sub, avoids __udivti3)
 *     otherwise      → mpz_gcd      (GMP Lehmer / HGCD)
 *
 *   GMP tier range: 64-bit … 2048-bit
 *     64/128-bit tiers show fast-path savings over raw mpz_gcd.
 *     256-bit+ shows hybrid falling through to mpz_gcd with negligible overhead.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <gmp.h>

#define SEED  42ULL

typedef unsigned __int128 u128;

/* ------------------------------------------------------------------ */
/* Native 64-bit                                                        */
/* ------------------------------------------------------------------ */

static uint64_t euc64(uint64_t a, uint64_t b)
{
    while (b) { uint64_t t = a % b; a = b; b = t; }
    return a;
}

static uint64_t bin64(uint64_t a, uint64_t b)
{
    if (!a) return b; if (!b) return a;
    int s = __builtin_ctzll(a | b);
    a >>= __builtin_ctzll(a);
    do { b >>= __builtin_ctzll(b); if (a > b) { uint64_t t=a;a=b;b=t; } b-=a; } while (b);
    return a << s;
}

static uint64_t euc64_cnt(uint64_t a, uint64_t b, uint64_t *c)
{
    while (b) { uint64_t t = a % b; a = b; b = t; ++*c; }
    return a;
}

static uint64_t bin64_cnt(uint64_t a, uint64_t b, uint64_t *c)
{
    if (!a) return b; if (!b) return a;
    int s = __builtin_ctzll(a | b);
    a >>= __builtin_ctzll(a);
    do { b >>= __builtin_ctzll(b); if (a > b) { uint64_t t=a;a=b;b=t; } b-=a; ++*c; } while (b);
    return a << s;
}

/* ------------------------------------------------------------------ */
/* Native 128-bit                                                       */
/* ------------------------------------------------------------------ */

static int ctz128(u128 x)
{
    uint64_t lo = (uint64_t)x;
    return lo ? __builtin_ctzll(lo) : 64 + __builtin_ctzll((uint64_t)(x >> 64));
}

static u128 euc128(u128 a, u128 b)
{
    while (b) { u128 t = a % b; a = b; b = t; }
    return a;
}

static u128 bin128(u128 a, u128 b)
{
    if (!a) return b; if (!b) return a;
    int s = ctz128(a | b);
    a >>= ctz128(a);
    do { b >>= ctz128(b); if (a > b) { u128 t=a;a=b;b=t; } b-=a; } while (b);
    return a << s;
}

static u128 euc128_cnt(u128 a, u128 b, uint64_t *c)
{
    while (b) { u128 t = a % b; a = b; b = t; ++*c; }
    return a;
}

static u128 bin128_cnt(u128 a, u128 b, uint64_t *c)
{
    if (!a) return b; if (!b) return a;
    int s = ctz128(a | b);
    a >>= ctz128(a);
    do { b >>= ctz128(b); if (a > b) { u128 t=a;a=b;b=t; } b-=a; ++*c; } while (b);
    return a << s;
}

/* ------------------------------------------------------------------ */
/* GMP helpers                                                          */
/* ------------------------------------------------------------------ */

static u128 mpz_to_u128(const mpz_t x)
{
    u128 r = 0;
    mp_size_t n = mpz_size(x);
    if (n >= 1) r  = (u128)mpz_getlimbn(x, 0);
    if (n >= 2) r |= (u128)mpz_getlimbn(x, 1) << 64;
    return r;
}

static void u128_to_mpz(mpz_t r, u128 x)
{
    uint64_t hi = (uint64_t)(x >> 64);
    uint64_t lo = (uint64_t)x;
    if (hi) {
        mpz_set_ui(r, hi);
        mpz_mul_2exp(r, r, 64);
        mpz_add_ui(r, r, lo);
    } else {
        mpz_set_ui(r, lo);
    }
}

/* ------------------------------------------------------------------ */
/* Hybrid GCD                                                           */
/* ------------------------------------------------------------------ */

static void hybrid_gcd(mpz_t result, const mpz_t a, const mpz_t b)
{
    /* fast path 1: both fit in one 64-bit limb → hardware Euclidean */
    if (mpz_fits_ulong_p(a) && mpz_fits_ulong_p(b)) {
        mpz_set_ui(result, euc64(mpz_get_ui(a), mpz_get_ui(b)));
        return;
    }
    /* fast path 2: both fit in two 64-bit limbs (≤128-bit) → binary GCD */
    if (mpz_size(a) <= 2 && mpz_size(b) <= 2) {
        u128 r = bin128(mpz_to_u128(a), mpz_to_u128(b));
        u128_to_mpz(result, r);
        return;
    }
    /* large: GMP Lehmer / HGCD */
    mpz_gcd(result, a, b);
}

/* ------------------------------------------------------------------ */
/* RNG / timing                                                         */
/* ------------------------------------------------------------------ */

static uint64_t rng_state;
static uint64_t rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ */
/* Section 1 runners (native fixed-width)                              */
/* ------------------------------------------------------------------ */

#define PAIRS_NATIVE 10000000UL

static void run_64(const char *label, uint64_t mask, FILE *csv)
{
    size_t n = PAIRS_NATIVE;
    uint64_t *as = malloc(n * sizeof *as);
    uint64_t *bs = malloc(n * sizeof *bs);
    if (!as || !bs) { fputs("OOM\n", stderr); exit(1); }

    rng_state = SEED;
    for (size_t i = 0; i < n; ++i) {
        as[i] = (rng_next() & mask) | 1;
        bs[i] = (rng_next() & mask) | 1;
    }

    volatile uint64_t sink = 0;
    double t0;

    t0 = now_sec(); for (size_t i=0;i<n;++i) sink ^= euc64(as[i],bs[i]); double euc_t = now_sec()-t0;
    t0 = now_sec(); for (size_t i=0;i<n;++i) sink ^= bin64(as[i],bs[i]); double bin_t = now_sec()-t0;
    (void)sink;

    uint64_t ec=0, bc=0; sink=0;
    for (size_t i=0;i<n;++i) sink ^= euc64_cnt(as[i],bs[i],&ec);
    for (size_t i=0;i<n;++i) sink ^= bin64_cnt(as[i],bs[i],&bc);
    (void)sink;

    free(as); free(bs);

    double ea=ec/(double)n, ba=bc/(double)n, tr=euc_t/bin_t, or_=ea/ba;
    printf("%-9s  euc %6.3fs %5.1fops  bin %6.3fs %5.1fops  t_ratio=%5.3f  ops_ratio=%5.3f  [%s]\n",
           label, euc_t,ea, bin_t,ba, tr, or_, tr>1?"BINARY WINS":"EUC WINS");
    if (csv)
        fprintf(csv,"native,%s,%zu,%.6f,%.4f,%.6f,%.4f,%.4f,%.4f\n",
                label,(size_t)n,euc_t,ea,bin_t,ba,tr,or_);
}

static void run_128(FILE *csv)
{
    size_t n = PAIRS_NATIVE;
    u128 *as = malloc(n * sizeof *as);
    u128 *bs = malloc(n * sizeof *bs);
    if (!as || !bs) { fputs("OOM\n", stderr); exit(1); }

    rng_state = SEED;
    for (size_t i=0;i<n;++i) {
        as[i] = ((u128)rng_next()<<64|rng_next())|1;
        bs[i] = ((u128)rng_next()<<64|rng_next())|1;
    }

    volatile u128 sink=0; double t0;
    t0=now_sec(); for(size_t i=0;i<n;++i) sink^=euc128(as[i],bs[i]); double euc_t=now_sec()-t0;
    t0=now_sec(); for(size_t i=0;i<n;++i) sink^=bin128(as[i],bs[i]); double bin_t=now_sec()-t0;
    (void)sink;

    uint64_t ec=0,bc=0; sink=0;
    for(size_t i=0;i<n;++i) sink^=euc128_cnt(as[i],bs[i],&ec);
    for(size_t i=0;i<n;++i) sink^=bin128_cnt(as[i],bs[i],&bc);
    (void)sink;
    free(as); free(bs);

    double ea=ec/(double)n,ba=bc/(double)n,tr=euc_t/bin_t,or_=ea/ba;
    printf("%-9s  euc %6.3fs %5.1fops  bin %6.3fs %5.1fops  t_ratio=%5.3f  ops_ratio=%5.3f  [%s]\n",
           "128-bit", euc_t,ea, bin_t,ba, tr, or_, tr>1?"BINARY WINS":"EUC WINS");
    if (csv)
        fprintf(csv,"native,128-bit,%zu,%.6f,%.4f,%.6f,%.4f,%.4f,%.4f\n",
                (size_t)n,euc_t,ea,bin_t,ba,tr,or_);
}

/* ------------------------------------------------------------------ */
/* Section 2 runner (GMP arbitrary precision)                          */
/* ------------------------------------------------------------------ */

static void run_gmp(const char *label, unsigned bits, size_t n,
                    const char *hybrid_path, FILE *csv)
{
    gmp_randstate_t rng;
    gmp_randinit_mt(rng);
    gmp_randseed_ui(rng, SEED);

    mpz_t *as = malloc(n * sizeof(mpz_t));
    mpz_t *bs = malloc(n * sizeof(mpz_t));
    if (!as || !bs) { fputs("OOM\n", stderr); exit(1); }

    for (size_t i = 0; i < n; ++i) {
        mpz_init(as[i]); mpz_urandomb(as[i], rng, bits); mpz_setbit(as[i], 0);
        mpz_init(bs[i]); mpz_urandomb(bs[i], rng, bits); mpz_setbit(bs[i], 0);
    }
    gmp_randclear(rng);

    mpz_t result;
    mpz_init(result);
    volatile unsigned long sink = 0;
    double t0;

    t0 = now_sec();
    for (size_t i = 0; i < n; ++i) {
        mpz_gcd(result, as[i], bs[i]);
        sink ^= mpz_get_ui(result);
    }
    double gmp_t = now_sec() - t0;

    t0 = now_sec();
    for (size_t i = 0; i < n; ++i) {
        hybrid_gcd(result, as[i], bs[i]);
        sink ^= mpz_get_ui(result);
    }
    double hyb_t = now_sec() - t0;
    (void)sink;

    for (size_t i = 0; i < n; ++i) { mpz_clear(as[i]); mpz_clear(bs[i]); }
    free(as); free(bs);
    mpz_clear(result);

    double speedup = gmp_t / hyb_t;
    printf("%-9s  mpz_gcd %6.3fs  hybrid %6.3fs  speedup=%5.3f  hybrid→%-7s  [%s]\n",
           label, gmp_t, hyb_t, speedup, hybrid_path,
           speedup > 1.0 ? "HYBRID WINS" : "GMP WINS");
    if (csv)
        fprintf(csv, "gmp,%s,%zu,%.6f,%.6f,%.4f,%s\n",
                label, n, gmp_t, hyb_t, speedup, hybrid_path);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    FILE *csv = fopen("results.csv", "w");
    if (csv)
        fputs("section,tier,n_pairs,"
              "col_a_time,col_a_ops_or_na,"
              "col_b_time,col_b_ops_or_na,"
              "ratio,extra\n", csv);

    /* ── Section 1: Native fixed-width ── */
    puts("=== Section 1: Native (fixed-width) ===");
    puts("t_ratio = euc/bin  (>1 means binary wins)\n");

    static const struct { const char *label; uint64_t mask; } t64[] = {
        {"8-bit",  0xFFULL}, {"16-bit", 0xFFFFULL},
        {"24-bit", 0xFFFFFFULL}, {"32-bit", 0xFFFFFFFFULL},
        {"40-bit", 0xFFFFFFFFFFULL}, {"48-bit", 0xFFFFFFFFFFFFULL},
        {"56-bit", 0xFFFFFFFFFFFFFFULL}, {"64-bit", 0xFFFFFFFFFFFFFFFFULL},
    };
    for (int i = 0; i < 8; ++i) run_64(t64[i].label, t64[i].mask, csv);
    puts("---");
    run_128(csv);

    /* ── Section 2: GMP ── */
    puts("\n=== Section 2: GMP (mpz_t, arbitrary precision) ===");
    puts("speedup = mpz_gcd_time / hybrid_time  (>1 means hybrid wins)\n");

    static const struct {
        const char *label; unsigned bits; size_t n; const char *path;
    } tgmp[] = {
        {"gmp-64",    64,   500000, "euc64"  },
        {"gmp-128",  128,   500000, "bin128" },
        {"gmp-256",  256,   500000, "mpz_gcd"},
        {"gmp-512",  512,   200000, "mpz_gcd"},
        {"gmp-1024", 1024,  100000, "mpz_gcd"},
        {"gmp-2048", 2048,   50000, "mpz_gcd"},
    };
    for (int i = 0; i < 6; ++i)
        run_gmp(tgmp[i].label, tgmp[i].bits, tgmp[i].n, tgmp[i].path, csv);

    if (csv) { fclose(csv); puts("\nSaved → results.csv"); }
    return 0;
}
