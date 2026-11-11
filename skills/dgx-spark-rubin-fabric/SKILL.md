---
name: dgx-spark-rubin-fabric
description: Configure and benchmark NVIDIA DGX Spark (Grace-Blackwell GB10 sm_120, 128GB LPDDR5x, ConnectX-7 200G RoCE v2) and Vera-Rubin (Vera ARM64 + Rubin sm_130, 288GB HBM4, ConnectX-8 800G NVLink 6 / SHARPv4) hardware and fabrics.
---

# DGX Spark & Vera-Rubin Hardware & Fabric Skill

Use this skill when provisioning, locking, or benchmarking **SHV (Symphony Hardware Vector)** and **SCV (Symphony Cloud Vector)** topologies.

## Supported Hardware Generations

1. **NVIDIA DGX Spark (`BLACKWELL_GB10_SM120`, `sm_120`)**:
   - **CPU/GPU**: Grace-Blackwell GB10 Superchip (72-core ARM64 + Blackwell GPU), NVLink-C2C coherent interconnect (900 GB/s), 128 GB unified LPDDR5x memory.
   - **Core Partitioning**: Cores `0-15` pinned to Sovereign Symphony Fast-Path; `16-23` to CUDA Daemon Pollers; `24-31` to Dynamic Agent Gateway.
   - **Network Fabric**: Dual-port NVIDIA ConnectX-7 200GbE QSFP56 RoCE v2 (`enp1s0f0np0`, `enp1s0f0np1`, MTU 9000, GPUDirect RDMA).
2. **NVIDIA Vera-Rubin (`VERA_RUBIN_SM130`, `sm_130`)**:
   - **CPU/GPU**: 96-core Vera ARM64 v9 CPU + Rubin GPU (`sm_130` with native `NVFP4`), NVLink-C2C Gen 2 (1.8 TB/s), 288 GB HBM4 + LPDDR6 unified memory.
   - **Network Fabric**: ConnectX-8 SuperNIC (800G InfiniBand / RoCE) + NVLink 6 Switch (3.6 TB/s per GPU) with **SHARPv4** in-network collective reductions and **BlueField-4 DPU** lock-manager offload.

## Key Operations

- Discover & verify SHV: `qxctl shv nst-discover`, `qxctl shv nst-verify-isolation`, `qxctl shv nst-lock`, `qxctl shv nst-benchmark-bus`.
- Inspect & bring up SCV fabric: `qxctl scv nst-inspect-fabric`, `qxctl scv nst-test-fabric`, `qxctl scv nst-cluster-up`.
