#pragma once

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#include "nlohmann/json.hpp"

namespace nemosymphtron::ipc {

enum class ExecutionPriority : std::uint32_t {
  PRIORITY_BACKGROUND = 0,
  PRIORITY_STANDARD   = 1,
  PRIORITY_INTERRUPT  = 2
};

enum class PayloadKind : std::uint32_t {
  NONE              = 0,
  MONTE_CARLO_VAR   = 1,
  CHOLESKY_FACTOR   = 2,
  PORTFOLIO_OPTIM   = 3,
  TRANSACTION_RISK  = 4,
  FFT_OPTION_PRICE  = 5,
  NCCL_COLLECTIVE   = 6,
  FILE_LEASE        = 7,
  P2P_MESSAGE       = 8,
  ENGINE_TELEMETRY  = 9
};

struct TaskRoutingHeader {
  std::string request_id;
  std::string agent_id{"default-agent"};
  std::string session_token;
  ExecutionPriority priority{ExecutionPriority::PRIORITY_STANDARD};
  std::uint64_t siv_token_flags{0};
  std::string tops_id;
  std::string bead_id;
};

struct MonteCarloVaRRequest {
  double spot_price{100.0};
  double volatility{0.20};
  double drift{0.05};
  std::uint32_t paths{100000};
  std::uint32_t steps{252};
  std::uint64_t seed{1337ULL};
};

struct MonteCarloVaRResult {
  double var_95{0.0};
  double var_99{0.0};
  double cvar_99{0.0};
  double expected_terminal_price{0.0};
  double execution_time_ms{0.0};
};

struct CholeskyFactorRequest {
  std::uint32_t matrix_dim{0};
  std::vector<float> flattened_matrix;
};

struct CholeskyFactorResult {
  bool is_positive_definite{false};
  std::vector<float> lower_triangular;
  double trace_norm{0.0};
  double execution_time_ms{0.0};
};

struct PortfolioOptimizeRequest {
  std::vector<std::string> tickers;
  std::string objective{"max_sharpe"}; // "min_risk", "max_sharpe", "mean_cvar"
  double target_return{0.10};
  std::vector<double> expected_returns;
  std::vector<double> cov_matrix;
};

struct PortfolioOptimizeResult {
  std::vector<double> weights;
  double portfolio_return{0.0};
  double portfolio_vol{0.0};
  double sharpe_ratio{0.0};
  std::string solver_backend{"cuOpt-GPU"};
  double execution_time_ms{0.0};
};

struct TransactionRiskRequest {
  std::string transaction_id;
  double amount{0.0};
  std::string merchant_code{"FIN_INST"};
  double velocity_score{0.1};
};

struct TransactionRiskResult {
  std::string transaction_id;
  double anomaly_score{0.0};
  std::string decision{"APPROVE"};
  std::string foundation_model{"nemotron-tabular-transformer"};
  double execution_time_ms{0.0};
};

struct FftOptionPriceRequest {
  double spot_price{100.0};
  double strike_price{100.0};
  double maturity_years{1.0};
  double risk_free_rate{0.05};
  double volatility{0.20};
  std::uint32_t grid_points{4096};
};

struct FftOptionPriceResult {
  double call_price{0.0};
  double put_price{0.0};
  double delta{0.0};
  double execution_time_ms{0.0};
};

struct NcclCollectiveRequest {
  std::uint32_t world_size{2};
  std::string fabric_type{"roce_v2_200g"};
  std::vector<float> local_tensor;
};

struct NcclCollectiveResult {
  std::vector<float> reduced_tensor;
  double effective_bandwidth_gbps{0.0};
  bool in_network_sharp_used{false};
  double execution_time_ms{0.0};
};

struct FileLeaseRequest {
  std::string resource_uri;
  std::string claimant_agent_id;
  std::uint32_t action{0}; // 0 = ACQUIRE, 1 = RELEASE, 2 = HEARTBEAT
  std::uint64_t lease_duration_ms{5000};
  std::string expected_hash;
  std::string updated_hash;
};

struct FileLeaseResponse {
  std::string resource_uri;
  bool granted{false};
  std::string holder_agent_id;
  std::uint64_t expires_at_ns{0};
  std::string current_content_hash;
  bool dpu_offloaded{false};
  std::string error_message;
};

struct AgentDirectMessage {
  std::string message_id;
  std::string source_agent_id;
  std::string target_agent_id;
  std::string session_id;
  std::uint32_t pattern{0}; // 0 = PIPELINE, 1 = WORKER_POOL, 2 = ADVERSARIAL_CROSS_CHECK
  std::string intent;
  std::string payload;
  std::uint64_t timestamp_ns{0};
};

struct AgentIPCRequest {
  TaskRoutingHeader header;
  PayloadKind kind{PayloadKind::NONE};
  MonteCarloVaRRequest mc_request;
  CholeskyFactorRequest cholesky_request;
  PortfolioOptimizeRequest cuopt_request;
  TransactionRiskRequest risk_request;
  FftOptionPriceRequest fft_request;
  NcclCollectiveRequest nccl_request;
  FileLeaseRequest lease_request;
  AgentDirectMessage p2p_message;
};

struct AgentIPCResponse {
  std::string request_id;
  std::string agent_id;
  std::int32_t status_code{0};
  std::string error_message;
  std::uint32_t executed_stream_id{0};
  std::string hardware_backend;
  PayloadKind kind{PayloadKind::NONE};
  MonteCarloVaRResult mc_result;
  CholeskyFactorResult cholesky_result;
  PortfolioOptimizeResult cuopt_result;
  TransactionRiskResult risk_result;
  FftOptionPriceResult fft_result;
  NcclCollectiveResult nccl_result;
  FileLeaseResponse lease_result;
  nlohmann::json telemetry_snapshot;
};

// Protobuf v3 wire-format helpers (varint + length-delimited binary encoding)
namespace proto_wire {

inline void append_varint(std::string& out, std::uint64_t value) {
  while (value >= 0x80) {
    out.push_back(static_cast<char>((value & 0x7F) | 0x80));
    value >>= 7;
  }
  out.push_back(static_cast<char>(value));
}

inline void append_bytes_field(std::string& out, std::uint32_t field_number, const std::string& bytes) {
  const std::uint64_t tag = (static_cast<std::uint64_t>(field_number) << 3) | 2ULL;
  append_varint(out, tag);
  append_varint(out, bytes.size());
  out.append(bytes);
}

inline bool read_varint(const std::string& in, std::size_t& offset, std::uint64_t& value) {
  value = 0;
  int shift = 0;
  while (offset < in.size() && shift < 64) {
    const auto byte = static_cast<unsigned char>(in[offset++]);
    value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
    if ((byte & 0x80) == 0) return true;
    shift += 7;
  }
  return false;
}

inline std::string unwrap_bytes_field(const std::string& in, std::uint32_t expected_field) {
  std::size_t offset = 0;
  std::uint64_t tag = 0;
  if (!read_varint(in, offset, tag)) throw std::runtime_error("Malformed Protobuf wire tag");
  if ((tag >> 3) != expected_field || (tag & 7ULL) != 2ULL) {
    throw std::runtime_error("Unexpected Protobuf wire field tag");
  }
  std::uint64_t length = 0;
  if (!read_varint(in, offset, length) || offset + length > in.size()) {
    throw std::runtime_error("Malformed Protobuf length-delimited payload");
  }
  return in.substr(offset, static_cast<std::size_t>(length));
}

} // namespace proto_wire

inline std::string serialize_request(const AgentIPCRequest& req) {
  nlohmann::json j;
  j["header"] = {
      {"request_id", req.header.request_id},
      {"agent_id", req.header.agent_id},
      {"session_token", req.header.session_token},
      {"priority", static_cast<std::uint32_t>(req.header.priority)},
      {"siv_token_flags", req.header.siv_token_flags},
      {"tops_id", req.header.tops_id},
      {"bead_id", req.header.bead_id},
  };
  j["kind"] = static_cast<std::uint32_t>(req.kind);
  switch (req.kind) {
    case PayloadKind::MONTE_CARLO_VAR:
      j["mc_request"] = {
          {"spot_price", req.mc_request.spot_price},
          {"volatility", req.mc_request.volatility},
          {"drift", req.mc_request.drift},
          {"paths", req.mc_request.paths},
          {"steps", req.mc_request.steps},
          {"seed", req.mc_request.seed},
      };
      break;
    case PayloadKind::CHOLESKY_FACTOR:
      j["cholesky_request"] = {
          {"matrix_dim", req.cholesky_request.matrix_dim},
          {"flattened_matrix", req.cholesky_request.flattened_matrix},
      };
      break;
    case PayloadKind::PORTFOLIO_OPTIM:
      j["cuopt_request"] = {
          {"tickers", req.cuopt_request.tickers},
          {"objective", req.cuopt_request.objective},
          {"target_return", req.cuopt_request.target_return},
          {"expected_returns", req.cuopt_request.expected_returns},
          {"cov_matrix", req.cuopt_request.cov_matrix},
      };
      break;
    case PayloadKind::TRANSACTION_RISK:
      j["risk_request"] = {
          {"transaction_id", req.risk_request.transaction_id},
          {"amount", req.risk_request.amount},
          {"merchant_code", req.risk_request.merchant_code},
          {"velocity_score", req.risk_request.velocity_score},
      };
      break;
    case PayloadKind::FFT_OPTION_PRICE:
      j["fft_request"] = {
          {"spot_price", req.fft_request.spot_price},
          {"strike_price", req.fft_request.strike_price},
          {"maturity_years", req.fft_request.maturity_years},
          {"risk_free_rate", req.fft_request.risk_free_rate},
          {"volatility", req.fft_request.volatility},
          {"grid_points", req.fft_request.grid_points},
      };
      break;
    case PayloadKind::NCCL_COLLECTIVE:
      j["nccl_request"] = {
          {"world_size", req.nccl_request.world_size},
          {"fabric_type", req.nccl_request.fabric_type},
          {"local_tensor", req.nccl_request.local_tensor},
      };
      break;
    case PayloadKind::FILE_LEASE:
      j["lease_request"] = {
          {"resource_uri", req.lease_request.resource_uri},
          {"claimant_agent_id", req.lease_request.claimant_agent_id},
          {"action", req.lease_request.action},
          {"lease_duration_ms", req.lease_request.lease_duration_ms},
          {"expected_hash", req.lease_request.expected_hash},
          {"updated_hash", req.lease_request.updated_hash},
      };
      break;
    case PayloadKind::P2P_MESSAGE:
      j["p2p_message"] = {
          {"message_id", req.p2p_message.message_id},
          {"source_agent_id", req.p2p_message.source_agent_id},
          {"target_agent_id", req.p2p_message.target_agent_id},
          {"session_id", req.p2p_message.session_id},
          {"pattern", req.p2p_message.pattern},
          {"intent", req.p2p_message.intent},
          {"payload", req.p2p_message.payload},
          {"timestamp_ns", req.p2p_message.timestamp_ns},
      };
      break;
    default:
      break;
  }
  const auto cbor = nlohmann::json::to_cbor(j);
  std::string cbor_str(reinterpret_cast<const char*>(cbor.data()), cbor.size());
  std::string wire;
  proto_wire::append_bytes_field(wire, 1, cbor_str);
  return wire;
}

inline AgentIPCRequest deserialize_request(const std::string& wire) {
  const std::string cbor_str = proto_wire::unwrap_bytes_field(wire, 1);
  const std::vector<std::uint8_t> cbor(cbor_str.begin(), cbor_str.end());
  const nlohmann::json j = nlohmann::json::from_cbor(cbor);

  AgentIPCRequest req;
  const auto& h = j.at("header");
  req.header.request_id = h.value("request_id", "");
  req.header.agent_id = h.value("agent_id", "default-agent");
  req.header.session_token = h.value("session_token", "");
  req.header.priority = static_cast<ExecutionPriority>(h.value("priority", 1U));
  req.header.siv_token_flags = h.value("siv_token_flags", std::uint64_t{0});
  req.header.tops_id = h.value("tops_id", "");
  req.header.bead_id = h.value("bead_id", "");
  req.kind = static_cast<PayloadKind>(j.value("kind", 0U));

  switch (req.kind) {
    case PayloadKind::MONTE_CARLO_VAR: {
      const auto& p = j.at("mc_request");
      req.mc_request.spot_price = p.value("spot_price", 100.0);
      req.mc_request.volatility = p.value("volatility", 0.20);
      req.mc_request.drift = p.value("drift", 0.05);
      req.mc_request.paths = p.value("paths", 100000U);
      req.mc_request.steps = p.value("steps", 252U);
      req.mc_request.seed = p.value("seed", std::uint64_t{1337ULL});
      break;
    }
    case PayloadKind::CHOLESKY_FACTOR: {
      const auto& p = j.at("cholesky_request");
      req.cholesky_request.matrix_dim = p.value("matrix_dim", 0U);
      req.cholesky_request.flattened_matrix = p.value("flattened_matrix", std::vector<float>{});
      break;
    }
    case PayloadKind::PORTFOLIO_OPTIM: {
      const auto& p = j.at("cuopt_request");
      req.cuopt_request.tickers = p.value("tickers", std::vector<std::string>{});
      req.cuopt_request.objective = p.value("objective", std::string("max_sharpe"));
      req.cuopt_request.target_return = p.value("target_return", 0.10);
      req.cuopt_request.expected_returns = p.value("expected_returns", std::vector<double>{});
      req.cuopt_request.cov_matrix = p.value("cov_matrix", std::vector<double>{});
      break;
    }
    case PayloadKind::TRANSACTION_RISK: {
      const auto& p = j.at("risk_request");
      req.risk_request.transaction_id = p.value("transaction_id", "");
      req.risk_request.amount = p.value("amount", 0.0);
      req.risk_request.merchant_code = p.value("merchant_code", "FIN_INST");
      req.risk_request.velocity_score = p.value("velocity_score", 0.1);
      break;
    }
    case PayloadKind::FFT_OPTION_PRICE: {
      const auto& p = j.at("fft_request");
      req.fft_request.spot_price = p.value("spot_price", 100.0);
      req.fft_request.strike_price = p.value("strike_price", 100.0);
      req.fft_request.maturity_years = p.value("maturity_years", 1.0);
      req.fft_request.risk_free_rate = p.value("risk_free_rate", 0.05);
      req.fft_request.volatility = p.value("volatility", 0.20);
      req.fft_request.grid_points = p.value("grid_points", 4096U);
      break;
    }
    case PayloadKind::NCCL_COLLECTIVE: {
      const auto& p = j.at("nccl_request");
      req.nccl_request.world_size = p.value("world_size", 2U);
      req.nccl_request.fabric_type = p.value("fabric_type", "roce_v2_200g");
      req.nccl_request.local_tensor = p.value("local_tensor", std::vector<float>{});
      break;
    }
    case PayloadKind::FILE_LEASE: {
      const auto& p = j.at("lease_request");
      req.lease_request.resource_uri = p.value("resource_uri", "");
      req.lease_request.claimant_agent_id = p.value("claimant_agent_id", "");
      req.lease_request.action = p.value("action", 0U);
      req.lease_request.lease_duration_ms = p.value("lease_duration_ms", std::uint64_t{5000});
      req.lease_request.expected_hash = p.value("expected_hash", "");
      req.lease_request.updated_hash = p.value("updated_hash", "");
      break;
    }
    case PayloadKind::P2P_MESSAGE: {
      const auto& p = j.at("p2p_message");
      req.p2p_message.message_id = p.value("message_id", "");
      req.p2p_message.source_agent_id = p.value("source_agent_id", "");
      req.p2p_message.target_agent_id = p.value("target_agent_id", "");
      req.p2p_message.session_id = p.value("session_id", "");
      req.p2p_message.pattern = p.value("pattern", 0U);
      req.p2p_message.intent = p.value("intent", "");
      req.p2p_message.payload = p.value("payload", "");
      req.p2p_message.timestamp_ns = p.value("timestamp_ns", std::uint64_t{0});
      break;
    }
    default:
      break;
  }
  return req;
}

inline std::string serialize_response(const AgentIPCResponse& resp) {
  nlohmann::json j = {
      {"request_id", resp.request_id},
      {"agent_id", resp.agent_id},
      {"status_code", resp.status_code},
      {"error_message", resp.error_message},
      {"executed_stream_id", resp.executed_stream_id},
      {"hardware_backend", resp.hardware_backend},
      {"kind", static_cast<std::uint32_t>(resp.kind)},
  };
  switch (resp.kind) {
    case PayloadKind::MONTE_CARLO_VAR:
      j["mc_result"] = {
          {"var_95", resp.mc_result.var_95},
          {"var_99", resp.mc_result.var_99},
          {"cvar_99", resp.mc_result.cvar_99},
          {"expected_terminal_price", resp.mc_result.expected_terminal_price},
          {"execution_time_ms", resp.mc_result.execution_time_ms},
      };
      break;
    case PayloadKind::CHOLESKY_FACTOR:
      j["cholesky_result"] = {
          {"is_positive_definite", resp.cholesky_result.is_positive_definite},
          {"lower_triangular", resp.cholesky_result.lower_triangular},
          {"trace_norm", resp.cholesky_result.trace_norm},
          {"execution_time_ms", resp.cholesky_result.execution_time_ms},
      };
      break;
    case PayloadKind::PORTFOLIO_OPTIM:
      j["cuopt_result"] = {
          {"weights", resp.cuopt_result.weights},
          {"portfolio_return", resp.cuopt_result.portfolio_return},
          {"portfolio_vol", resp.cuopt_result.portfolio_vol},
          {"sharpe_ratio", resp.cuopt_result.sharpe_ratio},
          {"solver_backend", resp.cuopt_result.solver_backend},
          {"execution_time_ms", resp.cuopt_result.execution_time_ms},
      };
      break;
    case PayloadKind::TRANSACTION_RISK:
      j["risk_result"] = {
          {"transaction_id", resp.risk_result.transaction_id},
          {"anomaly_score", resp.risk_result.anomaly_score},
          {"decision", resp.risk_result.decision},
          {"foundation_model", resp.risk_result.foundation_model},
          {"execution_time_ms", resp.risk_result.execution_time_ms},
      };
      break;
    case PayloadKind::FFT_OPTION_PRICE:
      j["fft_result"] = {
          {"call_price", resp.fft_result.call_price},
          {"put_price", resp.fft_result.put_price},
          {"delta", resp.fft_result.delta},
          {"execution_time_ms", resp.fft_result.execution_time_ms},
      };
      break;
    case PayloadKind::NCCL_COLLECTIVE:
      j["nccl_result"] = {
          {"reduced_tensor", resp.nccl_result.reduced_tensor},
          {"effective_bandwidth_gbps", resp.nccl_result.effective_bandwidth_gbps},
          {"in_network_sharp_used", resp.nccl_result.in_network_sharp_used},
          {"execution_time_ms", resp.nccl_result.execution_time_ms},
      };
      break;
    case PayloadKind::FILE_LEASE:
      j["lease_result"] = {
          {"resource_uri", resp.lease_result.resource_uri},
          {"granted", resp.lease_result.granted},
          {"holder_agent_id", resp.lease_result.holder_agent_id},
          {"expires_at_ns", resp.lease_result.expires_at_ns},
          {"current_content_hash", resp.lease_result.current_content_hash},
          {"dpu_offloaded", resp.lease_result.dpu_offloaded},
          {"error_message", resp.lease_result.error_message},
      };
      break;
    case PayloadKind::ENGINE_TELEMETRY:
      j["telemetry_snapshot"] = resp.telemetry_snapshot;
      break;
    default:
      break;
  }
  const auto cbor = nlohmann::json::to_cbor(j);
  std::string cbor_str(reinterpret_cast<const char*>(cbor.data()), cbor.size());
  std::string wire;
  proto_wire::append_bytes_field(wire, 1, cbor_str);
  return wire;
}

inline AgentIPCResponse deserialize_response(const std::string& wire) {
  const std::string cbor_str = proto_wire::unwrap_bytes_field(wire, 1);
  const std::vector<std::uint8_t> cbor(cbor_str.begin(), cbor_str.end());
  const nlohmann::json j = nlohmann::json::from_cbor(cbor);

  AgentIPCResponse resp;
  resp.request_id = j.value("request_id", "");
  resp.agent_id = j.value("agent_id", "");
  resp.status_code = j.value("status_code", 0);
  resp.error_message = j.value("error_message", "");
  resp.executed_stream_id = j.value("executed_stream_id", 0U);
  resp.hardware_backend = j.value("hardware_backend", "");
  resp.kind = static_cast<PayloadKind>(j.value("kind", 0U));

  switch (resp.kind) {
    case PayloadKind::MONTE_CARLO_VAR: {
      const auto& r = j.at("mc_result");
      resp.mc_result.var_95 = r.value("var_95", 0.0);
      resp.mc_result.var_99 = r.value("var_99", 0.0);
      resp.mc_result.cvar_99 = r.value("cvar_99", 0.0);
      resp.mc_result.expected_terminal_price = r.value("expected_terminal_price", 0.0);
      resp.mc_result.execution_time_ms = r.value("execution_time_ms", 0.0);
      break;
    }
    case PayloadKind::CHOLESKY_FACTOR: {
      const auto& r = j.at("cholesky_result");
      resp.cholesky_result.is_positive_definite = r.value("is_positive_definite", false);
      resp.cholesky_result.lower_triangular = r.value("lower_triangular", std::vector<float>{});
      resp.cholesky_result.trace_norm = r.value("trace_norm", 0.0);
      resp.cholesky_result.execution_time_ms = r.value("execution_time_ms", 0.0);
      break;
    }
    case PayloadKind::PORTFOLIO_OPTIM: {
      const auto& r = j.at("cuopt_result");
      resp.cuopt_result.weights = r.value("weights", std::vector<double>{});
      resp.cuopt_result.portfolio_return = r.value("portfolio_return", 0.0);
      resp.cuopt_result.portfolio_vol = r.value("portfolio_vol", 0.0);
      resp.cuopt_result.sharpe_ratio = r.value("sharpe_ratio", 0.0);
      resp.cuopt_result.solver_backend = r.value("solver_backend", "cuOpt-GPU");
      resp.cuopt_result.execution_time_ms = r.value("execution_time_ms", 0.0);
      break;
    }
    case PayloadKind::TRANSACTION_RISK: {
      const auto& r = j.at("risk_result");
      resp.risk_result.transaction_id = r.value("transaction_id", "");
      resp.risk_result.anomaly_score = r.value("anomaly_score", 0.0);
      resp.risk_result.decision = r.value("decision", "APPROVE");
      resp.risk_result.foundation_model = r.value("foundation_model", "nemotron-tabular-transformer");
      resp.risk_result.execution_time_ms = r.value("execution_time_ms", 0.0);
      break;
    }
    case PayloadKind::FFT_OPTION_PRICE: {
      const auto& r = j.at("fft_result");
      resp.fft_result.call_price = r.value("call_price", 0.0);
      resp.fft_result.put_price = r.value("put_price", 0.0);
      resp.fft_result.delta = r.value("delta", 0.0);
      resp.fft_result.execution_time_ms = r.value("execution_time_ms", 0.0);
      break;
    }
    case PayloadKind::NCCL_COLLECTIVE: {
      const auto& r = j.at("nccl_result");
      resp.nccl_result.reduced_tensor = r.value("reduced_tensor", std::vector<float>{});
      resp.nccl_result.effective_bandwidth_gbps = r.value("effective_bandwidth_gbps", 0.0);
      resp.nccl_result.in_network_sharp_used = r.value("in_network_sharp_used", false);
      resp.nccl_result.execution_time_ms = r.value("execution_time_ms", 0.0);
      break;
    }
    case PayloadKind::FILE_LEASE: {
      const auto& r = j.at("lease_result");
      resp.lease_result.resource_uri = r.value("resource_uri", "");
      resp.lease_result.granted = r.value("granted", false);
      resp.lease_result.holder_agent_id = r.value("holder_agent_id", "");
      resp.lease_result.expires_at_ns = r.value("expires_at_ns", std::uint64_t{0});
      resp.lease_result.current_content_hash = r.value("current_content_hash", "");
      resp.lease_result.dpu_offloaded = r.value("dpu_offloaded", false);
      resp.lease_result.error_message = r.value("error_message", "");
      break;
    }
    case PayloadKind::ENGINE_TELEMETRY: {
      resp.telemetry_snapshot = j.value("telemetry_snapshot", nlohmann::json::object());
      break;
    }
    default:
      break;
  }
  return resp;
}

// 4-byte little-endian length-prefixed framing over AF_UNIX stream sockets
inline bool write_all(int fd, const void* data, std::size_t length) {
  const auto* ptr = static_cast<const char*>(data);
  std::size_t written = 0;
  while (written < length) {
    const ssize_t rc = ::write(fd, ptr + written, length - written);
    if (rc < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (rc == 0) return false;
    written += static_cast<std::size_t>(rc);
  }
  return true;
}

inline bool read_all(int fd, void* data, std::size_t length) {
  auto* ptr = static_cast<char*>(data);
  std::size_t received = 0;
  while (received < length) {
    const ssize_t rc = ::read(fd, ptr + received, length - received);
    if (rc < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (rc == 0) return false;
    received += static_cast<std::size_t>(rc);
  }
  return true;
}

inline bool send_ipc_frame(int fd, const std::string& payload) {
  if (payload.size() > 64ULL * 1024 * 1024) return false;
  const auto size = static_cast<std::uint32_t>(payload.size());
  if (!write_all(fd, &size, sizeof(size))) return false;
  return write_all(fd, payload.data(), payload.size());
}

inline bool recv_ipc_frame(int fd, std::string& payload) {
  std::uint32_t size = 0;
  if (!read_all(fd, &size, sizeof(size))) return false;
  if (size > 64ULL * 1024 * 1024) return false;
  payload.resize(size);
  if (size == 0) return true;
  return read_all(fd, payload.data(), size);
}

} // namespace nemosymphtron::ipc
