#pragma once

#include <chrono>
#include <cstdint>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

namespace nemosymphtron::siv {

// 64-bit SIV Bitmask evaluated by Plane 1 C++26 Engine in O(1) constant time
enum SIVFlags : std::uint64_t {
  SIV_NONE                         = 0ULL,
  SIV_AUDITED_BEAD                 = 1ULL << 0, // Execution is tracked in Beads DAG
  SIV_PRIORITY_BOOST               = 1ULL << 1, // Permitted priority interrupt override (e.g. Antigravity)
  SIV_RISK_CHECK_PASSED            = 1ULL << 2, // Complies with FINRA/SEC risk & path ceilings
  SIV_UNHEDGED_ALLOWED             = 1ULL << 3, // Allowed exploratory/speculative runs
  SIV_DETERMINISTIC_MATH_DELEGATED = 1ULL << 4  // Verified math delegated to CUDA/C++ rather than LLM
};

inline constexpr bool ValidateSIVCompliance(std::uint64_t token_flags) noexcept {
  constexpr std::uint64_t kRequiredFlags =
      SIV_AUDITED_BEAD | SIV_RISK_CHECK_PASSED | SIV_DETERMINISTIC_MATH_DELEGATED;
  return (token_flags & kRequiredFlags) == kRequiredFlags;
}

struct AgentRoleAuthorization {
  std::string id;
  std::string role_class{"worker"};
  std::vector<std::string> allowed_tools;
  std::uint32_t max_tokens_per_call{8192};
  bool can_spawn_subagents{false};
  bool priority_override{false};
};

struct SIVAuditRecord {
  std::uint64_t timestamp_ns{0};
  std::string agent_id;
  std::string tool_name;
  bool permitted{false};
  std::uint64_t siv_bitmask{0};
  std::string detail;
};

class SIVPolicyEnforcer {
public:
  SIVPolicyEnforcer() { LoadDefaultPolicy(); }

  void LoadDefaultPolicy() {
    name_ = "dgx-spark-quant-intelligence";
    version_ = "1.0.0";
    provider_ = "nvidia-nim";
    endpoint_ = "http://127.0.0.1:8000/v1";
    primary_model_ = "nvidia/nemotron-3-super-120b-a12b";
    auxiliary_model_ = "nvidia/nemotron-nano-9b-nvfp4";
    rubin_moe_model_ = "nvidia/nemotron-4-400b-moe-nvfp4";
    enforce_nemo_guardrails_ = true;
    enforce_deterministic_cuda_ = true;
    max_monte_carlo_paths_ = 100000000ULL;
    max_matrix_dimensions_ = 10000U;

    roles_.clear();
    RegisterRole({
        .id = "claude-architect",
        .role_class = "strategic_orchestrator",
        .allowed_tools = {"nemotron_broker", "compute_var_simulation", "cholesky_factor", "optimize_portfolio_cuopt", "score_transaction_risk", "fft_option_price", "nccl_allreduce", "beads_lease", "beads_p2p"},
        .max_tokens_per_call = 8192,
        .can_spawn_subagents = true,
        .priority_override = false,
    });
    RegisterRole({
        .id = "antigravity-operator",
        .role_class = "interactive_terminal",
        .allowed_tools = {"nemotron_broker", "compute_var_simulation", "cholesky_factor", "optimize_portfolio_cuopt", "score_transaction_risk", "fft_option_price", "nccl_allreduce", "beads_lease", "beads_lease_override", "beads_p2p", "health_inspect"},
        .max_tokens_per_call = 8192,
        .can_spawn_subagents = true,
        .priority_override = true,
    });
    RegisterRole({
        .id = "symphony-codex",
        .role_class = "pipeline_executor",
        .allowed_tools = {"nemotron_broker", "compute_var_simulation", "cholesky_factor", "optimize_portfolio_cuopt", "score_transaction_risk", "fft_option_price", "nccl_allreduce", "beads_lease", "beads_p2p"},
        .max_tokens_per_call = 16384,
        .can_spawn_subagents = false,
        .priority_override = false,
    });
    RegisterRole({
        .id = "cursor-worker",
        .role_class = "specialist_worker",
        .allowed_tools = {"compute_var_simulation", "cholesky_factor", "optimize_portfolio_cuopt", "fft_option_price", "beads_lease", "beads_p2p"},
        .max_tokens_per_call = 4096,
        .can_spawn_subagents = false,
        .priority_override = false,
    });
    RegisterRole({
        .id = "default-agent",
        .role_class = "dynamic_harness",
        .allowed_tools = {"nemotron_broker", "compute_var_simulation", "cholesky_factor", "optimize_portfolio_cuopt", "score_transaction_risk", "fft_option_price", "nccl_allreduce", "beads_lease", "beads_p2p"},
        .max_tokens_per_call = 4096,
        .can_spawn_subagents = true,
        .priority_override = false,
    });
  }

