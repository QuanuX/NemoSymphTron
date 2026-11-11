#include "nemosymphtron/beads_engine.hpp"
#include "nemosymphtron/cuda_quant_worker.hpp"
#include "nemosymphtron/scv_fabric_manager.hpp"
#include "nemosymphtron/sha256.hpp"
#include "nemosymphtron/shv_hardware_guard.hpp"
#include "nemosymphtron/siv_policy_enforcer.hpp"
#include "nemosymphtron/transport.hpp"
#include "nemosymphtron/wire_protocol.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

void require(bool ok, const std::string& msg) {
  if (!ok) {
    throw std::runtime_error(msg);
  }
}

uint64_t now_epoch_ns() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

std::string read_text_file(const fs::path& path, size_t max_bytes = 16 * 1024 * 1024) {
  std::error_code ec;
  require(!fs::is_symlink(path, ec), "Refusing to follow symlink input: " + path.string());
  require(fs::is_regular_file(path, ec), "Expected regular file: " + path.string());
  require(fs::file_size(path, ec) <= max_bytes, "Input file exceeds bounded size limit: " + path.string());
  std::ifstream in(path, std::ios::binary);
  require(bool(in), "Cannot open file: " + path.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_text_file(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  const fs::path tmp = path.string() + ".tmp." + std::to_string(::getpid());
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    require(bool(out), "Failed to open state file for writing: " + tmp.string());
    out << content;
  }
  fs::rename(tmp, path);
}

json make_command_record(
    const std::string& command_id,
    const std::string& grammar,
    const std::string& mutability,
    const std::string& authority_mode,
    const std::string& feature_id,
    const std::string& interaction,
    const std::optional<std::string>& recovery_id = std::nullopt) {
  return json{
      {"command_id", command_id},
      {"status", "experimental"},
      {"introduced_in", "0.2.0-alpha.1"},
      {"deprecated_in", nullptr},
      {"replacement_ids", json::array()},
      {"grammar", grammar},
      {"aliases", json::array()},
      {"visibility", "public"},
      {"feature_bindings", json::array({{{"feature_id", feature_id}, {"interaction", interaction}}})},
      {"infrastructure_purpose", "nvidia_cognitive_and_quant_acceleration"},
      {"backend_operation_ids", json::array()},
      {"mutability", mutability},
      {"authority_mode", authority_mode},
      {"target_scope", "local"},
      {"input_protocols", json::array({"nemosymphtron.control.v1"})},
      {"output_protocols", json::array({"nemosymphtron.result.v1"})},
      {"result_validation_protocols", json::array({"nemosymphtron.result.v1"})},
      {"recovery_command_id", recovery_id.has_value() ? json(*recovery_id) : json(nullptr)},
      {"noninteractive", true},
      {"json_output", true}};
}

json build_command_records() {
  return json::array({
      make_command_record(
          "qxcmd:nemosymphtron:commands.list",
          "qxctl commands list [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:qxctl.command-registry",
          "discover"),
      make_command_record(
          "qxcmd:nemosymphtron:commands.manifest",
          "qxctl commands manifest [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:qxctl.command-registry",
          "discover"),
      make_command_record(
          "qxcmd:nemosymphtron:nst.status",
          "qxctl nst status [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--engine-socket VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:nst.runtime",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:nst.doctor",
          "qxctl nst doctor [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--engine-socket VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:nst.runtime",
          "diagnose"),
      make_command_record(
          "qxcmd:nemosymphtron:nst.schema",
          "qxctl nst schema [--name VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:nst.contracts",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:nst.template",
          "qxctl nst template [--name VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:nst.contracts",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:nst.telemetry",
          "qxctl nst telemetry [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--engine-socket VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:nst.telemetry",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:nst.quant.run",
          "qxctl nst quant run [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--spec VALUE] [--engine-socket VALUE] [--json]",
          "evidence_only",
          "ssiag_grant",
          "ssfv:nemosymphtron:nst.quant",
          "execute",
          "qxcmd:nemosymphtron:nst.status"),
      make_command_record(
          "qxcmd:nemosymphtron:nst.cluster.apply",
          "qxctl nst cluster apply [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--spec VALUE] [--expected-state-digest VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_and_stav",
          "ssfv:nemosymphtron:nst.cluster",
          "apply",
          "qxcmd:nemosymphtron:nst.cluster.recover"),
      make_command_record(
          "qxcmd:nemosymphtron:nst.cluster.recover",
          "qxctl nst cluster recover [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--expected-state-digest VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_and_stav",
          "ssfv:nemosymphtron:nst.cluster",
          "recover",
          "qxcmd:nemosymphtron:nst.status"),
      make_command_record(
          "qxcmd:nemosymphtron:siv.inspect",
          "qxctl siv inspect [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--agent-id VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:siv.governance",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:siv.schema",
          "qxctl siv schema [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:siv.governance",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:siv.template",
          "qxctl siv template [--profile VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:siv.governance",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:siv.audit-log",
          "qxctl siv audit-log [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--limit UINT64] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:siv.governance",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:siv.policy.propose",
          "qxctl siv policy propose [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--policy VALUE] [--json]",
          "proposal_only",
          "ssiag_grant",
          "ssfv:nemosymphtron:siv.governance",
          "propose",
          "qxcmd:nemosymphtron:siv.inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:siv.policy.apply",
          "qxctl siv policy apply [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--policy VALUE] [--expected-state-digest VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_and_stav",
          "ssfv:nemosymphtron:siv.governance",
          "apply",
          "qxcmd:nemosymphtron:siv.inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:siv.agent.authorize",
          "qxctl siv agent authorize [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--agent-id VALUE] [--bitmask VALUE] [--max-vram-mb UINT64] [--expected-state-digest VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_and_stav",
          "ssfv:nemosymphtron:siv.governance",
          "authorize",
          "qxcmd:nemosymphtron:siv.inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:siv.agent.revoke",
          "qxctl siv agent revoke [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--agent-id VALUE] [--expected-state-digest VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_and_stav",
          "ssfv:nemosymphtron:siv.governance",
          "revoke",
          "qxcmd:nemosymphtron:siv.inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:siv.model.route",
          "qxctl siv model route [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--agent-id VALUE] [--prompt VALUE] [--complexity VALUE] [--domain-hint VALUE] [--engine-socket VALUE] [--json]",
          "proposal_only",
          "ssiag_grant",
          "ssfv:nemosymphtron:siv.routing",
          "propose",
          "qxcmd:nemosymphtron:siv.inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:beads.dag.list",
          "qxctl beads dag list [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--engine-socket VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:beads.dag",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:beads.dag.propose",
          "qxctl beads dag propose [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--spec VALUE] [--engine-socket VALUE] [--json]",
          "proposal_only",
          "ssiag_grant",
          "ssfv:nemosymphtron:beads.dag",
          "propose",
          "qxcmd:nemosymphtron:beads.dag.list"),
      make_command_record(
          "qxcmd:nemosymphtron:beads.dag.commit",
          "qxctl beads dag commit [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--bead-id VALUE] [--status VALUE] [--agent-id VALUE] [--expected-state-digest VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_and_stav",
          "ssfv:nemosymphtron:beads.dag",
          "apply",
          "qxcmd:nemosymphtron:beads.dag.list"),
      make_command_record(
          "qxcmd:nemosymphtron:beads.leases.list",
          "qxctl beads leases list [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--engine-socket VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:beads.leases",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:beads.leases.checkout",
          "qxctl beads leases checkout [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--agent-id VALUE] [--file VALUE] [--duration-sec UINT64] [--expected-sha256 VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_grant",
          "ssfv:nemosymphtron:beads.leases",
          "apply",
          "qxcmd:nemosymphtron:beads.leases.list"),
      make_command_record(
          "qxcmd:nemosymphtron:beads.leases.release",
          "qxctl beads leases release [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--agent-id VALUE] [--file VALUE] [--new-sha256 VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_grant",
          "ssfv:nemosymphtron:beads.leases",
          "apply",
          "qxcmd:nemosymphtron:beads.leases.list"),
      make_command_record(
          "qxcmd:nemosymphtron:beads.leases.unlock",
          "qxctl beads leases unlock [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--file VALUE] [--expected-state-digest VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_and_stav",
          "ssfv:nemosymphtron:beads.leases",
          "recover",
          "qxcmd:nemosymphtron:beads.leases.list"),
      make_command_record(
          "qxcmd:nemosymphtron:beads.bus.poll",
          "qxctl beads bus poll [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--agent-id VALUE] [--engine-socket VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:beads.bus",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:beads.bus.dispatch",
          "qxctl beads bus dispatch [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--sender VALUE] [--recipient VALUE] [--pattern VALUE] [--bead-id VALUE] [--payload VALUE] [--engine-socket VALUE] [--json]",
          "evidence_only",
          "ssiag_grant",
          "ssfv:nemosymphtron:beads.bus",
          "execute",
          "qxcmd:nemosymphtron:beads.bus.poll"),
      make_command_record(
          "qxcmd:nemosymphtron:shv.nst-discover",
          "qxctl shv nst-discover [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--arch VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:shv.hardware",
          "discover"),
      make_command_record(
          "qxcmd:nemosymphtron:shv.nst-verify-isolation",
          "qxctl shv nst-verify-isolation [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--spec VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:shv.hardware",
          "verify"),
      make_command_record(
          "qxcmd:nemosymphtron:shv.nst-lock",
          "qxctl shv nst-lock [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--spec VALUE] [--expected-state-digest VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_and_stav",
          "ssfv:nemosymphtron:shv.hardware",
          "apply",
          "qxcmd:nemosymphtron:shv.nst-verify-isolation"),
      make_command_record(
          "qxcmd:nemosymphtron:shv.nst-benchmark-bus",
          "qxctl shv nst-benchmark-bus [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--arch VALUE] [--iterations UINT64] [--json]",
          "evidence_only",
          "ssiag_grant",
          "ssfv:nemosymphtron:shv.hardware",
          "benchmark",
          "qxcmd:nemosymphtron:shv.nst-discover"),
      make_command_record(
          "qxcmd:nemosymphtron:scv.nst-inspect-fabric",
          "qxctl scv nst-inspect-fabric [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--arch VALUE] [--json]",
          "read_only",
          "none",
          "ssfv:nemosymphtron:scv.fabric",
          "inspect"),
      make_command_record(
          "qxcmd:nemosymphtron:scv.nst-test-fabric",
          "qxctl scv nst-test-fabric [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--spec VALUE] [--json]",
          "evidence_only",
          "ssiag_grant",
          "ssfv:nemosymphtron:scv.fabric",
          "verify",
          "qxcmd:nemosymphtron:scv.nst-inspect-fabric"),
      make_command_record(
          "qxcmd:nemosymphtron:scv.nst-cluster-up",
          "qxctl scv nst-cluster-up [--prefix VALUE] [--version VALUE] [--tops-id VALUE] [--spec VALUE] [--expected-state-digest VALUE] [--engine-socket VALUE] [--json]",
          "permission_backed_mutation",
          "ssiag_and_stav",
          "ssfv:nemosymphtron:scv.fabric",
          "apply",
          "qxcmd:nemosymphtron:scv.nst-inspect-fabric")});
}

struct ParsedArgs {
  std::vector<std::string> route;
  std::map<std::string, std::string> flags;
  bool json_output{false};
};

ParsedArgs parse_cli(int argc, char** argv) {
  ParsedArgs out;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--json") {
      out.json_output = true;
      continue;
    }
    if (arg.starts_with("--")) {
      const auto eq = arg.find('=');
      if (eq != std::string::npos) {
        out.flags[arg.substr(2, eq - 2)] = arg.substr(eq + 1);
      } else {
        const std::string key = arg.substr(2);
        if (i + 1 < argc && !std::string(argv[i + 1]).starts_with("--")) {
          out.flags[key] = argv[++i];
        } else {
          out.flags[key] = "true";
        }
      }
    } else {
      out.route.push_back(arg);
    }
  }
  if (!out.route.empty() && out.route.front() == "qxctl") {
    out.route.erase(out.route.begin());
  }
  return out;
}

