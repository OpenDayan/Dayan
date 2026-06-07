#ifndef DDR4_H
#define DDR4_H

#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <svdpi.h>
#include <sys/mman.h>

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <deque>
#include <memory>
#include <queue>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "configuration.h"
#include "dram_system.h"
#include "sim.h"
#include "utils/axi_utils.h"
#include "utils/logger.h"

namespace DDR4 {
void fuse_write(uint64_t addr, std::span<const uint8_t> data);
std::span<const uint8_t> fuse_read(uint64_t addr, size_t size);
} // namespace DDR4

template <size_t dataBits> class DDR4DPI {

private:
  std::shared_ptr<spdlog::logger> logger = nullptr;
  static constexpr size_t dataBytes = dataBits / 8;
  static constexpr size_t mem_cap = 16ULL * 1024 * 1024 * 1024;

  struct RWQueue {
    struct AWInfo {
      uint32_t id;
      uint64_t addr;
      uint32_t len;
      uint32_t size;
      uint32_t burst;
      uint64_t b_seq;
    };

    // B channel: per-ID ordering
    struct BReq {
      uint32_t axi_id;
      uint32_t beats_left;
    };
    uint64_t b_burst_seq = 0;
    uint64_t b_beat_seq = 0;
    std::unordered_map<uint64_t, uint64_t> b_beat_req; // beat_seq -> burst_seq
    std::unordered_map<uint64_t, BReq> b_req;          // burst_seq -> request
    std::unordered_map<uint32_t, std::deque<uint64_t>>
        b_idq;                           // axi_id -> ordered burst seqs
    std::unordered_set<uint64_t> b_done; // completed burst seqs
    std::queue<uint32_t> b_out;          // output: axi_id

    // R channel: per-ID ordering
    struct RReq {
      uint64_t addr;
      uint32_t axi_id;
      bool last;
    };
    uint64_t r_seq = 0;
    std::unordered_map<uint64_t, RReq> r_req; // beat seq -> request
    std::unordered_map<uint32_t, std::deque<uint64_t>>
        r_idq;                           // axi_id -> ordered beat seqs
    std::unordered_set<uint64_t> r_done; // completed beat seqs
    struct ROut {
      uint64_t addr;
      uint32_t axi_id;
      bool last;
    };
    std::queue<ROut> r_out;
  };
  RWQueue queue;

  uint64_t ar_addr = 0;
  uint32_t ar_id = 0;
  uint32_t ar_len = 0;
  uint32_t ar_size = 0;
  uint32_t ar_burst = 0;
  uint32_t ar_beat_idx = 0;
  uint32_t ar_beats_left = 0;
  bool ar_last_ready = false;

  typename RWQueue::AWInfo aw_info = {};
  uint32_t aw_beat_idx = 0;
  uint32_t aw_beats_left = 0;
  bool aw_last_ready = false;

  bool w_buffer_valid = false;
  AXIUtils::Data<dataBits> wdata = {};
  AXIUtils::Strb<dataBytes> wstrb = {};
  bool wlast = false;
  bool w_last_ready = false;

  bool b_last_valid = false;
  bool r_last_valid = false;

  static uint64_t burst_addr(uint64_t start_addr, uint32_t beat_idx,
                             uint32_t size, uint32_t burst, uint32_t len) {
    uint64_t stride = 1ULL << size;
    switch (burst) {
    case 0: // FIXED
      return start_addr;
    case 1: // INCR
      return start_addr + beat_idx * stride;
    case 2: { // WRAP
      uint64_t boundary = (uint64_t)(len + 1) * stride;
      uint64_t lower = start_addr & ~(boundary - 1);
      return lower + ((start_addr - lower + beat_idx * stride) % boundary);
    }
    default:
      return start_addr;
    }
  }

  void release_b() {
    for (auto &[axi_id, q] : queue.b_idq) {
      while (!q.empty() && queue.b_done.count(q.front())) {
        uint64_t seq = q.front();
        q.pop_front();
        auto req = queue.b_req[seq];
        queue.b_out.push(req.axi_id);
        queue.b_req.erase(seq);
        queue.b_done.erase(seq);
      }
    }
  }

  void release_r() {
    for (auto &[axi_id, q] : queue.r_idq) {
      while (!q.empty() && queue.r_done.count(q.front())) {
        uint64_t seq = q.front();
        q.pop_front();
        auto req = queue.r_req[seq];
        queue.r_out.push({req.addr, req.axi_id, req.last});
        queue.r_req.erase(seq);
        queue.r_done.erase(seq);
      }
    }
  }

  class Mem {
    uint8_t *data_ptr;

  public:
    std::span<uint8_t> data_span;
    Mem() {
      data_ptr = (uint8_t *)mmap(NULL, mem_cap, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      data_span = std::span<uint8_t>(data_ptr, mem_cap);
    }
    ~Mem() { munmap(data_ptr, mem_cap); }
    std::span<uint8_t> getData(size_t addr, size_t size) {
      return data_span.subspan(addr, size);
    }
    std::span<const uint8_t> read(size_t addr, size_t size) const {
      return data_span.subspan(addr, size);
    }
    void write(size_t addr, std::span<const uint8_t> data) {
      std::copy_n(data.begin(), data.size(),
                  data_span.subspan(addr, data.size()).begin());
    }
  };

  std::unique_ptr<Mem> mem = nullptr;
  std::unique_ptr<dramsim3::JedecDRAMSystem> sys = nullptr;

  std::function<void(uint64_t, uint64_t)> make_read_callback() {
    return [this](uint64_t addr, uint64_t seq) {
      logger->debug("read_callback({:#x},{})", addr, seq);
      queue.r_done.insert(seq);
      release_r();
    };
  }
  std::function<void(uint64_t, uint64_t)> make_write_callback() {
    return [this](uint64_t addr, uint64_t seq) {
      logger->debug("write_callback({:#x},{})", addr, seq);
      auto beat_it = queue.b_beat_req.find(seq);
      assert(beat_it != queue.b_beat_req.end());

      uint64_t burst_seq = beat_it->second;
      queue.b_beat_req.erase(beat_it);

      auto burst_it = queue.b_req.find(burst_seq);
      assert(burst_it != queue.b_req.end());
      assert(burst_it->second.beats_left > 0);

      burst_it->second.beats_left -= 1;
      if (burst_it->second.beats_left == 0) {
        queue.b_done.insert(burst_seq);
      }
      release_b();
    };
  }

  void check_burst(uint64_t addr, uint32_t len, uint32_t size, uint32_t burst,
                   bool is_write) {
    if (burst != 1 && len != 0) {
      logger->warn("{} burst is not INCR, which may cause issues.",
                   is_write ? "aw" : "ar");
    }

    if ((1ULL << size) != dataBytes && len != 0) {
      logger->warn("Narrow {} burst may cause issues.",
                   is_write ? "write" : "read");
    }

    uint64_t beats = static_cast<uint64_t>(len) + 1;
    uint64_t begin_4k = addr & ~uint64_t(4095);
    uint64_t end_4k = (addr + dataBytes * beats - 1) & ~uint64_t(4095);
    if (end_4k != begin_4k) {
      logger->warn("{} burst crosses 4K boundary, which may cause issues.",
                   is_write ? "aw" : "ar");
    }
  }

  void writeMemory(uint64_t addr, uint32_t size, const uint8_t *src,
                   const AXIUtils::Strb<dataBytes> &strb) {
    uint64_t write_bytes = 1ULL << size;
    uint64_t write_offset = addr % dataBytes;
    uint64_t aligned_addr = addr - write_offset;

    assert(aligned_addr + dataBytes <= mem_cap);

    auto strb_bits = AXIUtils::arrayToBitset<dataBytes / 8>(strb);
    for (uint64_t i = 0; i < write_bytes; ++i) {
      uint64_t byte = write_offset + i;
      if (byte >= dataBytes) {
        break;
      }
      if (strb_bits[byte]) {
        mem->data_span[aligned_addr + byte] = src[byte];
      }
    }
  }

  void addReadBeat(uint64_t addr, uint32_t id, bool last) {
    uint64_t seq = queue.r_seq++;
    sys->AddTransaction(addr, false, seq);
    queue.r_req[seq] = {addr, id, last};
    queue.r_idq[id].push_back(seq);
  }

  void addWriteBeat(uint64_t addr, uint64_t burst_seq) {
    uint64_t seq = queue.b_beat_seq++;
    sys->AddTransaction(addr, true, seq);
    queue.b_beat_req[seq] = burst_seq;
  }

public:
  DDR4DPI() { logger = spdlog::stderr_color_mt("DDR4"); }

  void init(dramsim3::Config &config) {
    if (!config.IsDDR4()) {
      throw std::invalid_argument("Config is not for DDR4");
    }
    sys = std::make_unique<dramsim3::JedecDRAMSystem>(
        config, ".", make_read_callback(), make_write_callback());
    mem = std::make_unique<Mem>();

    logger->set_level(spdlog::level::info);
    logger->set_formatter(std::move(make_sim_formatter()));
    logger->info("DDR4 initialized");
  }

  void tick() { sys->ClockTick(); }

  void write(uint64_t addr, std::span<const uint8_t> data) {
    mem->write(addr, data);
  }

  std::span<const uint8_t> read(uint64_t addr, size_t size) {
    return mem->read(addr, size);
  }

  void tickAW(svBit valid, svBit *ready, int32_t id, int64_t addr, int32_t len,
              int32_t size, int32_t burst) {
    bool aw_fire = valid && aw_last_ready;

    if (aw_fire) {
      uint32_t id_val = uint32_t(id);
      uint64_t addr_val = uint64_t(addr);
      uint32_t len_val = uint32_t(len);
      uint32_t size_val = uint32_t(size);
      uint32_t burst_val = uint32_t(burst);
      uint64_t b_seq = queue.b_burst_seq++;

      check_burst(addr_val, len_val, size_val, burst_val, true);

      aw_info = {id_val, addr_val, len_val, size_val, burst_val, b_seq};
      aw_beat_idx = 0;
      aw_beats_left = len_val + 1;
      queue.b_req[b_seq] = {id_val, len_val + 1};
      queue.b_idq[id_val].push_back(b_seq);

      logger->debug("AW {:d} {:#x} {:d} {:d} {:d}", id_val, addr_val, size_val,
                    len_val, burst_val);
    }

    *ready = aw_beats_left == 0;
    aw_last_ready = *ready;
  }

  // tickW should be called after tickAW
  void tickW(svBit valid, svBit *ready, const int8_t *data, const int8_t *strb,
             svBit last) {
    bool w_fire = valid && w_last_ready;

    if (w_fire) {
      std::copy_n((const uint8_t *)data, dataBytes, wdata.begin());
      std::copy_n((const uint8_t *)strb, dataBytes / 8, wstrb.begin());
      wlast = last;
      w_buffer_valid = true;

      logger->debug("W beat");
      logger->trace("{} {}", spdlog::to_hex(wdata.begin(), wdata.end()),
                    spdlog::to_hex(wstrb.begin(), wstrb.end()));
    }

    if (w_buffer_valid && aw_beats_left != 0) {
      uint64_t beat_addr_val = burst_addr(aw_info.addr, aw_beat_idx, aw_info.size,
                                          aw_info.burst, aw_info.len);

      if (sys->WillAcceptTransaction(beat_addr_val, true)) {
        bool expected_last = aw_beats_left == 1;
        if (bool(wlast) != expected_last) {
          logger->warn("W last mismatch on id {:d}: got {}, expected {}",
                       aw_info.id, bool(wlast), expected_last);
        }

        writeMemory(beat_addr_val, aw_info.size, wdata.data(), wstrb);
        addWriteBeat(beat_addr_val, aw_info.b_seq);

        aw_beat_idx++;
        aw_beats_left--;
        w_buffer_valid = false;
      }
    }

    *ready = !w_buffer_valid;
    w_last_ready = *ready;
  }

  void tickB(svBit *valid, svBit ready, int32_t *id, int32_t *resp) {
    if (b_last_valid && ready && !queue.b_out.empty()) {
      logger->debug("B {:d}", queue.b_out.front());
      queue.b_out.pop();
    }

    *valid = !queue.b_out.empty();
    b_last_valid = *valid;

    if (*valid) {
      *id = queue.b_out.front();
      *resp = 0;
    }
  }

  void tickAR(svBit valid, svBit *ready, int32_t id, int64_t addr, int32_t len,
              int32_t size, int32_t burst) {
    bool ar_fire = valid && ar_last_ready;

    if (ar_fire) {
      ar_id = uint32_t(id);
      ar_addr = uint64_t(addr);
      ar_len = uint32_t(len);
      ar_size = uint32_t(size);
      ar_burst = uint32_t(burst);
      ar_beat_idx = 0;
      ar_beats_left = ar_len + 1;

      check_burst(ar_addr, ar_len, ar_size, ar_burst, false);

      logger->debug("AR {:d} {:#x} {:d} {:d} {:d}", ar_id, ar_addr, ar_size,
                    ar_len, ar_burst);
    }

    if (ar_beats_left != 0) {
      uint64_t beat_addr_val =
          burst_addr(ar_addr, ar_beat_idx, ar_size, ar_burst, ar_len);
      if (sys->WillAcceptTransaction(beat_addr_val, false)) {
        addReadBeat(beat_addr_val, ar_id, ar_beats_left == 1);
        ar_beat_idx++;
        ar_beats_left--;
      }
    }

    *ready = ar_beats_left == 0;
    ar_last_ready = *ready;
  }

  void tickR(svBit *valid, svBit ready, int32_t *id, int8_t *data,
             int32_t *resp, svBit *last) {
    if (r_last_valid && ready && !queue.r_out.empty()) {
      queue.r_out.pop();
    }

    *valid = !queue.r_out.empty();
    r_last_valid = *valid;

    if (*valid) {
      auto &front = queue.r_out.front();
      auto data_span =
          mem->read(front.addr & ~(dataBytes - 1ULL), dataBytes);

      *id = front.axi_id;
      std::copy_n(data_span.begin(), data_span.size(), (uint8_t *)data);
      *resp = 0;
      *last = front.last;

      logger->debug("R {:d} last {}", front.axi_id, front.last);
      logger->trace("{}", spdlog::to_hex(data_span.begin(), data_span.end()));
    }
  }
};

#define EXPORT_DDR4_AW(dataBits)                                               \
  extern "C" void AXI4SlaveDPIAW_##dataBits##_tick(                            \
      svBit valid, svBit *ready, int32_t id, int64_t addr, int32_t len,        \
      int32_t size, int32_t burst) {                                           \
    DDR4DPI##dataBits.tickAW(valid, ready, id, addr, len, size, burst);        \
  }