  void RegisterRole(const AgentRoleAuthorization& role) {
    roles_[role.id] = role;
  }

  bool RevokeTool(const std::string& agent_id, const std::string& tool_name) {
    auto it = roles_.find(agent_id);
    if (it == roles_.end()) return false;
    auto& tools = it->second.allowed_tools;
    const auto old_size = tools.size();
    std::erase(tools, tool_name);
    return tools.size() != old_size;
  }

  // Validates agent authorization & parameter bounds and returns the 64-bit SIV compliance bitmask.
  // Supports dynamic harnesses with prefix matching (e.g., "antigravity-01", "claude-worker", "codex-02").
  std::uint64_t ValidateAgentCall(const std::string& agent_id,
                                  const std::string& tool_name,
                                  const nlohmann::json& args) {
    const AgentRoleAuthorization* role = ResolveRole(agent_id);
    if (!role) {
      RecordAudit(agent_id, tool_name, false, 0, "Unregistered agent identity: " + agent_id);
      throw std::runtime_error("[SIV Access Denied] Agent '" + agent_id + "' is not authorized in SIV.");
    }

    bool allowed = false;
    for (const auto& t : role->allowed_tools) {
      if (t == tool_name || t == "*") {
        allowed = true;
        break;
      }
    }
    if (!allowed) {
      RecordAudit(agent_id, tool_name, false, 0, "Unauthorized tool: " + tool_name);
      throw std::runtime_error("[SIV Tool Denied] Agent '" + agent_id + "' cannot call '" + tool_name + "'.");
    }

    if (tool_name == "compute_var_simulation") {
      const std::uint64_t paths = args.value("paths", std::uint64_t{100000});
      if (paths == 0 || paths > max_monte_carlo_paths_) {
        RecordAudit(agent_id, tool_name, false, 0, "Requested paths exceed SIV risk ceiling: " + std::to_string(paths));
        throw std::runtime_error("[SIV Boundary Violation] Requested paths (" + std::to_string(paths) +
                                 ") exceeds SIV risk ceiling (" + std::to_string(max_monte_carlo_paths_) + ").");
      }
    } else if (tool_name == "cholesky_factor") {
      const std::uint32_t dim = args.value("matrix_dim", 0U);
      if (dim == 0 || dim > max_matrix_dimensions_) {
        RecordAudit(agent_id, tool_name, false, 0, "Matrix dimension exceeds SIV ceiling: " + std::to_string(dim));
        throw std::runtime_error("[SIV Boundary Violation] Matrix dimension (" + std::to_string(dim) +
                                 ") exceeds SIV ceiling (" + std::to_string(max_matrix_dimensions_) + ").");
      }
    }

    std::uint64_t bitmask = SIV_AUDITED_BEAD | SIV_RISK_CHECK_PASSED | SIV_DETERMINISTIC_MATH_DELEGATED;
    if (role->priority_override) {
      bitmask |= SIV_PRIORITY_BOOST;
    }
    RecordAudit(agent_id, tool_name, true, bitmask, "SIV policy check passed");
    return bitmask;
  }

  // Cognitive Planner & Parameter Broker: parses natural-language quantitative directives into
  // structured CUDA tool calls without letting the LLM compute matrix/stochastic math directly.
  nlohmann::json RouteCognitiveDirective(const std::string& directive, bool rubin_mode = false) const {
    const std::string selected_model = rubin_mode ? rubin_moe_model_ : primary_model_;

    double spot = 180.0;
    double vol = 0.25;
    double drift = 0.05;
    std::uint32_t paths = 100000;
    std::uint32_t steps = 252;

    std::smatch match;
    if (std::regex_search(directive, match, std::regex(R"(spot\s*\$?([0-9]+(?:\.[0-9]+)?))", std::regex::icase))) {
      spot = std::stod(match[1].str());
    }
    if (std::regex_search(directive, match, std::regex(R"(vol(?:atility)?\s*([0-9]+(?:\.[0-9]+)?))", std::regex::icase))) {
      vol = std::stod(match[1].str());
      if (vol > 2.0) vol /= 100.0;
    }
    if (std::regex_search(directive, match, std::regex(R"(drift\s*([0-9]+(?:\.[0-9]+)?))", std::regex::icase))) {
      drift = std::stod(match[1].str());
    }
    if (std::regex_search(directive, match, std::regex(R"(([0-9]+)k\s*path)", std::regex::icase))) {
      paths = static_cast<std::uint32_t>(std::stoul(match[1].str()) * 1000U);
    } else if (std::regex_search(directive, match, std::regex(R"(([0-9]{4,9})\s*path)", std::regex::icase))) {
      paths = static_cast<std::uint32_t>(std::stoul(match[1].str()));
    }

    std::string target_tool = "compute_var_simulation";
    if (directive.find("Cholesky") != std::string::npos || directive.find("covariance") != std::string::npos) {
      target_tool = "cholesky_factor";
    } else if (directive.find("portfolio") != std::string::npos || directive.find("cuOpt") != std::string::npos ||
               directive.find("rebalance") != std::string::npos) {
      target_tool = "optimize_portfolio_cuopt";
    } else if (directive.find("fraud") != std::string::npos || directive.find("anomaly") != std::string::npos) {
      target_tool = "score_transaction_risk";
    } else if (directive.find("FFT") != std::string::npos || directive.find("Heston") != std::string::npos ||
               directive.find("option") != std::string::npos) {
      target_tool = "fft_option_price";
    }

    return {
        {"status", "planned"},
        {"cognitive_core", selected_model},
        {"auxiliary_model", auxiliary_model_},
        {"endpoint", endpoint_},
        {"nemo_guardrails_enforced", enforce_nemo_guardrails_},
        {"deterministic_cuda_delegation", enforce_deterministic_cuda_},
        {"speculative_decoding", rubin_mode},
        {"recommended_tool", target_tool},
        {"extracted_parameters",
         {{"spot_price", spot},
          {"volatility", vol},
          {"drift", drift},
          {"paths", paths},
          {"steps", steps}}},
    };
  }