std::string require_flag(const ParsedArgs& args, const std::string& name) {
  auto it = args.flags.find(name);
  require(it != args.flags.end() && !it->second.empty(), "Explicit --" + name + " is required by Symphony doctrine");
  return it->second;
}

fs::path state_file_path(const std::string& prefix, const std::string& tops_id) {
  return fs::path(prefix) / "state" / tops_id / "nemosymphtron_state.json";
}

std::pair<json, std::string> load_or_init_state(
    const std::string& prefix,
    const std::string& version,
    const std::string& tops_id) {
  const fs::path path = state_file_path(prefix, tops_id);
  std::error_code ec;
  if (fs::is_regular_file(path, ec)) {
    const std::string raw = read_text_file(path);
    return {json::parse(raw), nemosymphtron::buffer_sha256(raw)};
  }
  nemosymphtron::siv::SIVPolicyEnforcer default_siv;
  json initial = {
      {"schema", "nemosymphtron.owner-state.v1"},
      {"prefix", prefix},
      {"version", version},
      {"tops_id", tops_id},
      {"cluster_state", "IDLE"},
      {"hardware_locked", false},
      {"fabric_up", false},
      {"siv_policy", default_siv.ToJson()},
      {"audit_log", json::array()},
      {"beads_dag", json::array()},
      {"file_leases", json::object()},
      {"message_bus", json::array()}};
  return {initial, "absent"};
}

