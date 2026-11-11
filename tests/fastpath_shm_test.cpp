#include "nemosymphtron/cuda_quant_worker.hpp"
#include "nemosymphtron/symphony_fastpath.hpp"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

static void check(bool ok, const char* msg) {
  if (!ok) throw std::runtime_error(msg);
}

int main() {
  const std::string shm_name = "/nst_fp_test_" + std::to_string(::getpid() % 100000);
  nemosymphtron::fastpath::PosixSharedMemorySegment server_shm(shm_name, true);
  nemosymphtron::fastpath::PosixSharedMemorySegment client_shm(shm_name, false);

  auto* srv_chan = server_shm.channel();
  auto* cli_chan = client_shm.channel();

  static_assert(sizeof(nemosymphtron::fastpath::FastPathCommand) == 64);
  static_assert(sizeof(nemosymphtron::fastpath::FastPathResponse) == 64);

  nemosymphtron::engine::DualPoolQuantEngine engine(false);
  std::atomic<bool> worker_done{false};

  std::thread server_thread([&]() {
    int processed = 0;
    nemosymphtron::fastpath::FastPathCommand cmd{};
    while (processed < 5) {
      if (srv_chan->command_channel.Pop(cmd)) {
        const auto resp = engine.ExecuteFastPathCommand(cmd);
        while (!srv_chan->response_channel.Push(resp)) {
          std::this_thread::yield();
        }
        ++processed;
      } else {
        std::this_thread::yield();
      }
    }
    worker_done.store(true, std::memory_order_release);
  });

  const nemosymphtron::fastpath::TaskType tasks[] = {
      nemosymphtron::fastpath::TaskType::MonteCarloVaR,
      nemosymphtron::fastpath::TaskType::CholeskyDecomp,
      nemosymphtron::fastpath::TaskType::PortfolioOptimization,
      nemosymphtron::fastpath::TaskType::FftOptionPricing,
      nemosymphtron::fastpath::TaskType::NcclAllReduce};

  std::uint64_t seq = 100;
  for (auto t : tasks) {
    nemosymphtron::fastpath::FastPathCommand cmd{};
    cmd.sequence_id = ++seq;
    cmd.task = t;
    cmd.spot_price = 100.0;
    cmd.volatility = 0.22;
    cmd.drift = 0.05;
    cmd.strike_or_target = 100.0;
    cmd.paths = 4096;
    cmd.steps = 64;
    cmd.matrix_dim = 4;
    cmd.world_size = 2;

    check(cli_chan->command_channel.Push(cmd), "Push failed");
    nemosymphtron::fastpath::FastPathResponse resp{};
    while (!cli_chan->response_channel.Pop(resp)) {
      std::this_thread::yield();
    }
    check(resp.sequence_id == cmd.sequence_id, "Sequence mismatch");
    check(resp.status_code == 0, "Status non-zero");
    check(resp.execution_time_ns > 0.0, "Zero execution time");
  }

  server_thread.join();
  check(worker_done.load(), "Worker not done");
  std::cout << "fastpath_shm_test PASSED\n";
  return 0;
}
