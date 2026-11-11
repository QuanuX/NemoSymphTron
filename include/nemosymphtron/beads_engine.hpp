#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "nemosymphtron/sha256.hpp"
#include "nemosymphtron/wire_protocol.hpp"
#include "nlohmann/json.hpp"

namespace nemosymphtron::beads {

enum class BeadState : std::uint32_t {
  PENDING   = 1,
  LEASED    = 2,
  EXECUTING = 3,
  COMMITTED = 4,
  FAILED    = 5
};

struct TaskBeadNode {
  std::string bead_id;
  std::vector<std::string> parent_bead_ids;
  std::string target_capability;
  std::string task_payload;
  std::uint64_t creation_timestamp_ns{0};
  std::uint32_t priority{1};
  BeadState status{BeadState::PENDING};
  std::string assigned_agent_id;
  std::string output_digest;
};

struct ResourceLease {
  std::string resource_uri;
  std::string holder_agent_id;
  std::string current_content_hash;
  std::chrono::steady_clock::time_point expires_at;
  std::uint64_t duration_ms{5000};
  bool dpu_offloaded{false};
};

class BeadsEngine {
public:
  explicit BeadsEngine(bool bluefield_dpu_offload = false)
      : bluefield_dpu_offload_(bluefield_dpu_offload) {}

  // Creates an immutable content-addressed TaskBead in the Merkle DAG
  TaskBeadNode CreateTaskBead(const std::string& target_capability,
                              const std::string& task_payload,
                              const std::vector<std::string>& parent_bead_ids,
                              std::uint32_t priority = 1) {
    std::lock_guard<std::mutex> lock(mtx_);
    for (const auto& pid : parent_bead_ids) {
      if (!dag_.contains(pid)) {
        throw std::runtime_error("[Beads DAG] Unknown parent bead: " + pid);
      }
    }
    const auto now_ns = NowNs();
    std::string material = target_capability + "|" + task_payload + "|" + std::to_string(now_ns);
    for (const auto& pid : parent_bead_ids) {
      material += "|" + pid;
    }
    const std::string bead_id = buffer_sha256(material);
    TaskBeadNode node{
        .bead_id = bead_id,
        .parent_bead_ids = parent_bead_ids,
        .target_capability = target_capability,
        .task_payload = task_payload,
        .creation_timestamp_ns = now_ns,
        .priority = priority,
        .status = BeadState::PENDING,
        .assigned_agent_id = "",
        .output_digest = "",
    };
    dag_[bead_id] = node;
    return node;
  }

  bool AreParentsCommitted(const std::string& bead_id) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = dag_.find(bead_id);
    if (it == dag_.end()) return false;
    for (const auto& pid : it->second.parent_bead_ids) {
      auto pit = dag_.find(pid);
      if (pit == dag_.end() || pit->second.status != BeadState::COMMITTED) {
        return false;
      }
    }
    return true;
  }

  bool TransitionBead(const std::string& bead_id,
                      BeadState new_state,
                      const std::string& agent_id,
                      const std::string& output_digest = "") {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = dag_.find(bead_id);
    if (it == dag_.end()) return false;
    for (const auto& pid : it->second.parent_bead_ids) {
      auto pit = dag_.find(pid);
      if (pit == dag_.end() || pit->second.status != BeadState::COMMITTED) {
        return false; // Cannot execute/commit before parent beads are COMMITTED
      }
    }
    it->second.status = new_state;
    if (!agent_id.empty()) it->second.assigned_agent_id = agent_id;
    if (!output_digest.empty()) it->second.output_digest = output_digest;
    return true;
  }

