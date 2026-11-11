---
name: nemotron-cognitive-orchestration
description: Route cognitive reasoning tasks across the NVIDIA Nemotron 3 hierarchy (Ultra, Super 120B, Nano 9B, and RAG/VLM) under SIV 64-bit bitmask governance and deterministic CUDA math delegation.
---

# NVIDIA Nemotron Cognitive Orchestration Skill

Use this skill to route prompts and quantitative tasks across the **NVIDIA Nemotron** model family under **Symphony Inference Vector (SIV)** governance.

## Cognitive Hierarchy & Routing

1. **Nemotron 3 Ultra (`nvidia/nemotron-4-400b-moe-nvfp4`)**:
   - Deep architectural synthesis, formal KKT/stochastic proof verification, and multi-agent Merkle-DAG planning.
2. **Nemotron 3 Super 120B (`nvidia/nemotron-3-super-120b-a12b`)**:
   - Hybrid Mamba-Transformer MoE (120B total, 12B active parameters) optimized for high-throughput multi-agent tool calling, C++26/CUDA code generation, and quantitative orchestration.
3. **Nemotron 3 Nano (`nvidia/nemotron-nano-9b-nvfp4`)**:
   - Sub-10ms edge guardrails, syntax validation, and speculative decoding draft generation.
4. **Nemotron RAG / VLM**:
   - Multimodal document parsing, financial PDF table extraction, and dense embedding retrieval.

## SIV Anti-Hallucination Rule

- **Never** perform Monte Carlo VaR/CVaR, Cholesky matrix factorization, cuOpt portfolio optimization, or FFT option pricing in LLM token generation.
- Use `qxctl siv model route` (`nemosymphtron_siv_model_route_preview` -> `_apply`) to extract structured parameters from natural-language directives and delegate execution to `qxctl nst quant run`.
