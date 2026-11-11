#pragma once

#include <atomic>
#include <cerrno>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nemosymphtron::fastpath {

// Cache-line isolation to eliminate false sharing on Grace/Vera ARM64 Neoverse cores
constexpr std::size_t kCacheLineSize = 64;

enum class TaskType : std::uint32_t {
  MonteCarloVaR         = 1,
  CholeskyDecomp        = 2,
  PortfolioOptimization = 3,
  FftOptionPricing      = 4,
  NcclAllReduce         = 5
};

struct alignas(kCacheLineSize) FastPathCommand {
  std::uint64_t sequence_id{0};
  TaskType task{TaskType::MonteCarloVaR};
  double spot_price{100.0};
  double volatility{0.20};
  double drift{0.05};
  double strike_or_target{100.0};
  std::uint32_t paths{100000};
  std::uint32_t steps{252};
  std::uint32_t matrix_dim{4};
  std::uint32_t world_size{1};
};

struct alignas(kCacheLineSize) FastPathResponse {
  std::uint64_t sequence_id{0};
  std::uint32_t status_code{0};
  std::uint32_t stream_priority_rank{1};
  double var_99{0.0};
  double var_95{0.0};
  double cvar_99{0.0};
  double expected_terminal_price{0.0};
  double secondary_metric{0.0};
  double execution_time_ns{0.0};
};

template <typename T, std::size_t Capacity = 256>
requires std::is_trivially_copyable_v<T>
class LockFreeRingBuffer {
public:
  alignas(kCacheLineSize) std::atomic<std::size_t> head{0};
  alignas(kCacheLineSize) std::atomic<std::size_t> tail{0};
  T buffer[Capacity]{};

  bool Push(const T& item) noexcept {
    const std::size_t current_tail = tail.load(std::memory_order_relaxed);
    const std::size_t next_tail = (current_tail + 1) % Capacity;
    if (next_tail == head.load(std::memory_order_acquire)) {
      return false; // Queue saturated
    }
    buffer[current_tail] = item;
    tail.store(next_tail, std::memory_order_release);
    return true;
  }

  bool Pop(T& item) noexcept {
    const std::size_t current_head = head.load(std::memory_order_relaxed);
    if (current_head == tail.load(std::memory_order_acquire)) {
      return false; // Queue empty
    }
    item = buffer[current_head];
    head.store((current_head + 1) % Capacity, std::memory_order_release);
    return true;
  }
};

struct SovereignSharedChannel {
  alignas(kCacheLineSize) std::atomic<std::uint64_t> magic{0x4E53545F53484D31ULL}; // "NST_SHM1"
  alignas(kCacheLineSize) std::atomic<std::uint64_t> engine_heartbeat_ns{0};
  alignas(kCacheLineSize) std::atomic<std::uint64_t> total_dispatched{0};
  LockFreeRingBuffer<FastPathCommand, 256> command_channel{};
  LockFreeRingBuffer<FastPathResponse, 256> response_channel{};
};

class PosixSharedMemorySegment {
public:
  PosixSharedMemorySegment(std::string shm_name, bool create_owner)
      : shm_name_(std::move(shm_name)), owner_(create_owner) {
    if (shm_name_.empty() || shm_name_.front() != '/') {
      shm_name_ = "/" + shm_name_;
    }
    if (owner_) {
      ::shm_unlink(shm_name_.c_str());
    }
    const int flags = owner_ ? (O_CREAT | O_RDWR) : O_RDWR;
    const int fd = ::shm_open(shm_name_.c_str(), flags, 0600);
    if (fd < 0) {
      throw std::runtime_error("shm_open failed for " + shm_name_ + " (errno " + std::to_string(errno) + ")");
    }
    if (owner_) {
      if (::ftruncate(fd, static_cast<off_t>(sizeof(SovereignSharedChannel))) != 0) {
        ::close(fd);
        ::shm_unlink(shm_name_.c_str());
        throw std::runtime_error("ftruncate failed for POSIX SHM " + shm_name_);
      }
    }
    void* mapped = ::mmap(nullptr, sizeof(SovereignSharedChannel),
                          PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) {
      if (owner_) ::shm_unlink(shm_name_.c_str());
      throw std::runtime_error("mmap failed for POSIX SHM " + shm_name_);
    }
    if (owner_) {
      channel_ = new (mapped) SovereignSharedChannel();
    } else {
      channel_ = static_cast<SovereignSharedChannel*>(mapped);
    }
  }

  ~PosixSharedMemorySegment() {
    if (channel_) {
      ::munmap(channel_, sizeof(SovereignSharedChannel));
      channel_ = nullptr;
    }
    if (owner_) {
      ::shm_unlink(shm_name_.c_str());
    }
  }

  PosixSharedMemorySegment(const PosixSharedMemorySegment&) = delete;
  PosixSharedMemorySegment& operator=(const PosixSharedMemorySegment&) = delete;

  SovereignSharedChannel* channel() const noexcept { return channel_; }
  const std::string& name() const noexcept { return shm_name_; }

private:
  std::string shm_name_;
  bool owner_{false};
  SovereignSharedChannel* channel_{nullptr};
};

} // namespace nemosymphtron::fastpath
