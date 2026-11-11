# QuanuX NemoSymphTron

**QuanuX NemoSymphTron** (`https://github.com/QuanuX/NemoSymphTron`) is a standalone C++26 NVIDIA hardware acceleration, **NVIDIA Nemotron** cognitive orchestration, Tri-Vector (`SHV`/`SCV`/`SIV`) governance, and **Beads** multi-agent clustering extension for **QuanuX Symphony** (`https://github.com/QuanuX/Symphony`).

It is engineered for **NVIDIA DGX Spark** (Grace-Blackwell `GB10` `sm_120`, 128 GB unified LPDDR5x, ConnectX-7 200GbE RoCE v2) and next-generation **NVIDIA Vera-Rubin** (Vera ARM64 + Rubin `sm_130`, 288 GB HBM4, ConnectX-8 800G / NVLink 6 Switch with SHARPv4 and BlueField-4 DPU offload), while providing a deterministic C++26 IEEE-754 hardware-emulation fallback on macOS and non-GPU development hosts.

---

## Architecture Overview

NemoSymphTron strictly separates real-time quantitative execution from cold-path agentic orchestration across two pure C++26 planes:

1. **Plane 1 — Sovereign C++26 CUDA & Beads Engine (`nemosymphtron-engine`)**:
   - **Pool 1 (Sovereign Symphony Fast-Path)**: Zero-copy POSIX shared memory (`SovereignSharedChannel` at `/dev/shm/nemosymphtron_fastpath`) with cache-line-aligned (`alignas(64)`) lock-free SPSC ring buffers (`LockFreeRingBuffer<T, 256>`) and dedicated high-priority CUDA stream (`priority = -1` on Blackwell, `-2` on Rubin). Sub-microsecond IPC latency, completely immune to LLM/MCP traffic.
   - **Pool 2 (Dynamic Multi-Agent Stream Pool)**: Accepts length-prefixed binary Protobuf v3 frames (`proto/nemosymphtron_wire.proto`) over a local `0600` Unix domain socket (`AF_UNIX`), validates 64-bit **SIV** capability bitmasks (`SIVFlags`) in `O(1)` constant time, provisions per-agent non-blocking CUDA streams (`cudaStreamNonBlocking`), and executes 6 quantitative & distributed kernels:
     1. `MONTE_CARLO_VAR` (Multi-asset Geometric Brownian Motion VaR / CVaR)
     2. `CHOLESKY_DECOMP` (Positive-definite covariance matrix factorization)
     3. `CUOPT_PORTFOLIO` (Mean-variance / Sharpe ratio portfolio optimization)
     4. `TX_RISK_SCORE` (Real-time transaction anomaly scoring)
     5. `FFT_OPTION_PRICING` (Carr-Madan Fast Fourier Transform option pricing)
     6. `NCCL_MULTI_NODE_SIM` (Multi-node RoCE v2 / NVLink 6 SHARPv4 All-Reduce collective)
2. **Plane 2 — C++26 MCP (`2025-11-25`) Gateway & `qxctl` Extension Owner (`nemosymphtron-mcp` & `nemosymphtron-ctl`)**:
   - Exposes 35 registered `qxctl` extension commands (`qxcmd:nemosymphtron:*`) and 54 MCP tools (`nemosymphtron_connection` + 53 operation-specific read/preview/apply tools in `catalog/tools.json`).
   - Enforces single-use 5-minute session-bound previews (`owner_plan_executed: false`, `authorization_granted: false`), direct input file SHA-256 pinning, and Compare-And-Swap (`--expected-state-digest`) state mutations.

---

## Building & Testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
cmake --install build --prefix .
```

## Integrating with a Symphony Installation

NemoSymphTron does not modify the upstream `QuanuX/Symphony` repository. Its `qxctl` extensions are registered only when attached to an explicit Symphony installation prefix:

```bash
python3 scripts/integrate_qxctl_extension.py register \
  --symphony-prefix /opt/symphony \
  --nemosymphtron-ctl ./bin/nemosymphtron-ctl \
  --extension-catalog ./catalog/qxctl_extension_commands.json \
  --version 0.2.0-alpha.1 \
  --tops-id 11111111-2222-3333-4444-555555555555
```

## License

Licensed under `AGPL-3.0-only` (see [LICENSE](file:///Users/Duncan/Documents/NemoSymphTron/LICENSE)).
