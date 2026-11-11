#include "nemosymphtron/cuda_quant_worker.hpp"
#include "nemosymphtron/siv_policy_enforcer.hpp"
#include "nemosymphtron/wire_protocol.hpp"

#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

static void check(bool ok, const char* msg) {
  if (!ok) throw std::runtime_error(msg);
}

int main() {
  using namespace nemosymphtron;

  int sv[2] = {-1, -1};
  check(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair failed");

  ipc::AgentIPCRequest req;
  req.header.request_id = "req-ipc-001";
  req.header.agent_id = "antigravity-operator";
  req.header.tops_id = "11111111-2222-3333-4444-555555555555";
  req.header.siv_token_flags =
      siv::SIV_AUDITED_BEAD |
      siv::SIV_RISK_CHECK_PASSED |
      siv::SIV_DETERMINISTIC_MATH_DELEGATED |
      siv::SIV_PRIORITY_BOOST;
  req.kind = ipc::PayloadKind::MONTE_CARLO_VAR;
  req.mc_request.spot_price = 100.0;
  req.mc_request.volatility = 0.22;
  req.mc_request.drift = 0.05;
  req.mc_request.paths = 4096;
  req.mc_request.steps = 32;
  req.mc_request.seed = 42;

  const auto encoded_req = ipc::serialize_request(req);
  check(ipc::send_ipc_frame(sv[0], encoded_req), "send_ipc_frame failed");

  std::string received_bytes;
  check(ipc::recv_ipc_frame(sv[1], received_bytes), "recv_ipc_frame failed");
  const auto decoded_req = ipc::deserialize_request(received_bytes);

  check(decoded_req.header.request_id == req.header.request_id, "request_id mismatch");
  check(decoded_req.header.agent_id == req.header.agent_id, "agent_id mismatch");
  check(decoded_req.header.tops_id == req.header.tops_id, "tops_id mismatch");
  check(decoded_req.kind == ipc::PayloadKind::MONTE_CARLO_VAR, "kind mismatch");
  check(decoded_req.mc_request.paths == 4096, "paths mismatch");

  engine::DualPoolQuantEngine quant_engine(false);
  const auto resp = quant_engine.ExecuteAgentIPC(decoded_req);
  check(resp.status_code == 0, "ExecuteAgentIPC failed");
  check(resp.mc_result.var_99 > 0.0, "Expected positive VaR");

  check(ipc::send_ipc_frame(sv[1], ipc::serialize_response(resp)), "send_ipc_frame resp failed");
  std::string resp_bytes;
  check(ipc::recv_ipc_frame(sv[0], resp_bytes), "recv_ipc_frame resp failed");
  const auto decoded_resp = ipc::deserialize_response(resp_bytes);
  check(decoded_resp.request_id == req.header.request_id, "resp request_id mismatch");
  check(decoded_resp.status_code == 0, "resp status_code mismatch");
  check(decoded_resp.mc_result.var_99 == resp.mc_result.var_99, "resp var_99 mismatch");

  ::close(sv[0]);
  ::close(sv[1]);
  std::cout << "engine_ipc_test PASSED\n";
  return 0;
}
