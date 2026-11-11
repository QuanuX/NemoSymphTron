#include "nemosymphtron/beads_engine.hpp"
#include "nemosymphtron/scv_fabric_manager.hpp"
#include "nemosymphtron/shv_hardware_guard.hpp"
#include "nemosymphtron/siv_policy_enforcer.hpp"

#include <iostream>
#include <stdexcept>

static void check(bool ok, const char* msg) {
  if (!ok) throw std::runtime_error(msg);
}

int main() {
  using namespace nemosymphtron;

  // 1. Verify SIV constexpr O(1) bitmask checks
  constexpr std::uint64_t valid_mask =
      siv::SIV_AUDITED_BEAD | siv::SIV_RISK_CHECK_PASSED | siv::SIV_DETERMINISTIC_MATH_DELEGATED;
  static_assert(siv::ValidateSIVCompliance(valid_mask));
  static_assert(!siv::ValidateSIVCompliance(siv::SIV_AUDITED_BEAD));

  siv::SIVPolicyEnforcer enforcer;
  const auto mask = enforcer.ValidateAgentCall(
      "antigravity-operator", "compute_var_simulation", {{"paths", 50000}});
  check(siv::ValidateSIVCompliance(mask), "SIV compliance check failed");
  check((mask & siv::SIV_PRIORITY_BOOST) != 0, "Expected SIV_PRIORITY_BOOST");

  const auto route = enforcer.RouteCognitiveDirective(
      "Run Monte Carlo VaR for spot $185.5 volatility 22% with 50k paths", false);
  check(route["recommended_tool"] == "compute_var_simulation", "Unexpected recommended tool");
  check(route["extracted_parameters"]["paths"] == 50000, "Unexpected extracted paths");

  // 2. Verify Beads Merkle-DAG & CAS file leases
  beads::BeadsEngine beads_engine(false);
  const auto b1 = beads_engine.CreateTaskBead("architect", "Design CUDA kernel", {}, 1);
  check(b1.bead_id.starts_with("sha256:"), "Expected sha256 bead_id");
  const auto b2 = beads_engine.CreateTaskBead("coder", "Implement CUDA kernel", {b1.bead_id}, 1);
  check(!beads_engine.AreParentsCommitted(b2.bead_id), "Parent should not be committed yet");
  check(
      beads_engine.TransitionBead(
          b1.bead_id, beads::BeadState::COMMITTED, "claude-architect", "sha256:out1"),
      "Parent transition failed");
  check(beads_engine.AreParentsCommitted(b2.bead_id), "Parent should be committed");

  // CAS Lease acquisition and conflict detection
  ipc::FileLeaseRequest acquire_a;
  acquire_a.resource_uri = "src/kernel.cu";
  acquire_a.claimant_agent_id = "claude-architect";
  acquire_a.action = 0; // LOCK_ACQUIRE
  acquire_a.expected_hash = "sha256:1111";
  acquire_a.lease_duration_ms = 60000;
  const auto res_a = beads_engine.HandleLeaseRequest(acquire_a);
  check(res_a.granted, "Lease A should be granted");

  // Conflicting checkout by another agent must fail closed
  ipc::FileLeaseRequest acquire_b = acquire_a;
  acquire_b.claimant_agent_id = "cursor-worker";
  const auto res_b = beads_engine.HandleLeaseRequest(acquire_b);
  check(!res_b.granted, "Conflicting lease B must be rejected");

  // Release by holder succeeds
  ipc::FileLeaseRequest release_a = acquire_a;
  release_a.action = 1; // LOCK_RELEASE
  release_a.updated_hash = "sha256:2222";
  const auto rel_a = beads_engine.HandleLeaseRequest(release_a);
  check(rel_a.granted, "Lease A release should succeed");

  // P2P Message bus
  ipc::AgentDirectMessage msg;
  msg.message_id = "msg-01";
  msg.source_agent_id = "claude-architect";
  msg.target_agent_id = "symphony-codex";
  msg.pattern = 2; // ADVERSARIAL_CROSS_CHECK
  msg.intent = "verify_kernel";
  msg.payload = R"({"review":"ready"})";
  beads_engine.DispatchDirectMessage(msg);
  const auto inbox = beads_engine.PollMessages("symphony-codex", true);
  check(inbox.size() == 1 && inbox[0].intent == "verify_kernel", "P2P message poll failed");

  // 3. Verify SHV & SCV profiles for DGX Spark (Blackwell) and Vera-Rubin
  const auto spark_shv = shv::HardwareVectorProfile::DefaultDgxSpark();
  check(spark_shv.target_sm == "sm_120", "Expected sm_120");
  const auto rubin_shv = shv::HardwareVectorProfile::DefaultVeraRubin();
  check(rubin_shv.target_sm == "sm_130", "Expected sm_130");

  const auto spark_scv = scv::CloudVectorFabricSpec::DefaultDgxSparkCluster();
  check(spark_scv.port_speed_gbps == 200, "Expected 200G");
  const auto rubin_scv = scv::CloudVectorFabricSpec::DefaultVeraRubinSuperPod();
  check(rubin_scv.port_speed_gbps == 800 && rubin_scv.in_network_reduction_sharp, "Expected 800G SHARP");

  std::cout << "siv_beads_test PASSED\n";
  return 0;
}
