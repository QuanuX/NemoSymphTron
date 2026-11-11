# QuanuX NemoSymphTron Architectural & Operational Rules

You are operating alongside **QuanuX NemoSymphTron** (`https://github.com/QuanuX/NemoSymphTron`), the standalone NVIDIA Hardware Acceleration, Nemotron Cognitive Orchestration, Tri-Vector (`SHV`/`SCV`/`SIV`) Governance, and `Beads` Multi-Agent Clustering Extension for **QuanuX Symphony**. Follow these rules without exception in Antigravity, Antigravity IDE, Antigravity CLI (`agy`), and Gemini CLI.

## 1. Core Doctrine

1. **Verify Connection First**:
   - Begin every session by calling `nemosymphtron_connection` (or reading `nemosymphtron://connection`) to confirm the pinned `qxctl_sha256`, `registry_digest`, `registered_commands`, `routable_commands`, and `enabled_nonread_commands`.
2. **Explicit Target Selection**:
   - Pass explicit `--prefix`, `--version`, and `--tops-id` on all operational routes.
   - Pass `--expected-state-digest` (`"absent"` on initial creation or the exact `sha256:` state digest on subsequent updates) on all state mutations.
3. **Preview Before Apply**:
   - Every non-read command (`permission_backed_mutation`, `proposal_only`, `evidence_only`) requires a single-use 5-minute preview (`nemosymphtron_<route>_preview` or `nemosymphtron_prepare`) before apply (`nemosymphtron_<route>_apply` or `nemosymphtron_apply`).
   - Never treat a transport preview as an owner dry-run (`owner_plan_executed: false`) or authorization grant (`authorization_granted: false`).
4. **Deterministic Quantitative Delegation (SIV Anti-Hallucination Guard)**:
   - Never estimate Monte Carlo VaR/CVaR, Cholesky factorizations, cuOpt portfolio weights, or FFT option prices in LLM prose. Always route quantitative computations to `nemosymphtron-engine` via `nst.quant.run`.
5. **Multi-Agent Beads Coordination**:
   - Use `beads.dag.propose` / `beads.dag.commit` for Merkle-DAG task dependencies, `beads.leases.checkout` / `beads.leases.release` for CAS file locking, and `beads.bus.dispatch` / `beads.bus.poll` for direct P2P subagent messaging.
