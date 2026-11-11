---
name: nemosymphtron-admin
description: Administer QuanuX NemoSymphTron hardware acceleration, SIV governance, quantitative CUDA kernels, and Beads multi-agent clusters via nemosymphtron-mcp and qxctl extensions.
---

# NemoSymphTron Administration Skill

Use this skill whenever inspecting, configuring, benchmarking, or executing workloads on **QuanuX NemoSymphTron** (`https://github.com/QuanuX/NemoSymphTron`).

## Mandatory Operational Sequence

1. **Inspect Connection**:
   - Call `nemosymphtron_connection` (or read `nemosymphtron://connection`) to verify `qxctl_sha256`, `registry_digest`, and `enabled_nonread_commands`.
2. **Inspect Runtime & Tri-Vector Readiness**:
   - Run `nemosymphtron_nst_status` and `nemosymphtron_nst_doctor` with explicit `prefix`, `version`, and `tops-id`.
   - Inspect hardware isolation via `nemosymphtron_shv_nst_discover` and `nemosymphtron_shv_nst_verify_isolation`.
   - Inspect cluster fabric via `nemosymphtron_scv_nst_inspect_fabric`.
   - Inspect SIV policy and current `state_digest` via `nemosymphtron_siv_inspect`.
3. **Preview & Apply Non-Read Operations**:
   - For any `permission_backed_mutation`, `proposal_only`, or `evidence_only` command, call its `*_preview` tool first, verify the pinned `argv` and `direct_file_sha256`, and then call `*_apply` with the returned `plan_id`.
   - Always supply `--expected-state-digest` (`"absent"` when initializing a new TOPS state or the current `state_digest` returned by `nst.status` / `siv.inspect`).
4. **Reconcile Unknown Outcomes**:
   - If any apply returns `transport_outcome: "unknown_requires_owner_reconciliation"`, stop immediately and run the declared `recovery_command_id` (`nst.status`, `siv.inspect`, `beads.dag.list`, or `beads.leases.list`). Never blindly retry.
