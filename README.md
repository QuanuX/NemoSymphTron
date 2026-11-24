<div align="center">
  <img src="assets/nemosymphtron-hero-terminal.svg" width="100%" alt="QuanuX NemoSymphTron — C++26 NVIDIA Grace-Blackwell & Vera-Rubin Hardware Acceleration, Nemotron-3 Orchestration, and Tri-Vector Governance" />
  <img src="assets/nemosymphtron-pillars-matrix.svg" width="100%" alt="NemoSymphTron 6 Architectural Pillars and NVIDIA Stack Integration Matrix" />
  <img src="assets/nemosymphtron-formal-specs.svg" width="100%" alt="NemoSymphTron Dual-Plane C++26 Runtime, 6 Quantitative Kernels, and CTest Verification" />
</div>

<br />

## Try NemoSymphTron with Symphony beta.2.1

The `0.2.0-beta.2.1` release installs a native CPU experiment runtime, four skills and a private, read-only connection. The Codex and Antigravity plugins use a separate MCP server named `nemosymphtron`. You can keep the Symphony plugin enabled in the same client to inspect both command registries. Each server has its own pinned native owner.

**Requirements** include Python 3.9+, macOS 15 or later (Intel or Apple Silicon), or Ubuntu 24.04/26.04 (x86-64 or ARM64). For Codex, install a client with `codex plugin marketplace` support. For Antigravity, use a client with local plugin support. The release installer gets its native archive and SHA-256 digest from the tagged GitHub release. It asks for no administrator privileges, model credentials or GPU drivers.

Download the installer once, then run the command for your client.

```sh
curl -fL -o install-nemosymphtron.py https://github.com/QuanuX/NemoSymphTron/releases/download/v0.2.0-beta.2.1/install-nemosymphtron.py
```

**Codex**

```sh
python3 install-nemosymphtron.py --client codex
```

**Antigravity**

```sh
python3 install-nemosymphtron.py --client antigravity
```

Antigravity installs this user’s plugin under `~/.gemini/config/plugins/nemosymphtron`. To limit it to an open project, append `--workspace "$PWD"`. `agy plugin list --json` lists user plugins. For a workspace install, run `agy plugin validate --strict --json .agents/plugins/nemosymphtron`.

Start a new Codex session or reopen the Antigravity workspace. Try this prompt in either client.

> Call `nemosymphtron_connection` and `nemosymphtron_commands_list`. Report the bridge version and number of routable commands. If Symphony is installed, also call `symphony_connection` and report the separate Symphony registry. Use read-only tools.

The Nemo connection should report `0.2.0-beta.2.1`, 35 routable native commands, and an empty list of enabled non-read commands. These results come from the local executable. The CPU package starts no resident engine or model server. The built-in cognitive planner parses a directive locally and returns a recommended tool and model label. Model weights, external NIM/TensorRT inference, CUDA execution and DGX/Rubin fabric qualification are separate from this experiment. To explore the native CUDA configuration, build from source on a compatible NVIDIA system.