  nlohmann::json ToJson() const {
    nlohmann::json roles_arr = nlohmann::json::array();
    for (const auto& [id, r] : roles_) {
      roles_arr.push_back({
          {"id", r.id},
          {"class", r.role_class},
          {"allowed_tools", r.allowed_tools},
          {"max_tokens_per_call", r.max_tokens_per_call},
          {"can_spawn_subagents", r.can_spawn_subagents},
          {"priority_override", r.priority_override},
      });
    }
    return {
        {"apiVersion", "siv.symphony.quanux.org/v1"},
        {"kind", "IntelligenceVector"},
        {"metadata", {{"name", name_}, {"version", version_}}},
        {"spec",
         {{"cognitive_core",
           {{"provider", provider_},
            {"endpoint", endpoint_},
            {"primary_model", primary_model_},
            {"auxiliary_model", auxiliary_model_},
            {"rubin_moe_model", rubin_moe_model_},
            {"quant_reasoning_mode", "strict_function_calling"}}},
          {"governance_and_rails",
           {{"enforce_nemo_guardrails", enforce_nemo_guardrails_},
            {"hallucination_guard", "deterministic_cuda_delegation"},
            {"risk_limits",
             {{"max_monte_carlo_paths", max_monte_carlo_paths_},
              {"max_matrix_dimensions", max_matrix_dimensions_}}}}},
          {"agent_roles", roles_arr}}},
    };
  }

  nlohmann::json GetAuditLogJson(bool violations_only) const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& rec : audit_log_) {
      if (violations_only && rec.permitted) continue;
      arr.push_back({
          {"timestamp_ns", rec.timestamp_ns},
          {"agent_id", rec.agent_id},
          {"tool_name", rec.tool_name},
          {"permitted", rec.permitted},
          {"siv_bitmask", rec.siv_bitmask},
          {"detail", rec.detail},
      });
    }
    return arr;
  }

private:
  const AgentRoleAuthorization* ResolveRole(const std::string& agent_id) const {
    auto it = roles_.find(agent_id);
    if (it != roles_.end()) return &it->second;
    if (agent_id.starts_with("antigravity")) return &roles_.at("antigravity-operator");
    if (agent_id.starts_with("claude")) return &roles_.at("claude-architect");
    if (agent_id.starts_with("codex") || agent_id.starts_with("symphony-codex")) return &roles_.at("symphony-codex");
    if (agent_id.starts_with("cursor")) return &roles_.at("cursor-worker");
    if (agent_id.starts_with("dynamic-") || agent_id.starts_with("subagent-") || agent_id.starts_with("risk-bot")) {
      return &roles_.at("default-agent");
    }
    return nullptr;
  }

  void RecordAudit(const std::string& agent_id,
                   const std::string& tool_name,
                   bool permitted,
                   std::uint64_t bitmask,
                   const std::string& detail) {
    const auto now_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    audit_log_.push_back(SIVAuditRecord{now_ns, agent_id, tool_name, permitted, bitmask, detail});
  }

  std::string name_;
  std::string version_;
  std::string provider_;
  std::string endpoint_;
  std::string primary_model_;
  std::string auxiliary_model_;
  std::string rubin_moe_model_;
  bool enforce_nemo_guardrails_{true};
  bool enforce_deterministic_cuda_{true};
  std::uint64_t max_monte_carlo_paths_{100000000ULL};
  std::uint32_t max_matrix_dimensions_{10000U};
  std::unordered_map<std::string, AgentRoleAuthorization> roles_;
  std::vector<SIVAuditRecord> audit_log_;
};

} // namespace nemosymphtron::siv
