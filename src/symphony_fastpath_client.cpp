#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

#include "nemosymphtron/symphony_fastpath.hpp"
#include "nlohmann/json.hpp"

int main(int argc, char** argv) {
  std::string shm_name = "/nemosymphtron_fastpath";
  std::string task_str = "mc";
  std::uint64_t seq_id = 1001;
  double spot = 240.50;
  double vol = 0.28;
  double drift = 0.04;
  double strike = 240.50;
  std::uint32_t paths = 10000;
  std::uint32_t steps = 252;
  std::uint32_t matrix_dim = 8;
  std::uint32_t world_size = 2;
  int timeout_ms = 5000;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--shm-name" && i + 1 < argc) shm_name = argv[++i];
    else if (arg == "--task" && i + 1 < argc) task_str = argv[++i];
    else if (arg == "--sequence-id" && i + 1 < argc) seq_id = std::stoull(argv[++i]);
    else if (arg == "--spot" && i + 1 < argc) spot = std::stod(argv[++i]);
    else if (arg == "--vol" && i + 1 < argc) vol = std::stod(argv[++i]);
    else if (arg == "--drift" && i + 1 < argc) drift = std::stod(argv[++i]);
    else if (arg == "--strike" && i + 1 < argc) strike = std::stod(argv[++i]);
    else if (arg == "--paths" && i + 1 < argc) paths = static_cast<std::uint32_t>(std::stoul(argv[++i]));
    else if (arg == "--steps" && i + 1 < argc) steps = static_cast<std::uint32_t>(std::stoul(argv[++i]));
    else if (arg == "--matrix-dim" && i + 1 < argc) matrix_dim = static_cast<std::uint32_t>(std::stoul(argv[++i]));
    else if (arg == "--world-size" && i + 1 < argc) world_size = static_cast<std::uint32_t>(std::stoul(argv[++i]));
    else if (arg == "--timeout-ms" && i + 1 < argc) timeout_ms = std::stoi(argv[++i]);
  }

  using nemosymphtron::fastpath::TaskType;
  TaskType task = TaskType::MonteCarloVaR;
  if (task_str == "cholesky") task = TaskType::CholeskyDecomp;
  else if (task_str == "cuopt") task = TaskType::PortfolioOptimization;
  else if (task_str == "fft") task = TaskType::FftOptionPricing;
  else if (task_str == "nccl") task = TaskType::NcclAllReduce;

  try {
    nemosymphtron::fastpath::PosixSharedMemorySegment segment(shm_name, false);
    auto* channel = segment.channel();

    nemosymphtron::fastpath::FastPathCommand cmd{
        .sequence_id = seq_id,
        .task = task,
        .spot_price = spot,
        .volatility = vol,
        .drift = drift,
        .strike_or_target = strike,
        .paths = paths,
        .steps = steps,
        .matrix_dim = matrix_dim,
        .world_size = world_size,
    };

    const auto roundtrip_start = std::chrono::high_resolution_clock::now();
    if (!channel->command_channel.Push(cmd)) {
      std::cerr << "{\"ok\":false,\"error\":\"Sovereign SHM command ring buffer is full\"}\n";
      return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    nemosymphtron::fastpath::FastPathResponse resp{};
    bool received = false;
    while (std::chrono::steady_clock::now() < deadline) {
      if (channel->response_channel.Pop(resp)) {
        if (resp.sequence_id == seq_id) {
          received = true;
          break;
        }
      }
#if defined(__aarch64__)
      asm volatile("yield" ::: "memory");
#else
      std::this_thread::yield();
#endif
    }

    if (!received) {
      std::cerr << "{\"ok\":false,\"error\":\"Timed out waiting for Sovereign SHM response\"}\n";
      return 1;
    }

    const auto roundtrip_end = std::chrono::high_resolution_clock::now();
    const double roundtrip_us =
        std::chrono::duration<double, std::micro>(roundtrip_end - roundtrip_start).count();

    nlohmann::json out = {
        {"ok", resp.status_code == 0},
        {"status", resp.status_code == 0 ? "completed" : "failed"},
        {"channel", shm_name},
        {"sequence_id", resp.sequence_id},
        {"task", task_str},
        {"stream_priority_rank", resp.stream_priority_rank},
        {"var_99", resp.var_99},
        {"var_95", resp.var_95},
        {"cvar_99", resp.cvar_99},
        {"expected_terminal_price", resp.expected_terminal_price},
        {"secondary_metric", resp.secondary_metric},
        {"kernel_execution_ns", resp.execution_time_ns},
        {"roundtrip_latency_us", roundtrip_us},
    };
    std::cout << out.dump() << "\n";
    return resp.status_code == 0 ? 0 : 1;
  } catch (const std::exception& ex) {
    nlohmann::json err = {{"ok", false}, {"status", "error"}, {"error", ex.what()}};
    std::cout << err.dump() << "\n";
    return 1;
  }
}