The default prefix is `~/.local/share/quanux/nemosymphtron`. Use `--prefix` to select another location. Run the installer again with the other `--client` to enable both. To attach a Nemo command capsule to a specific Symphony prefix, follow the explicit registration example below. [Release assets](https://github.com/QuanuX/NemoSymphTron/releases/tag/v0.2.0-beta.2.1) · [Support](https://support.quanux.org)

## `SYSTEM TOPOLOGY // MERMAID SPECIFICATION`

```mermaid
flowchart TD
    subgraph Harnesses["Dynamic Agent Fleet (Homogeneous & Heterogeneous Clusters)"]
        A1["Antigravity / agy\n(Operator & Priority Interrupt)"]
        A2["Symphony-Codex\n(Pipeline & Skills Runner)"]
        A3["Claude / Cursor / Agent N\n(Architect, Worker, Red-Team Auditor)"]
    end

    subgraph SymphonyCore["Sovereign Symphony Runtime (Unmodified C++26)"]
        SC["symphonyd / SBV C++26 Core\nZero Python • Zero MCP on Hot-Path"]
    end

    subgraph Plane2["Plane 2: nemosymphtron-mcp & nemosymphtron-ctl (Pure C++26 Gateway & CLI Owner)"]
        MCP["C++26 MCP 2025-11-25 Server\n(stdio & Same-UID Unix Socket IPC)"]
        SIV["SIV — Intelligence Vector Enforcer\n(Role Allowlists, Risk Ceilings, 64-bit SIVFlags)"]
        BEADS["Beads Coordination Engine\n(Merkle Task DAG, CAS File Leases, P2P Bus)"]
        NIM["Nemotron Cognitive Broker\n(NIM / TensorRT-LLM OpenAI-Compatible Client)"]
    end

    subgraph Plane1["Plane 1: nemosymphtron-engine (Pure C++26 Hardware Daemon)"]
        SHM["Pool 1: Sovereign Symphony Fast-Path\nPOSIX SHM Lock-Free Ring Buffer\n(/nemosymphtron_fastpath • Priority -1/-2)"]
        DYN["Pool 2: Dynamic Agent Stream Pool\nOn-Demand cudaStream_t + curandGenerator_t per agent_id\n(Token-Bucket Memory Governor & Idle Eviction)"]
        HW["SHV & SCV Hardware Enforcer\n• Grace/Vera ARM64 Core Affinity & mlockall\n• Blackwell SM120 (LPDDR5x) & Rubin SM130 (HBM4)\n• ConnectX-7 200G RoCEv2 & ConnectX-8 800G NVLink 6 / SHARPv4"]
    end

    SC <-->|"Zero-Copy POD Structs (<200ns)\nPOSIX SHM Ring Buffer"| SHM
    A1 & A2 & A3 <-->|"MCP 2025-11-25\n(stdio / Unix Socket IPC)"| MCP
    MCP --> SIV
    SIV --> BEADS
    SIV <-->|"Strict Function Calling\nNemotron-3 Nano / Super / Ultra"| NIM
    SIV <-->|"Length-Prefixed Wire Protocol\n(nst_ipc.sock + 64-bit siv_token_flags)"| DYN
    SHM & DYN --> HW
```

---

## `QUICKSTART // BUILD, VERIFY & ATTACH TO SYMPHONY`

### 1. Build & Run All 6 CTest Verification Suites

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
cmake --install build --prefix .
```

### 2. Attach `qxctl` Extensions to an Explicit Symphony Installation

NemoSymphTron does not modify the upstream [`QuanuX/Symphony`](https://github.com/QuanuX/Symphony) repository. Its 35 `qxctl` extension commands (`qxcmd:nemosymphtron:*`) are registered only when attached to an explicit Symphony installation prefix:

```bash
python3 scripts/integrate_qxctl_extension.py register \
  --symphony-prefix /opt/symphony \
  --nemosymphtron-ctl ./bin/nemosymphtron-ctl \
  --extension-catalog ./catalog/qxctl_extension_commands.json \
  --version 0.2.0-beta.2.1 \
  --tops-id 11111111-2222-3333-4444-555555555555
```

### 3. Repository Structure

```text
NemoSymphTron/
├── assets/                          # quanux.org dark-mode SVG architecture, hero, & spec panels
├── catalog/
│   ├── qxctl_extension_commands.json # 35 qxcmd:nemosymphtron:* extension command definitions
│   └── tools.json                    # 53 operation-specific MCP tools + nemosymphtron_connection
├── config/                          # systemd cgroups v2 slice & Netplan 200GbE RoCEv2 configs
├── include/nemosymphtron/           # Pure C++26 headers (SHV, SCV, SIV, Beads, FastPath, IPC)
├── proto/nemosymphtron_wire.proto   # Binary IPC wire protocol specification
├── rules/AGENTS.md                  # Architectural & operational doctrine for AI agents
├── schemas/                         # JSON Schema Draft 2020-12 contracts (SHV, SCV, SIV, Beads)
├── scripts/                         # qxctl integration, DGX Spark / Rubin audit, & catalog tools
├── skills/                          # 4 specialized agent skills (tri-vector, beads, spark, rubin)
├── src/                             # C++26 sources (engine, ctl, mcp, fastpath-client, transport)
├── templates/                       # Hardware, RoCEv2 fabric, SIV policy, & Beads cluster templates
└── tests/                           # C++26 unit tests & Python multi-agent / MCP integration tests
```

### 4. License

Licensed under **`AGPL-3.0-only`** — see [LICENSE](LICENSE).

<div align="center">
  <sub><strong>QUANUX VENTURES LLC</strong> // <a href="https://quanux.org">QUANUX.ORG</a> · <a href="https://docs.quanux.org">DOCS.QUANUX.ORG</a> · <a href="https://github.com/QuanuX/Symphony">QUANUX SYMPHONY</a> // AGENTIC-FIRST · HUMANS WELCOME</sub>
</div>
