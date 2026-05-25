# GCD 演算法效能實驗報告
## Binary GCD (Stein's Algorithm) vs. Euclidean GCD

---

## 實驗環境

| 項目 | 規格 |
|------|------|
| CPU | AMD Ryzen 7 5800X（VM 2 vCPU） |
| 架構 | x86-64 |
| 記憶體 | 7.7 GiB |
| OS | Ubuntu 24.04 / Linux 6.17.0-23-generic |
| 編譯器 | GCC 13.3.0，`-O2` |

---

## 演算法說明

### Euclidean GCD
反覆做模除，每次將問題縮小：
```
gcd(a, b) = gcd(b, a % b)
```
關鍵成本：**modulo 運算**（`%`），直接對應 CPU 的除法指令。

### Binary GCD（Stein's Algorithm）
用位移和減法取代除法：
```
- 若 a, b 均為偶數 → gcd = 2 × gcd(a/2, b/2)
- 若其中一個為偶數 → 移除 2 因子
- 若均為奇數      → gcd(|a-b|/2, min(a,b))
```
關鍵成本：**位移（`>>`）與減法**，在 64-bit 硬體上均為單週期。

---

## Benchmark 方法論

| 項目 | 做法 |
|------|------|
| 計時工具 | `clock_gettime(CLOCK_MONOTONIC)` |
| 資料產生 | xorshift64 RNG，seed=42，事先產完放入陣列（產生成本不計入計時） |
| 計時 pass | 純計算，無 instrumentation overhead |
| 計次 pass | 獨立跑一遍，`++count` 不污染計時數據 |
| 防最佳化 | `volatile sink` XOR 每個結果，阻止編譯器 dead-code elimination |
| 樣本數 | 每個 tier 1,000 萬對隨機數 |
| perf | VM 的 `perf_event_paranoid=4`，無法存取 hardware counter |

---

## 實驗結果

### 主要 Benchmark（10M 對/tier）

| Tier | Euclidean 時間 | Euc ops/call | Binary 時間 | Bin ops/call | t\_ratio | ops\_ratio | 勝者 |
|------|--------------|-------------|------------|-------------|---------|-----------|------|
| 8-bit  | 0.169s | 4.9  | 0.170s | 5.7  | 0.993 | 0.861 | **EUCLIDEAN** |
| 16-bit | 0.312s | 9.6  | 0.346s | 11.4 | 0.900 | 0.846 | **EUCLIDEAN** |
| 24-bit | 0.451s | 14.3 | 0.514s | 17.0 | 0.877 | 0.839 | **EUCLIDEAN** |
| 32-bit | 0.586s | 19.0 | 0.665s | 22.7 | 0.881 | 0.836 | **EUCLIDEAN** |
| 40-bit | 0.679s | 23.6 | 0.815s | 28.3 | 0.833 | 0.835 | **EUCLIDEAN** |
| 48-bit | 0.747s | 28.3 | 0.966s | 34.0 | 0.774 | 0.833 | **EUCLIDEAN** |
| 56-bit | 0.847s | 33.0 | 1.121s | 39.6 | 0.755 | 0.832 | **EUCLIDEAN** |
| 64-bit | 0.968s | 37.6 | 1.277s | 45.3 | 0.757 | 0.832 | **EUCLIDEAN** |
| **128-bit** | **3.477s** | **75.0** | **3.269s** | **90.4** | **1.063** | **0.830** | **BINARY** |

> `t_ratio = euc_time / bin_time`，大於 1 表示 Binary 較快。  
> `ops_ratio = euc_ops / bin_ops`，小於 1 表示 Euclidean iteration 次數較少。

### 除法原始成本 Micro-benchmark（50M 次單一 mod）

| 操作 | 時間 | ns/op | 倍率 |
|------|------|-------|------|
| 64-bit `%`  | 0.083s | 1.7 ns | 1.0× |
| 128-bit `%` | 0.230s | 4.6 ns | **2.8×** |

---

## 分析與結論

### 關鍵觀察

**ops\_ratio 全程穩定在 ~0.83**：無論幾 bit，Euclidean 永遠比 Binary 少做約 17% 的 iteration。演算法層次上，Euclidean 本來就更精簡。

### 為什麼 64-bit 以下 Euclidean 贏？

硬體除法器快（1.7 ns/op），iteration 少 + 每次 op 便宜 = 雙重優勢。Binary 的位移迴圈 overhead 完全追不上。

### 為什麼 128-bit 翻轉？

`__uint128_t % __uint128_t` 觸發 libgcc 的 `__udivti3` 軟體模擬（4.6 ns/op，2.8× 更貴）。此時 Euclidean 每次 mod 成本暴增，Binary 的位移操作仍是單週期，最終翻盤。

### 修正後的假說

| 原始假說 | 結論 |
|---------|------|
| 「數字越大 → Binary 贏」 | ❌ 不正確 |
| 「除法指令越貴 → Binary 贏」 | ✅ 正確 |

在現代 x86-64 硬體上，64-bit 硬體除法器快到讓 Euclidean 在整個原生寬度都保有優勢。交叉點出現在「溢出硬體除法器寬度」的時候，不是「數字變大」本身。

---

*實驗程式碼：`gcd_bench.c`、`div_cost.c`*
