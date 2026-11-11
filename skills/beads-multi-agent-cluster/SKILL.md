---
name: beads-multi-agent-cluster
description: Coordinate multi-agent swarms (Antigravity, Claude Code, Codex, Cursor, Gemini CLI) using Beads Merkle-DAG task graphs, Compare-And-Swap (CAS) file leases, and P2P direct subagent messaging.
---

# Beads Multi-Agent Cluster Orchestration Skill

Use this skill to coordinate concurrent AI coding and quantitative research agents without race conditions or context bloat.

## Three Core Mechanisms

1. **Merkle-DAG Task Graph (`qxctl beads dag ...`)**:
   - Every task is an immutable content-addressed `TaskBeadNode` whose `bead_id` is a SHA-256 Merkle digest of its capability, payload, timestamp, and `parent_bead_ids`.
   - A child bead cannot transition to `EXECUTING` or `COMMITTED` until all of its parent beads are `COMMITTED`.
2. **Compare-And-Swap (CAS) File Leases (`qxctl beads leases ...`)**:
   - Before editing any shared source file or model artifact, an agent must call `beads leases checkout` with its `agent-id`, `file`, `duration-sec`, and `expected-sha256`.
   - Conflicting checkouts by other agents fail closed (`CAS Lease Conflict`) until the holder releases the lease (`beads leases release`) or an operator runs `beads leases unlock`.
3. **Direct P2P Subagent Bus (`qxctl beads bus ...`)**:
   - Supports `PIPELINE`, `WORKER_POOL`, and `ADVERSARIAL_CROSS_CHECK` orchestration patterns via `beads bus dispatch` and `beads bus poll`.