std::string persist_state_with_cas(
    const std::string& prefix,
    const std::string& tops_id,
    const std::string& expected_digest,
    json& state,
    const std::string& current_digest,
    const std::string& event_name) {
  require(!expected_digest.empty(), "Explicit --expected-state-digest is required for state mutations");
  require(
      expected_digest == current_digest,
      "CAS state digest mismatch: expected '" + expected_digest + "' but observed '" + current_digest + "'");
  state["audit_log"].push_back({
      {"timestamp_ns", now_epoch_ns()},
      {"event", event_name},
      {"previous_state_digest", current_digest}});
  const std::string dumped = state.dump(2) + "\n";
  write_text_file(state_file_path(prefix, tops_id), dumped);
  return nemosymphtron::buffer_sha256(dumped);
}

std::optional<nemosymphtron::ipc::AgentIPCResponse> try_forward_to_engine(
    const ParsedArgs& args,
    const nemosymphtron::ipc::AgentIPCRequest& req) {
  std::string sock;
  if (auto it = args.flags.find("engine-socket"); it != args.flags.end()) {
    sock = it->second;
  } else if (auto it = args.flags.find("prefix"); it != args.flags.end()) {
    const fs::path candidate = fs::path(it->second) / "run" / "nst_ipc.sock";
    struct stat st{};
    if (::lstat(candidate.c_str(), &st) == 0 && S_ISSOCK(st.st_mode)) {
      sock = candidate.string();
    }
  }
  if (sock.empty()) {
    return std::nullopt;
  }
  try {
    int fd = nemosymphtron::connect_local_socket(sock);
    (void)nemosymphtron::ipc::send_ipc_frame(fd, nemosymphtron::ipc::serialize_request(req));
    std::string wire_in;
    const bool ok = nemosymphtron::ipc::recv_ipc_frame(fd, wire_in);
    ::close(fd);
    if (!ok) return std::nullopt;
    return nemosymphtron::ipc::deserialize_response(wire_in);
  } catch (...) {
    struct stat st{};
    if (::lstat(sock.c_str(), &st) == 0 && S_ISSOCK(st.st_mode) && st.st_uid == ::geteuid() &&
        (st.st_mode & 07777) == 0600) {
      nemosymphtron::engine::DualPoolQuantEngine fallback_engine;
      return fallback_engine.ExecuteAgentIPC(req);
    }
    return std::nullopt;
  }
}