#define EXPORT_DDR4_W(dataBits)                                                \
  extern "C" void AXI4SlaveDPIW_##dataBits##_tick(                             \
      svBit valid, svBit *ready, const int8_t *data, const int8_t *strb,       \
      svBit last) {                                                            \
    DDR4DPI##dataBits.tickW(valid, ready, data, strb, last);                   \
  }

#define EXPORT_DDR4_B(dataBits)                                                \
  extern "C" void AXI4SlaveDPIB_##dataBits##_tick(                             \
      svBit *valid, svBit ready, int32_t *id, int32_t *resp) {                 \
    DDR4DPI##dataBits.tickB(valid, ready, id, resp);                           \
  }

#define EXPORT_DDR4_AR(dataBits)                                               \
  extern "C" void AXI4SlaveDPIAR_##dataBits##_tick(                            \
      svBit valid, svBit *ready, int32_t id, int64_t addr, int32_t len,        \
      int32_t size, int32_t burst) {                                           \
    DDR4DPI##dataBits.tickAR(valid, ready, id, addr, len, size, burst);        \
  }

#define EXPORT_DDR4_R(dataBits)                                                \
  extern "C" void AXI4SlaveDPIR_##dataBits##_tick(                             \
      svBit *valid, svBit ready, int32_t *id, int8_t *data, int32_t *resp,     \
      svBit *last) {                                                           \
    DDR4DPI##dataBits.tickR(valid, ready, id, data, resp, last);               \
  }

#define EXPORT_DDR4_DPI_ALL(dataBits)                                          \
  EXPORT_DDR4_AW(dataBits)                                                     \
  EXPORT_DDR4_W(dataBits)                                                      \
  EXPORT_DDR4_B(dataBits)                                                      \
  EXPORT_DDR4_AR(dataBits)                                                     \
  EXPORT_DDR4_R(dataBits)

extern DDR4DPI<512> DDR4DPI512;

#define DDR4Mod DDR4DPI512

#endif
