#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>

#include "nemosymphtron/cuda_quant_worker.hpp"
#include "nemosymphtron/shv_hardware_guard.hpp"
#include "nemosymphtron/symphony_fastpath.hpp"
#include "nemosymphtron/transport.hpp"
#include "nemosymphtron/wire_protocol.hpp"

namespace {

std::atomic<bool> g_stop_requested{false};

void signal_handler(int) noexcept {
  g_stop_requested.store(true, std::memory_order_relaxed);
}

class ScopedFd {
public:
  explicit ScopedFd(int fd = -1) : fd_(fd) {}
  ~ScopedFd() {
    if (fd_ >= 0) ::close(fd_);
  }
  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;
  int get() const noexcept { return fd_; }
private:
  int fd_;
};

class ScopedWorkerJoin {
public:
  ScopedWorkerJoin(std::atomic<bool>& stop_flag, std::thread& worker) noexcept
      : stop_flag_(stop_flag), worker_(worker) {}
  ~ScopedWorkerJoin() {
    stop_flag_.store(true, std::memory_order_relaxed);
    if (worker_.joinable()) {
      worker_.join();
    }
  }
  ScopedWorkerJoin(const ScopedWorkerJoin&) = delete;
  ScopedWorkerJoin& operator=(const ScopedWorkerJoin&) = delete;
private:
  std::atomic<bool>& stop_flag_;
  std::thread& worker_;
};

} // namespace

int main(int argc, char** argv) {
  std::string shm_name = "/nemosymphtron_fastpath";
  std::string socket_path;
  std::string arch = "spark";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--shm-name" && i + 1 < argc) {
      shm_name = argv[++i];
    } else if (arg == "--socket" && i + 1 < argc) {
      socket_path = argv[++i];
    } else if (arg == "--arch" && i + 1 < argc) {
      arch = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: nemosymphtron-engine [--shm-name /name] [--socket /abs/path/to/sock] [--arch spark|rubin]\n";
      return 0;
    }
  }

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  std::signal(SIGPIPE, SIG_IGN);

  const bool rubin_mode = (arch == "rubin" || arch == "vera-rubin");
  try {
    (void)nemosymphtron::shv::SHVEnforcer::EnforceThreadAndMemoryLocks(
        0, rubin_mode ? 23 : 15, true);

    nemosymphtron::engine::DualPoolQuantEngine engine(rubin_mode);
    nemosymphtron::fastpath::PosixSharedMemorySegment shm_segment(shm_name, true);

    int listen_fd = -1;
    if (!socket_path.empty()) {
      listen_fd = nemosymphtron::listen_local_socket(socket_path);
    }

    // Worker Thread 1: Sovereign Symphony Fast-Path Poller (Pool 1)
    std::atomic<bool> worker_stop{false};
    std::thread fastpath_worker([&]() {
      (void)nemosymphtron::shv::SHVEnforcer::EnforceThreadAndMemoryLocks(
          0, rubin_mode ? 23 : 15, false);
      auto* channel = shm_segment.channel();
      nemosymphtron::fastpath::FastPathCommand cmd{};
      while (!worker_stop.load(std::memory_order_relaxed) &&
             !g_stop_requested.load(std::memory_order_relaxed)) {
        const auto now_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        channel->engine_heartbeat_ns.store(now_ns, std::memory_order_relaxed);

        if (channel->command_channel.Pop(cmd)) {
          const auto resp = engine.ExecuteFastPathCommand(cmd);
          while (!channel->response_channel.Push(resp) &&
                 !worker_stop.load(std::memory_order_relaxed) &&
                 !g_stop_requested.load(std::memory_order_relaxed)) {
            std::this_thread::yield();
          }
          channel->total_dispatched.fetch_add(1, std::memory_order_relaxed);
        } else {
#if defined(__aarch64__)
          asm volatile("yield" ::: "memory");
#endif
          std::this_thread::sleep_for(std::chrono::microseconds(25));
        }
      }
    });
    ScopedWorkerJoin worker_guard(worker_stop, fastpath_worker);

    // Main Thread / Worker 2: Dynamic Agent IPC Server (Pool 2)
    (void)nemosymphtron::shv::SHVEnforcer::EnforceThreadAndMemoryLocks(
        rubin_mode ? 24 : 16, rubin_mode ? 31 : 23, false);

    while (!g_stop_requested.load(std::memory_order_relaxed)) {
      if (listen_fd >= 0) {
        pollfd pfd{listen_fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, 50);
        if (ready > 0 && (pfd.revents & POLLIN)) {
          try {
            ScopedFd client_fd(nemosymphtron::accept_local_socket(listen_fd));
            std::string wire_in;
            if (nemosymphtron::ipc::recv_ipc_frame(client_fd.get(), wire_in)) {
              const auto req = nemosymphtron::ipc::deserialize_request(wire_in);
              const auto resp = engine.ExecuteAgentIPC(req);
              const auto wire_out = nemosymphtron::ipc::serialize_response(resp);
              (void)nemosymphtron::ipc::send_ipc_frame(client_fd.get(), wire_out);
            }
          } catch (...) {
            // Isolate per-connection protocol errors so daemon remains live
          }
        }
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    }

    worker_stop.store(true, std::memory_order_relaxed);
    if (fastpath_worker.joinable()) {
      fastpath_worker.join();
    }
    if (listen_fd >= 0) {
      ::close(listen_fd);
      nemosymphtron::remove_local_socket(socket_path);
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "[nemosymphtron-engine] fatal: " << ex.what() << "\n";
    if (!socket_path.empty()) {
      nemosymphtron::remove_local_socket(socket_path);
    }
    return 1;
  }
}
