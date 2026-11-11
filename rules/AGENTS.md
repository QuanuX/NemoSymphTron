# QuanuX NemoSymphTron Architectural & Operational Rules

You are operating alongside **QuanuX NemoSymphTron** (`https://github.com/QuanuX/NemoSymphTron`), the standalone NVIDIA Hardware Acceleration, Nemotron Cognitive Orchestration, Tri-Vector (`SHV`/`SCV`/`SIV`) Governance, and `Beads` Multi-Agent Clustering Extension for **QuanuX Symphony**. Follow these rules without exception in Antigravity, Antigravity IDE, Antigravity CLI (`agy`), Gemini CLI, Claude Code, and Codex.

## 1. Core NemoSymphTron & Symphony Doctrine

1. **Standalone Extension Boundary**:
   - `NemoSymphTron` is a standalone extension repository (`https://github.com/QuanuX/NemoSymphTron`), not a core Symphony module.
   - Its `qxctl` extensions (`qxctl nst ...`, `qxctl siv ...`, `qxctl beads ...`, `qxctl shv nst-*`, `qxctl scv nst-*`) are registered only when integrated into an explicit Symphony installation prefix via `scripts/integrate_qxctl_extension.py`.
2. **User Ownership & Explicit Selection**:
   - Every native operation requires an explicit `--prefix`, exact `--version`, and `--tops-id`. Never assume implicit defaults or ambient environment variables.
3. **Bifurcated Compute Isolation (Plane 1 vs. Plane 2)**:
   - **Plane 1 (`nemosymphtron-engine`)**: Sovereign C++26 CUDA & Beads engine. Pool 1 serves Symphony's zero-copy POSIX SHM (`SovereignSharedChannel`) fast-path at high priority (`-1` on Blackwell `sm_120`, `-2` on Vera-Rubin `sm_130`). Pool 2 serves dynamic multi-agent `cudaStream_t` workloads over binary Protobuf IPC.
   - **Plane 2 (`nemosymphtron-mcp` + `nemosymphtron-ctl`)**: Cold-path C++26 MCP (`2025-11-25`) gateway and `qxctl` extension owner. Never place Plane 2 inside a resident trading loop.
4. **Tri-Vector Governance (`SHV`, `SCV`, `SIV`)**:
   - **SHV (Symphony Hardware Vector)**: Enforces physical core affinity (Grace 72-core / Vera 96-core ARM64), `mlockall(MCL_CURRENT | MCL_FUTURE)`, and NVLink-C2C coherent memory partitions.
   - **SCV (Symphony Cloud Vector)**: Governs multi-node RDMA fabrics (ConnectX-7 200GbE RoCE v2 for DGX Spark and ConnectX-8 800G NVLink 6 / SHARPv4 for Vera-Rubin).
   - **SIV (Symphony Inference Vector)**: Enforces 64-bit capability bitmasks (`SIVFlags`) in `O(1)` constant time and delegates all quantitative matrix/stochastic math to deterministic C++26/CUDA kernels rather than LLM token generation.
5. **Beads Multi-Agent Orchestration & CAS File Leases**:
   - Never modify shared workspace files in multi-agent clusters (`PIPELINE`, `WORKER_POOL`, `ADVERSARIAL_CROSS_CHECK`) without first acquiring a Compare-And-Swap (`CAS`) lease via `beads.leases.checkout`.
   - Child `TaskBead` nodes in the Merkle-DAG cannot transition to `EXECUTING` or `COMMITTED` until all parent beads are `COMMITTED`.
6. **Preview Before Every Non-Read Operation & Process Exit Honesty**:
   - Always call `nemosymphtron_connection` first.
   - Every `permission_backed_mutation`, `proposal_only`, and `evidence_only` command requires a single-use session-bound preview (`*_preview` or `nemosymphtron_prepare`) before apply (`*_apply` or `nemosymphtron_apply`).
   - Process exit code `0` means `process_completed`; `semantic_success` is always `"not_asserted"` at the bridge layer. Never blindly retry `unknown_requires_owner_reconciliation`.