  // Compare-And-Swap (CAS) distributed file/model checkout
  ipc::FileLeaseResponse HandleLeaseRequest(const ipc::FileLeaseRequest& req) {
    std::lock_guard<std::mutex> lock(mtx_);
    const auto now = std::chrono::steady_clock::now();
    ipc::FileLeaseResponse resp;
    resp.resource_uri = req.resource_uri;
    resp.dpu_offloaded = bluefield_dpu_offload_;

    auto it = leases_.find(req.resource_uri);

    if (req.action == 0) { // LOCK_ACQUIRE
      if (it != leases_.end()) {
        if (it->second.expires_at > now && it->second.holder_agent_id != req.claimant_agent_id) {
          resp.granted = false;
          resp.holder_agent_id = it->second.holder_agent_id;
          resp.current_content_hash = it->second.current_content_hash;
          resp.error_message = "BEAD_LEASE_CONFLICT: Resource locked by " + it->second.holder_agent_id;
          return resp;
        }
        if (!req.expected_hash.empty() && !it->second.current_content_hash.empty() &&
            it->second.current_content_hash != req.expected_hash) {
          resp.granted = false;
          resp.holder_agent_id = it->second.holder_agent_id;
          resp.current_content_hash = it->second.current_content_hash;
          resp.error_message = "BEAD_CAS_MISMATCH: Expected " + req.expected_hash +
                               " but found " + it->second.current_content_hash;
          return resp;
        }
      }
      const std::uint64_t ttl = req.lease_duration_ms > 0 ? req.lease_duration_ms : 5000ULL;
      const std::string hash = !req.expected_hash.empty()
                                   ? req.expected_hash
                                   : (it != leases_.end() ? it->second.current_content_hash : "sha256:initial");
      leases_[req.resource_uri] = ResourceLease{
          .resource_uri = req.resource_uri,
          .holder_agent_id = req.claimant_agent_id,
          .current_content_hash = hash,
          .expires_at = now + std::chrono::milliseconds(ttl),
          .duration_ms = ttl,
          .dpu_offloaded = bluefield_dpu_offload_,
      };
      resp.granted = true;
      resp.holder_agent_id = req.claimant_agent_id;
      resp.current_content_hash = hash;
      resp.expires_at_ns = NowNs() + ttl * 1'000'000ULL;
      return resp;
    }

    if (req.action == 1) { // LOCK_RELEASE
      if (it != leases_.end() && it->second.holder_agent_id == req.claimant_agent_id) {
        if (!req.updated_hash.empty()) {
          it->second.current_content_hash = req.updated_hash;
        }
        it->second.expires_at = now; // Expire immediately
        resp.granted = true;
        resp.holder_agent_id = "";
        resp.current_content_hash = it->second.current_content_hash;
        return resp;
      }
      resp.granted = false;
      resp.error_message = "Cannot release lease not held by " + req.claimant_agent_id;
      return resp;
    }

    // LOCK_HEARTBEAT (2)
    if (it != leases_.end() && it->second.holder_agent_id == req.claimant_agent_id) {
      const std::uint64_t ttl = req.lease_duration_ms > 0 ? req.lease_duration_ms : it->second.duration_ms;
      it->second.expires_at = now + std::chrono::milliseconds(ttl);
      resp.granted = true;
      resp.holder_agent_id = req.claimant_agent_id;
      resp.current_content_hash = it->second.current_content_hash;
      resp.expires_at_ns = NowNs() + ttl * 1'000'000ULL;
      return resp;
    }
    resp.granted = false;
    resp.error_message = "Cannot heartbeat inactive or foreign lease";
    return resp;
  }

  bool ForceUnlock(const std::string& resource_uri, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = leases_.find(resource_uri);
    if (it == leases_.end()) return false;
    it->second.expires_at = std::chrono::steady_clock::now();
    it->second.holder_agent_id = "unlocked:" + reason;
    return true;
  }

  // Direct Agent-to-Agent & Subagent P2P Messaging Bus
  void DispatchDirectMessage(ipc::AgentDirectMessage msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (msg.timestamp_ns == 0) msg.timestamp_ns = NowNs();
    inboxes_[msg.target_agent_id].push_back(std::move(msg));
  }

  std::vector<ipc::AgentDirectMessage> PollMessages(const std::string& agent_id, bool consume = true) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = inboxes_.find(agent_id);
    if (it == inboxes_.end()) return {};
    std::vector<ipc::AgentDirectMessage> result = it->second;
    if (consume) {
      it->second.clear();
    }
    return result;
  }

  nlohmann::json ListLeasesJson() const {
    std::lock_guard<std::mutex> lock(mtx_);
    const auto now = std::chrono::steady_clock::now();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [uri, l] : leases_) {
      const bool active = l.expires_at > now;
      const auto remaining_ms = active
                                    ? std::chrono::duration_cast<std::chrono::milliseconds>(l.expires_at - now).count()
                                    : 0LL;
      arr.push_back({
          {"resource_uri", l.resource_uri},
          {"holder_agent_id", l.holder_agent_id},
          {"current_content_hash", l.current_content_hash},
          {"expires_in_ms", remaining_ms},
          {"status", active ? "ACTIVE" : "EXPIRED"},
          {"dpu_offloaded", l.dpu_offloaded},
      });
    }
    return arr;
  }

  nlohmann::json ListDagJson() const {
    std::lock_guard<std::mutex> lock(mtx_);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [id, b] : dag_) {
      arr.push_back({
          {"bead_id", b.bead_id},
          {"parent_bead_ids", b.parent_bead_ids},
          {"target_capability", b.target_capability},
          {"priority", b.priority},
          {"status", static_cast<std::uint32_t>(b.status)},
          {"assigned_agent_id", b.assigned_agent_id},
          {"output_digest", b.output_digest},
          {"creation_timestamp_ns", b.creation_timestamp_ns},
      });
    }
    return arr;
  }

private:
  static std::uint64_t NowNs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
  }

  mutable std::mutex mtx_;
  bool bluefield_dpu_offload_{false};
  std::unordered_map<std::string, TaskBeadNode> dag_;
  std::unordered_map<std::string, ResourceLease> leases_;
  std::unordered_map<std::string, std::vector<ipc::AgentDirectMessage>> inboxes_;
};

} // namespace nemosymphtron::beads