json schema_by_name(const std::string& name) {
  if (name == "siv" || name == "siv_policy") {
    return {
        {"$schema", "https://json-schema.org/draft/2020-12/schema"},
        {"$id", "https://quanux.org/schemas/nemosymphtron/siv_policy.schema.json"},
        {"title", "NemoSymphTron SIV Policy Schema"},
        {"type", "object"},
        {"required", {"schema", "tops_id", "agents"}}};
  }
  if (name == "shv" || name == "shv_hardware") {
    return {
        {"$schema", "https://json-schema.org/draft/2020-12/schema"},
        {"$id", "https://quanux.org/schemas/nemosymphtron/shv_hardware.schema.json"},
        {"title", "NemoSymphTron SHV Hardware Vector Schema"},
        {"type", "object"},
        {"required", {"architecture_generation", "sm_arch", "total_unified_memory_bytes"}}};
  }
  if (name == "scv" || name == "scv_fabric") {
    return {
        {"$schema", "https://json-schema.org/draft/2020-12/schema"},
        {"$id", "https://quanux.org/schemas/nemosymphtron/scv_fabric.schema.json"},
        {"title", "NemoSymphTron SCV Fabric Vector Schema"},
        {"type", "object"},
        {"required", {"cluster_id", "architecture_generation", "primary_transport"}}};
  }
  if (name == "beads" || name == "beads_dag") {
    return {
        {"$schema", "https://json-schema.org/draft/2020-12/schema"},
        {"$id", "https://quanux.org/schemas/nemosymphtron/beads_dag.schema.json"},
        {"title", "NemoSymphTron Beads Merkle-DAG Schema"},
        {"type", "object"},
        {"required", {"bead_id", "intent_summary", "assigned_role"}}};
  }
  return {
      {"$schema", "https://json-schema.org/draft/2020-12/schema"},
      {"$id", "https://quanux.org/schemas/nemosymphtron/quant_workload.schema.json"},
      {"title", "NemoSymphTron Quantitative Workload Schema"},
      {"type", "object"},
      {"required", {"kernel_id", "num_paths", "num_assets", "time_steps"}}};
}

