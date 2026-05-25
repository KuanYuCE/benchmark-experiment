# GCD Algorithm Benchmark

Empirical comparison of **Euclidean GCD**, **Binary GCD (Stein's algorithm)**, and a **Hybrid GCD (GMP-backed)** across operand sizes from 8-bit to 2048-bit.

The experiment was designed to verify the common claim that *"Binary GCD outperforms Euclidean GCD for large numbers"* — and to find the actual causal condition behind the crossover.

## Results Summary

### Native fixed-width (8-bit → 128-bit)

| Tier | Winner | t\_ratio (euc/bin) |
|------|--------|--------------------|
| 8-bit → 64-bit | **Euclidean** | 0.73 – 0.99 |
| **128-bit** | **Binary** | **1.016** |

### GMP arbitrary precision (mpz\_t interface)

| Tier | Winner | Notes |
|------|--------|-------|
| 64-bit / 128-bit | **mpz\_gcd** | GMP's internal fast paths beat manual dispatch |
| 256-bit → 2048-bit | Tie (~1.00) | hybrid falls through to mpz\_gcd; overhead negligible |

### Corrected hypothesis

> ❌ "Larger numbers → Binary wins"
>
> ✅ **"Expensive division → Binary wins"**

The crossover at 128-bit is caused by `__uint128_t %` triggering libgcc's `__udivti3` software emulation (**4.6 ns/op** vs **1.7 ns/op** for hardware `div`). Binary GCD's shifts and subtractions are unaffected, so it wins — despite doing ~20% more iterations in every tier.

Below 128-bit, hardware division is fast enough that Euclidean's lower iteration count wins decisively. On hardware with native 128-bit division, Euclidean would win there too.

## Build & Run

**Requirements:** GCC, `libgmp-dev`

```bash
sudo apt-get install -y libgmp-dev

# Main benchmark (~3 min, writes results.csv)
gcc -O2 -o gcd_bench gcd_bench.c -lgmp
./gcd_bench

# Division cost micro-benchmark (~30 sec)
gcc -O2 -o div_cost div_cost.c
./div_cost
```

## Visualization

Open `visualize.html` in a browser (requires internet for Chart.js CDN). Contains 8 interactive charts:

- Wall-clock time comparison across all tiers
- t\_ratio trend showing the 128-bit crossover
- ops/call showing Euclidean's consistent ~17% iteration advantage
- ops\_ratio flatline proving iteration ratio is independent of operand size
- Division raw cost micro-benchmark (the causal explanation)
- GMP tier: mpz\_gcd vs hybrid\_gcd time comparison
- GMP speedup ratio showing hybrid's fast paths lose to GMP's internal paths
- GMP scaling curve (Lehmer/HGCD efficiency at 256-bit → 2048-bit)

## Environment

| Item | Spec |
|------|------|
| CPU | AMD Ryzen 7 5800X (VM, 2 vCPU) |
| Architecture | x86-64 |
| Memory | 7.7 GiB |
| OS | Ubuntu 24.04 / Linux 6.17.0-23-generic |
| Compiler | GCC 13.3.0, `-O2` |
| GMP | system libgmp-dev |

## Methodology

- **Timing:** `clock_gettime(CLOCK_MONOTONIC)`, generation cost excluded (pairs pre-generated)
- **Ops counting:** separate instrumented pass so `++count` never distorts timing
- **Anti-elimination:** `volatile sink` XOR'd with every result
- **RNG:** xorshift64, seed = 42 (reproducible)
- **Sample size:** 10M pairs/tier (native); 50K–500K pairs/tier (GMP)

## Implications for xv6-riscv

If implementing GCD as a kernel syscall in xv6-riscv:

- Use `euc64` (pure `uint64_t`, zero dependencies, safe in `-nostdlib` kernel)
- **Do not use `euc128`** — the `%` operator on `__uint128_t` emits a `call __udivti3` (libgcc), which is not linked in the kernel
- `bin128` is kernel-safe (shifts and subtraction only, no libgcc calls), but RISC-V base ISA lacks a native CTZ instruction (needs Zbb extension), so the hardware advantage over Euclidean may not hold on RV64
- GMP is entirely unsuitable for kernel use (requires `malloc`, user-space privilege level)
