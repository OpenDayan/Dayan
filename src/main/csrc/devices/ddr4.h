#ifndef DDR4_H
#define DDR4_H

#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <svdpi.h>
#include <sys/mman.h>

#include <bitset>
#include <cassert>
#include <cstdint>
#include <deque>
#include <memory>
#include <queue>
#include <span>
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
    };
    std::queue<AWInfo> aw;

    // B channel: per-ID ordering
    uint64_t b_seq = 0;
    std::unordered_map<uint64_t, uint32_t> b_req; // seq -> axi_id
    std::unordered_map<uint32_t, std::deque<uint64_t>>
        b_idq;                           // axi_id -> ordered seqs
    std::unordered_set<uint64_t> b_done; // completed seqs
    std::queue<uint32_t> b_out;          // output: axi_id

    // R channel: per-ID ordering
    struct RReq {
      uint64_t addr;
      uint32_t axi_id;
      uint32_t len;
      uint32_t size;
      uint32_t burst;
    };
    uint64_t r_seq = 0;
    std::unordered_map<uint64_t, RReq> r_req; // seq -> request
    std::unordered_map<uint32_t, std::deque<uint64_t>>
        r_idq;                           // axi_id -> ordered seqs
    std::unordered_set<uint64_t> r_done; // completed seqs
    struct ROut {
      uint64_t addr;
      uint32_t axi_id;
      uint32_t len;
      uint32_t size;
      uint32_t burst;
    };
    std::queue<ROut> r_out;
  };
  RWQueue queue;

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
        queue.b_out.push(queue.b_req[seq]);
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
        auto &req = queue.r_req[seq];
        queue.r_out.push({req.addr, req.axi_id, req.len, req.size, req.burst});
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
      queue.b_done.insert(seq);
      release_b();
    };
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
    static bool last_ready = false;

    if (valid && last_ready) {
      uint32_t id_val = uint32_t(id);
      uint64_t addr_val = uint64_t(addr);
      uint32_t len_val = uint32_t(len);
      uint32_t size_val = uint32_t(size);
      uint32_t burst_val = uint32_t(burst);

      queue.aw.push({id_val, addr_val, len_val, size_val, burst_val});

      logger->debug("AW {:d} {:#x} {:d} {:d} {:d}", id_val, addr_val, size_val,
                    len_val, burst_val);
    }

    *ready = 1;
    last_ready = *ready;
  }

  // tickW should be called after tickAW
  void tickW(svBit valid, svBit *ready, const int8_t *data, const int8_t *strb,
             svBit last) {
    static bool buffer_empty = true;
    static AXIUtils::Data<dataBits> wdata;
    static AXIUtils::Strb<dataBytes> wstrb;
    static bool wlast = false;

    static bool burst_active = false;
    static typename RWQueue::AWInfo aw_info;
    static uint32_t beat_idx = 0;

    // Start new burst if AW available
    if (!burst_active && !queue.aw.empty()) {
      aw_info = queue.aw.front();
      burst_active = true;
      beat_idx = 0;
    }

    if (valid && buffer_empty) {
      std::copy_n((const uint8_t *)data, dataBytes, wdata.begin());
      std::copy_n((const uint8_t *)strb, dataBytes / 8, wstrb.begin());
      wlast = last;
      buffer_empty = false;

      logger->debug("W beat");
      logger->trace("{} {}", spdlog::to_hex(wdata.begin(), wdata.end()),
                    spdlog::to_hex(wstrb.begin(), wstrb.end()));
    }

    if (!buffer_empty && burst_active) {
      uint64_t beat_addr_val = burst_addr(aw_info.addr, beat_idx, aw_info.size,
                                          aw_info.burst, aw_info.len);

      if (!wlast) {
        // Non-last beat: write to memory immediately
        auto addr_offset = beat_addr_val % dataBytes;
        auto wdata_it = wdata.begin() + addr_offset;
        std::bitset<dataBytes> wstrb_bits =
            AXIUtils::arrayToBitset<dataBytes / 8>(wstrb) >> addr_offset;
        auto mem_it = mem->getData(beat_addr_val, 1ULL << aw_info.size).begin();
        for (size_t i = 0; i < (1ULL << aw_info.size); i++) {
          if (wstrb_bits[i]) {
            *mem_it = *wdata_it;
          }
          wdata_it++;
          mem_it++;
        }

        beat_idx++;
        buffer_empty = true;
      } else {
        // Last beat: need DRAM to accept
        if (sys->WillAcceptTransaction(aw_info.addr, true)) {
          auto addr_offset = beat_addr_val % dataBytes;
          auto wdata_it = wdata.begin() + addr_offset;
          std::bitset<dataBytes> wstrb_bits =
              AXIUtils::arrayToBitset<dataBytes / 8>(wstrb) >> addr_offset;
          auto mem_it = mem->getData(beat_addr_val, 1ULL << aw_info.size).begin();
          for (size_t i = 0; i < (1ULL << aw_info.size); i++) {
            if (wstrb_bits[i]) {
              *mem_it = *wdata_it;
            }
            wdata_it++;
            mem_it++;
          }

          uint64_t seq = queue.b_seq++;
          sys->AddTransaction(aw_info.addr, true, seq);
          queue.b_req[seq] = aw_info.id;
          queue.b_idq[aw_info.id].push_back(seq);

          queue.aw.pop();
          burst_active = false;
          buffer_empty = true;
        }
      }
    }

    *ready = buffer_empty;
  }

  void tickB(svBit *valid, svBit ready, int32_t *id, int32_t *resp) {
    static bool last_valid = false;

    if (last_valid && ready) {
      logger->debug("B {:d}", queue.b_out.front());
      queue.b_out.pop();
    }

    *valid = !queue.b_out.empty();
    last_valid = *valid;

    if (*valid) {
      *id = queue.b_out.front();
      *resp = 0;
    }
  }

  void tickAR(svBit valid, svBit *ready, int32_t id, int64_t addr, int32_t len,
              int32_t size, int32_t burst) {
    static bool buffer_empty = true;
    static uint32_t id_val = 0;
    static uint64_t addr_val = 0;
    static uint32_t len_val = 0;
    static uint32_t size_val = 0;
    static uint32_t burst_val = 0;

    if (valid && buffer_empty) {
      id_val = uint32_t(id);
      addr_val = uint64_t(addr);
      len_val = uint32_t(len);
      size_val = uint32_t(size);
      burst_val = uint32_t(burst);

      buffer_empty = false;

      logger->debug("AR {:d} {:#x} {:d} {:d} {:d}", id_val, addr_val, size_val,
                    len_val, burst_val);
    }

    if (!buffer_empty && sys->WillAcceptTransaction(addr_val, false)) {
      uint64_t seq = queue.r_seq++;
      sys->AddTransaction(addr_val, false, seq);
      queue.r_req[seq] = {addr_val, id_val, len_val, size_val, burst_val};
      queue.r_idq[id_val].push_back(seq);
      buffer_empty = true;
    }

    *ready = buffer_empty;
  }

  void tickR(svBit *valid, svBit ready, int32_t *id, int8_t *data,
             int32_t *resp, svBit *last) {
    static bool last_valid = false;
    static uint32_t beat_idx = 0;

    if (last_valid && ready) {
      beat_idx++;
      if (beat_idx > queue.r_out.front().len) {
        queue.r_out.pop();
        beat_idx = 0;
      }
    }

    *valid = !queue.r_out.empty();
    last_valid = *valid;

    if (*valid) {
      auto &front = queue.r_out.front();
      uint64_t beat_addr_val =
          burst_addr(front.addr, beat_idx, front.size, front.burst, front.len);
      auto data_span =
          mem->read(beat_addr_val & ~(dataBytes - 1ULL), dataBytes);

      *id = front.axi_id;
      std::copy_n(data_span.begin(), data_span.size(), (uint8_t *)data);
      *resp = 0;
      *last = (beat_idx == front.len);

      logger->debug("R {:d} beat {:d}/{:d}", front.axi_id, beat_idx, front.len);
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