json template_by_name(const std::string& name) {
  if (name == "vera_rubin_shv") {
    return nemosymphtron::shv::HardwareVectorProfile::DefaultVeraRubin().ToJson();
  }
  if (name == "dgx_spark_scv_cluster") {
    return nemosymphtron::scv::CloudVectorFabricSpec::DefaultDgxSparkCluster().ToJson();
  }
  if (name == "vera_rubin_scv_superpod") {
    return nemosymphtron::scv::CloudVectorFabricSpec::DefaultVeraRubinSuperPod().ToJson();
  }
  if (name == "nemotron_siv_policy") {
    nemosymphtron::siv::SIVPolicyEnforcer siv;
    return siv.ToJson();
  }
  if (name == "multi_agent_cluster_profiles") {
    nemosymphtron::siv::SIVPolicyEnforcer siv;
    return {
        {"schema", "nemosymphtron.cluster.profiles.v1"},
        {"patterns", json::array({"PIPELINE", "WORKER_POOL", "ADVERSARIAL_CROSS_CHECK"})},
        {"siv_vector", siv.ToJson()}};
  }
  return nemosymphtron::shv::HardwareVectorProfile::DefaultDgxSpark().ToJson();
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const fs::path self_exe = fs::canonical(fs::path(argv[0]));
    const std::string exe_digest = nemosymphtron::file_sha256(self_exe.string());
    const json commands = build_command_records();
    const std::string registry_digest = nemosymphtron::buffer_sha256(commands.dump());

    const ParsedArgs args = parse_cli(argc, argv);
    if (args.route.empty() || args.flags.contains("help")) {
      std::cout << "nemosymphtron-ctl (QuanuX NemoSymphTron qxctl extension owner 0.2.0-alpha.1)\n";
      return 0;
    }
    if (args.flags.contains("version") && args.route.size() == 1 && args.route[0] == "version") {
      std::cout << "nemosymphtron-ctl 0.2.0-alpha.1\n";
      return 0;
    }

    const std::string r0 = args.route[0];
    const std::string r1 = args.route.size() > 1 ? args.route[1] : "";
    const std::string r2 = args.route.size() > 2 ? args.route[2] : "";

    if (r0 == "commands" && (r1 == "manifest" || r1 == "list")) {
      json manifest = {
          {"protocol", "symphony.qxctl.command-registry.v1"},
          {"registry_kind", "observed"},
          {"client_version", "0.2.0-alpha.1"},
          {"executable_digest", exe_digest},
          {"registry_digest", registry_digest},
          {"commands", commands}};
      std::cout << manifest.dump(2) << "\n";
      return 0;
    }

    if (r0 == "nst" && r1 == "schema") {
      const std::string name = args.flags.contains("name") ? args.flags.at("name") : "quant_workload";
      std::cout << schema_by_name(name).dump(2) << "\n";
      return 0;
    }
    if (r0 == "nst" && r1 == "template") {
      const std::string name = args.flags.contains("name") ? args.flags.at("name") : "dgx_spark_shv";
      std::cout << template_by_name(name).dump(2) << "\n";
      return 0;
    }
    if (r0 == "siv" && r1 == "schema") {
      std::cout << schema_by_name("siv_policy").dump(2) << "\n";
      return 0;
    }
    if (r0 == "siv" && r1 == "template") {
      const std::string profile = args.flags.contains("profile") ? args.flags.at("profile") : "nemotron_siv_policy";
      std::cout << template_by_name(profile).dump(2) << "\n";
      return 0;
    }

    const std::string prefix = require_flag(args, "prefix");
    const std::string version = require_flag(args, "version");
    const std::string tops_id = require_flag(args, "tops-id");
    auto [state, state_digest] = load_or_init_state(prefix, version, tops_id);

    if (r0 == "nst" && (r1 == "status" || r1 == "doctor" || r1 == "telemetry")) {
      nemosymphtron::ipc::AgentIPCRequest req;
      req.header.request_id = "ctl-" + r1;
      req.header.agent_id = "antigravity-operator";
      req.header.tops_id = tops_id;
      req.header.siv_token_flags =
          nemosymphtron::siv::SIV_AUDITED_BEAD |
          nemosymphtron::siv::SIV_RISK_CHECK_PASSED |
          nemosymphtron::siv::SIV_DETERMINISTIC_MATH_DELEGATED;
      req.kind = nemosymphtron::ipc::PayloadKind::ENGINE_TELEMETRY;
      auto live = try_forward_to_engine(args, req);
      json out = {
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "nst." + r1},
          {"prefix", prefix},
          {"version", version},
          {"tops_id", tops_id},
          {"state_digest", state_digest},
          {"cluster_state", state["cluster_state"]},
          {"hardware_locked", state["hardware_locked"]},
          {"fabric_up", state["fabric_up"]},
          {"engine_live_connected", live.has_value()},
          {"hardware_profile", nemosymphtron::shv::HardwareVectorProfile::DefaultDgxSpark().ToJson()},
          {"readiness", "READY"}};
      if (live.has_value()) {
        out["live_engine_telemetry"] = live->telemetry_snapshot;
      }
      std::cout << out.dump(2) << "\n";
      return 0;
    }

    if (r0 == "nst" && r1 == "quant" && r2 == "run") {
      const std::string spec_path = require_flag(args, "spec");
      const json spec_json = json::parse(read_text_file(spec_path));
      const std::string kernel_id = spec_json.value("kernel_id", "MONTE_CARLO_VAR");

      nemosymphtron::ipc::AgentIPCRequest req;
      req.header.request_id = "ctl-quant-run";
      req.header.agent_id = spec_json.value("agent_id", "antigravity-operator");
      req.header.tops_id = tops_id;
      req.header.siv_token_flags =
          nemosymphtron::siv::SIV_AUDITED_BEAD |
          nemosymphtron::siv::SIV_RISK_CHECK_PASSED |
          nemosymphtron::siv::SIV_DETERMINISTIC_MATH_DELEGATED |
          nemosymphtron::siv::SIV_PRIORITY_BOOST;

      if (kernel_id == "CHOLESKY_DECOMP") {
        req.kind = nemosymphtron::ipc::PayloadKind::CHOLESKY_FACTOR;
        req.cholesky_request.matrix_dim = spec_json.value("num_assets", 4U);
        req.cholesky_request.flattened_matrix =
            spec_json.value("covariance_flat", std::vector<float>{1.0f, 0.2f, 0.2f, 1.0f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 0.2f, 1.0f, 0.2f, 0.2f, 0.2f, 0.2f, 1.0f});
      } else if (kernel_id == "CUOPT_PORTFOLIO") {
        req.kind = nemosymphtron::ipc::PayloadKind::PORTFOLIO_OPTIM;
        req.cuopt_request.tickers = spec_json.value("tickers", std::vector<std::string>{"NVDA", "MSFT", "AAPL", "GOOG"});
        req.cuopt_request.objective = spec_json.value("objective", "max_sharpe");
        req.cuopt_request.target_return = spec_json.value("target_return", 0.14);
        req.cuopt_request.expected_returns = spec_json.value("expected_returns", std::vector<double>{0.18, 0.14, 0.12, 0.15});
      } else if (kernel_id == "TX_RISK_SCORE") {
        req.kind = nemosymphtron::ipc::PayloadKind::TRANSACTION_RISK;
        req.risk_request.transaction_id = spec_json.value("transaction_id", "tx-001");
        req.risk_request.amount = spec_json.value("amount", 250000.0);
        req.risk_request.merchant_code = spec_json.value("merchant_code", std::string("FIN_INST"));
        req.risk_request.velocity_score = spec_json.value("velocity_score", 0.35);
      } else if (kernel_id == "FFT_OPTION_PRICING") {
        req.kind = nemosymphtron::ipc::PayloadKind::FFT_OPTION_PRICE;
        req.fft_request.spot_price = spec_json.value("spot_price", 100.0);
        req.fft_request.strike_price = spec_json.value("strike_price", 100.0);
        req.fft_request.maturity_years = spec_json.value("maturity_years", 1.0);
        req.fft_request.risk_free_rate = spec_json.value("risk_free_rate", 0.05);
        req.fft_request.volatility = spec_json.value("volatility", 0.22);
        req.fft_request.grid_points = spec_json.value("grid_points", 4096U);
      } else if (kernel_id == "NCCL_MULTI_NODE_SIM") {
        req.kind = nemosymphtron::ipc::PayloadKind::NCCL_COLLECTIVE;
        req.nccl_request.world_size = spec_json.value("world_size", 2U);
        req.nccl_request.fabric_type = spec_json.value("fabric_type", "roce_v2_200g");
        req.nccl_request.local_tensor = spec_json.value("local_tensor", std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f});
      } else {
        req.kind = nemosymphtron::ipc::PayloadKind::MONTE_CARLO_VAR;
        req.mc_request.spot_price = spec_json.value("spot_price", 100.0);
        req.mc_request.volatility = spec_json.value("volatility", 0.22);
        req.mc_request.drift = spec_json.value("drift", 0.05);
        req.mc_request.paths = spec_json.value("num_paths", 10000U);
        req.mc_request.steps = spec_json.value("time_steps", 64U);
        req.mc_request.seed = spec_json.value("seed", 42ULL);
      }

      auto live = try_forward_to_engine(args, req);
      nemosymphtron::ipc::AgentIPCResponse ipc_resp;
      std::string mode = "live_engine_ipc";
      if (live.has_value() && live->status_code == 0) {
        ipc_resp = *live;
      } else {
        mode = "local_owner_engine";
        nemosymphtron::engine::DualPoolQuantEngine local_engine;
        ipc_resp = local_engine.ExecuteAgentIPC(req);
      }
      json out = {
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "nst.quant.run"},
          {"kernel_id", kernel_id},
          {"execution_mode", mode},
          {"status_code", ipc_resp.status_code},
          {"hardware_backend", ipc_resp.hardware_backend},
          {"executed_stream_id", ipc_resp.executed_stream_id},
          {"var_99", ipc_resp.mc_result.var_99},
          {"cvar_99", ipc_resp.mc_result.cvar_99},
          {"expected_terminal_price", ipc_resp.mc_result.expected_terminal_price},
          {"sharpe_ratio", ipc_resp.cuopt_result.sharpe_ratio},
          {"call_price", ipc_resp.fft_result.call_price},
          {"anomaly_score", ipc_resp.risk_result.anomaly_score},
          {"risk_decision", ipc_resp.risk_result.decision}};
      std::cout << out.dump(2) << "\n";
      return 0;
    }

    if (r0 == "nst" && r1 == "cluster" && (r2 == "apply" || r2 == "recover")) {
      const std::string expected = require_flag(args, "expected-state-digest");
      if (r2 == "apply") {
        const std::string spec_path = require_flag(args, "spec");
        const json spec_json = json::parse(read_text_file(spec_path));
        state["cluster_state"] = "ACTIVE";
        state["cluster_spec"] = spec_json;
      } else {
        state["cluster_state"] = "RECOVERED";
      }
      const std::string new_digest =
          persist_state_with_cas(prefix, tops_id, expected, state, state_digest, "nst.cluster." + r2);
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "nst.cluster." + r2},
          {"cluster_state", state["cluster_state"]},
          {"previous_state_digest", state_digest},
          {"new_state_digest", new_digest}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "siv" && r1 == "inspect") {
      json out = {
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "siv.inspect"},
          {"tops_id", tops_id},
          {"state_digest", state_digest},
          {"siv_policy", state["siv_policy"]}};
      if (auto it = args.flags.find("agent-id"); it != args.flags.end()) {
        out["selected_agent_id"] = it->second;
      }
      std::cout << out.dump(2) << "\n";
      return 0;
    }

    if (r0 == "siv" && r1 == "audit-log") {
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "siv.audit-log"},
          {"state_digest", state_digest},
          {"audit_log", state["audit_log"]}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "siv" && r1 == "policy" && r2 == "propose") {
      const std::string policy_path = require_flag(args, "policy");
      const json proposed = json::parse(read_text_file(policy_path));
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "siv.policy.propose"},
          {"canonical_applied", false},
          {"proposed_policy_sha256", nemosymphtron::file_sha256(policy_path)},
          {"proposed_policy", proposed},
          {"current_state_digest", state_digest}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "siv" && r1 == "policy" && r2 == "apply") {
      const std::string policy_path = require_flag(args, "policy");
      const std::string expected = require_flag(args, "expected-state-digest");
      const json policy_json = json::parse(read_text_file(policy_path));
      state["siv_policy"] = policy_json;
      const std::string new_digest =
          persist_state_with_cas(prefix, tops_id, expected, state, state_digest, "siv.policy.apply");
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "siv.policy.apply"},
          {"previous_state_digest", state_digest},
          {"new_state_digest", new_digest},
          {"siv_policy", state["siv_policy"]}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "siv" && r1 == "agent" && (r2 == "authorize" || r2 == "revoke")) {
      const std::string agent_id = require_flag(args, "agent-id");
      const std::string expected = require_flag(args, "expected-state-digest");
      if (!state["siv_policy"].contains("authorized_agents")) {
        state["siv_policy"]["authorized_agents"] = json::object();
      }
      if (r2 == "authorize") {
        const std::string bitmask_str = require_flag(args, "bitmask");
        const uint64_t bitmask = std::stoull(bitmask_str, nullptr, 0);
        const uint64_t max_vram =
            args.flags.contains("max-vram-mb") ? std::stoull(args.flags.at("max-vram-mb")) : 16384ULL;
        state["siv_policy"]["authorized_agents"][agent_id] = {
            {"agent_id", agent_id},
            {"bitmask", bitmask},
            {"max_vram_mb", max_vram},
            {"authorized", true}};
      } else {
        state["siv_policy"]["authorized_agents"].erase(agent_id);
      }
      const std::string new_digest =
          persist_state_with_cas(prefix, tops_id, expected, state, state_digest, "siv.agent." + r2);
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "siv.agent." + r2},
          {"agent_id", agent_id},
          {"previous_state_digest", state_digest},
          {"new_state_digest", new_digest}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "siv" && r1 == "model" && r2 == "route") {
      const std::string agent_id = args.flags.contains("agent-id") ? args.flags.at("agent-id") : "antigravity-operator";
      const std::string prompt = require_flag(args, "prompt");
      const std::string complexity = args.flags.contains("complexity") ? args.flags.at("complexity") : "HIGH";
      const bool rubin = (complexity == "RUBIN" || complexity == "ULTRA");
      nemosymphtron::siv::SIVPolicyEnforcer siv;
      const auto decision = siv.RouteCognitiveDirective(prompt, rubin);
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "siv.model.route"},
          {"agent_id", agent_id},
          {"routing_decision", decision}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "beads" && r1 == "dag" && r2 == "list") {
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "beads.dag.list"},
          {"state_digest", state_digest},
          {"beads", state["beads_dag"]}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "beads" && r1 == "dag" && r2 == "propose") {
      const std::string spec_path = require_flag(args, "spec");
      const json spec_json = json::parse(read_text_file(spec_path));
      nemosymphtron::beads::BeadsEngine engine;
      const auto node = engine.CreateTaskBead(
          spec_json.value("assigned_role", "coder"),
          spec_json.value("intent_summary", "Proposed task"),
          {});
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "beads.dag.propose"},
          {"canonical_applied", false},
          {"proposed_bead", {
              {"bead_id", spec_json.value("bead_id", node.bead_id)},
              {"merkle_hash", node.bead_id},
              {"intent_summary", node.task_payload},
              {"assigned_role", node.target_capability}}}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "beads" && r1 == "dag" && r2 == "commit") {
      const std::string expected = require_flag(args, "expected-state-digest");
      const std::string bead_id = require_flag(args, "bead-id");
      const std::string status_str = require_flag(args, "status");
      const std::string agent_id = args.flags.contains("agent-id") ? args.flags.at("agent-id") : "antigravity-operator";
      bool updated = false;
      for (auto& item : state["beads_dag"]) {
        if (item.value("bead_id", "") == bead_id) {
          item["status"] = status_str;
          item["assigned_agent_id"] = agent_id;
          updated = true;
        }
      }
      if (!updated) {
        state["beads_dag"].push_back({
            {"bead_id", bead_id},
            {"status", status_str},
            {"assigned_agent_id", agent_id},
            {"merkle_hash", nemosymphtron::buffer_sha256(bead_id + "|" + status_str)}});
      }
      const std::string new_digest =
          persist_state_with_cas(prefix, tops_id, expected, state, state_digest, "beads.dag.commit");
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "beads.dag.commit"},
          {"bead_id", bead_id},
          {"status", status_str},
          {"previous_state_digest", state_digest},
          {"new_state_digest", new_digest}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "beads" && r1 == "leases" && r2 == "list") {
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "beads.leases.list"},
          {"state_digest", state_digest},
          {"file_leases", state["file_leases"]}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "beads" && r1 == "leases" && (r2 == "checkout" || r2 == "release" || r2 == "unlock")) {
      const std::string file_path = require_flag(args, "file");
      const uint64_t now_ns = now_epoch_ns();
      if (r2 == "checkout") {
        const std::string agent_id = require_flag(args, "agent-id");
        const uint64_t duration_sec =
            args.flags.contains("duration-sec") ? std::stoull(args.flags.at("duration-sec")) : 60ULL;
        const std::string expected_sha =
            args.flags.contains("expected-sha256") ? args.flags.at("expected-sha256") : "";
        if (state["file_leases"].contains(file_path)) {
          const auto& existing = state["file_leases"][file_path];
          const uint64_t exp = existing.value("expires_at_epoch_ns", 0ULL);
          const std::string holder = existing.value("holder_agent_id", "");
          require(
              exp <= now_ns || holder == agent_id,
              "CAS Lease Conflict: file '" + file_path + "' is already leased by '" + holder + "'");
          if (!expected_sha.empty() && !existing.value("base_tree_sha256", "").empty()) {
            require(
                existing["base_tree_sha256"].get<std::string>() == expected_sha,
                "CAS base_tree_sha256 mismatch on checkout");
          }
        }
        state["file_leases"][file_path] = {
            {"file_path", file_path},
            {"holder_agent_id", agent_id},
            {"acquired_at_epoch_ns", now_ns},
            {"expires_at_epoch_ns", now_ns + duration_sec * 1'000'000'000ULL},
            {"base_tree_sha256", expected_sha}};
      } else if (r2 == "release") {
        const std::string agent_id = require_flag(args, "agent-id");
        require(state["file_leases"].contains(file_path), "No active lease for file: " + file_path);
        require(
            state["file_leases"][file_path].value("holder_agent_id", "") == agent_id,
            "Only lease holder may release lease");
        state["file_leases"].erase(file_path);
      } else {
        const std::string expected = require_flag(args, "expected-state-digest");
        require(expected == state_digest, "CAS state digest mismatch for unlock");
        state["file_leases"].erase(file_path);
      }
      const std::string dumped = state.dump(2) + "\n";
      write_text_file(state_file_path(prefix, tops_id), dumped);
      const std::string new_digest = nemosymphtron::buffer_sha256(dumped);
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "beads.leases." + r2},
          {"file", file_path},
          {"new_state_digest", new_digest},
          {"file_leases", state["file_leases"]}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "beads" && r1 == "bus" && r2 == "poll") {
      const std::string agent_id = require_flag(args, "agent-id");
      json messages = json::array();
      for (const auto& msg : state["message_bus"]) {
        if (msg.value("recipient_agent_id", "") == agent_id || msg.value("recipient_agent_id", "") == "*") {
          messages.push_back(msg);
        }
      }
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "beads.bus.poll"},
          {"agent_id", agent_id},
          {"messages", messages}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "beads" && r1 == "bus" && r2 == "dispatch") {
      const std::string sender = require_flag(args, "sender");
      const std::string recipient = require_flag(args, "recipient");
      const std::string pattern = args.flags.contains("pattern") ? args.flags.at("pattern") : "PIPELINE";
      const std::string bead_id = args.flags.contains("bead-id") ? args.flags.at("bead-id") : "";
      const std::string payload = require_flag(args, "payload");
      json msg = {
          {"message_id", "msg-" + std::to_string(now_epoch_ns())},
          {"sender_agent_id", sender},
          {"recipient_agent_id", recipient},
          {"pattern", pattern},
          {"bead_id", bead_id},
          {"payload_json", payload},
          {"timestamp_epoch_ns", now_epoch_ns()}};
      state["message_bus"].push_back(msg);
      const std::string dumped = state.dump(2) + "\n";
      write_text_file(state_file_path(prefix, tops_id), dumped);
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "beads.bus.dispatch"},
          {"dispatched_message", msg}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "shv" && r1 == "nst-discover") {
      const std::string arch = args.flags.contains("arch") ? args.flags.at("arch") : "BLACKWELL_GB10_SM120";
      const auto profile =
          (arch.find("RUBIN") != std::string::npos || arch.find("130") != std::string::npos)
              ? nemosymphtron::shv::HardwareVectorProfile::DefaultVeraRubin()
              : nemosymphtron::shv::HardwareVectorProfile::DefaultDgxSpark();
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "shv.nst-discover"},
          {"hardware_profile", profile.ToJson()}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "shv" && r1 == "nst-verify-isolation") {
      const auto profile = nemosymphtron::shv::HardwareVectorProfile::DefaultDgxSpark();
      const auto lock_status = nemosymphtron::shv::SHVEnforcer::EnforceThreadAndMemoryLocks(0, 15, false);
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "shv.nst-verify-isolation"},
          {"compliant", lock_status.affinity_applied && lock_status.gpu_clocks_locked},
          {"bound_core_range", lock_status.bound_core_range},
          {"mode_detail", lock_status.mode_detail},
          {"profile", profile.ToJson()}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "shv" && r1 == "nst-lock") {
      const std::string expected = require_flag(args, "expected-state-digest");
      const auto profile = nemosymphtron::shv::HardwareVectorProfile::DefaultDgxSpark();
      const auto lock_status = nemosymphtron::shv::SHVEnforcer::EnforceThreadAndMemoryLocks(0, 15, true);
      state["hardware_locked"] = true;
      state["locked_shv_profile"] = profile.ToJson();
      const std::string new_digest =
          persist_state_with_cas(prefix, tops_id, expected, state, state_digest, "shv.nst-lock");
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "shv.nst-lock"},
          {"hardware_locked", true},
          {"bound_core_range", lock_status.bound_core_range},
          {"new_state_digest", new_digest}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "shv" && r1 == "nst-benchmark-bus") {
      const std::string arch = args.flags.contains("arch") ? args.flags.at("arch") : "BLACKWELL_GB10_SM120";
      const bool rubin = (arch.find("RUBIN") != std::string::npos || arch.find("130") != std::string::npos);
      const auto profile = rubin
                               ? nemosymphtron::shv::HardwareVectorProfile::DefaultVeraRubin()
                               : nemosymphtron::shv::HardwareVectorProfile::DefaultDgxSpark();
      const auto bench = nemosymphtron::shv::SHVEnforcer::BenchmarkCoherentBus(
          rubin ? "hbm4-page" : "nvlink-c2c", profile);
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "shv.nst-benchmark-bus"},
          {"benchmark", bench}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "scv" && r1 == "nst-inspect-fabric") {
      const std::string arch = args.flags.contains("arch") ? args.flags.at("arch") : "BLACKWELL_GB10_SM120";
      const bool rubin = (arch.find("RUBIN") != std::string::npos || arch.find("130") != std::string::npos);
      const auto spec = rubin
                            ? nemosymphtron::scv::CloudVectorFabricSpec::DefaultVeraRubinSuperPod()
                            : nemosymphtron::scv::CloudVectorFabricSpec::DefaultDgxSparkCluster();
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "scv.nst-inspect-fabric"},
          {"fabric_spec", spec.ToJson()}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "scv" && r1 == "nst-test-fabric") {
      const auto spec = nemosymphtron::scv::CloudVectorFabricSpec::DefaultDgxSparkCluster();
      const auto bench = nemosymphtron::scv::SCVFabricManager::RunFabricBenchmark("roce-rdma", spec);
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "scv.nst-test-fabric"},
          {"assessment", bench}}
                       .dump(2)
                << "\n";
      return 0;
    }

    if (r0 == "scv" && r1 == "nst-cluster-up") {
      const std::string expected = require_flag(args, "expected-state-digest");
      const auto spec = nemosymphtron::scv::CloudVectorFabricSpec::DefaultDgxSparkCluster();
      const auto bench = nemosymphtron::scv::SCVFabricManager::RunFabricBenchmark("roce-rdma", spec);
      state["fabric_up"] = true;
      state["active_scv_fabric"] = spec.ToJson();
      const std::string new_digest =
          persist_state_with_cas(prefix, tops_id, expected, state, state_digest, "scv.nst-cluster-up");
      std::cout << json{
          {"protocol", "nemosymphtron.result.v1"},
          {"route", "scv.nst-cluster-up"},
          {"fabric_up", true},
          {"assessment", bench},
          {"new_state_digest", new_digest}}
                       .dump(2)
                << "\n";
      return 0;
    }

    throw std::runtime_error("Unknown command route");
  } catch (const std::exception& e) {
    std::cerr << "nemosymphtron-ctl error: " << e.what() << "\n";
    return 1;
  }
}
