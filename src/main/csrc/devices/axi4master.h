#ifndef AXI4MASTER_H
#define AXI4MASTER_H

#include <array>
#include <iostream>
#include <ostream>
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <svdpi.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "sim.h"
#include "utils/axi_utils.h"
#include "utils/logger.h"

template <size_t dataBits> class AXI4MasterDPI {

private:
  std::shared_ptr<spdlog::logger> logger = nullptr;

  class RWQueue {
  public:
    std::queue<std::pair<uint64_t, uint32_t>> aw;
    std::queue<std::pair<uint64_t, uint32_t>> ar;
    std::queue<std::pair<std::unique_ptr<AXIUtils::Data<dataBits>>,
                         std::unique_ptr<AXIUtils::Strb<dataBits / 8>>>>
        w;
    std::queue<uint32_t> b;
    std::queue<std::unique_ptr<AXIUtils::Data<dataBits>>> r;
  };
  RWQueue queue;

public:
  AXI4MasterDPI() {
    logger = spdlog::stderr_color_mt("AXIM" + std::to_string(dataBits));
  }

  void init() {
    logger->set_level(spdlog::level::warn);
    logger->set_formatter(std::move(make_sim_formatter()));
    logger->info("initialized");
  }

  ssize_t write(uint64_t addr, std::span<const uint8_t> data) {
    const size_t dataBytes = dataBits / 8;

    const size_t addr_offset = addr % dataBytes;
    const size_t start_addr_aligned = addr - addr_offset;
    const size_t end_addr = addr + data.size();
    const size_t end_addr_aligned =
        ((end_addr + dataBytes - 1) / dataBytes) * dataBytes;
    const size_t beats_bytes = end_addr_aligned - start_addr_aligned;
    const size_t beats = beats_bytes / dataBytes;

    logger->info("write {} bytes to address 0x{:x}", data.size(), addr);

    if (beats_bytes == dataBytes && std::has_single_bit(data.size()) &&
        addr % data.size() == 0) {
      queue.aw.push({addr, std::bit_width(data.size()) - 1});
      auto w_data_ptr = std::make_unique<AXIUtils::Data<dataBits>>();
      std::copy_n(data.begin(), data.size(), w_data_ptr->begin() + addr_offset);
      auto strb_ptr = std::make_unique<AXIUtils::Strb<dataBits / 8>>();
      for (size_t i = addr_offset; i < addr_offset + data.size(); ++i) {
        size_t strb_byte_idx = i / 8;
        size_t strb_bit_idx = i % 8;
        strb_ptr->at(strb_byte_idx) |= (1U << strb_bit_idx);
      }
      queue.w.push({std::move(w_data_ptr), std::move(strb_ptr)});

      size_t resp_count = 0;
      while (resp_count < 1) {
        while (queue.b.empty()) {
          if (is_shutting_down) {
            return -1;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        queue.b.pop();
        resp_count++;
      }

      return data.size();
    }

    for (size_t a = start_addr_aligned; a < end_addr_aligned; a += dataBytes) {
      queue.aw.push({a, 6});
    }

    size_t written_bytes = 0;
    std::vector<uint8_t> buf(beats_bytes);
    std::span<uint8_t> span_buf(buf);

    // copy data to aligned buffer
    std::copy_n(data.begin(), data.size(), buf.begin() + addr_offset);
    std::vector<std::unique_ptr<AXIUtils::Strb<dataBits / 8>>> strb(beats);
    for (size_t i = 0; i < beats; ++i) {
      auto strb_ptr = std::make_unique<AXIUtils::Strb<dataBits / 8>>();
      strb_ptr->fill(0xff);
      strb[i] = std::move(strb_ptr);
    }

    for (size_t i = 0; i < addr_offset; ++i) {
      size_t strb_byte_idx = i / 8;
      size_t strb_bit_idx = i % 8;
      strb[0]->at(strb_byte_idx) &= ~(1U << strb_bit_idx);
    }
    for (size_t i = end_addr % dataBytes; i < dataBytes; i++) {
      size_t strb_byte_idx = i / 8;
      size_t strb_bit_idx = i % 8;
      strb[beats - 1]->at(strb_byte_idx) &= ~(1U << strb_bit_idx);
    }

    while (written_bytes < beats_bytes) {
      auto sub_data = std::make_unique<AXIUtils::Data<dataBits>>();
      std::copy_n(span_buf.subspan(written_bytes, dataBytes).begin(), dataBytes,
                  sub_data->begin());
      queue.w.push(
          {std::move(sub_data), std::move(strb[written_bytes / dataBytes])});

      written_bytes += dataBytes;
    }

    size_t resp_count = 0;
    while (resp_count < beats) {
      while (queue.b.empty()) {
        if (is_shutting_down) {
          return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      queue.b.pop();
      resp_count++;
    }

    return data.size();
  }

  ssize_t write64(uint64_t addr, uint64_t data) {
    std::array<uint8_t, 8> bytes;
    bytes[0] = data & 0xff;
    bytes[1] = (data >> 8) & 0xff;
    bytes[2] = (data >> 16) & 0xff;
    bytes[3] = (data >> 24) & 0xff;
    bytes[4] = (data >> 32) & 0xff;
    bytes[5] = (data >> 40) & 0xff;
    bytes[6] = (data >> 48) & 0xff;
    bytes[7] = (data >> 56) & 0xff;
    return write(addr, bytes);
  }

  ssize_t write32(uint64_t addr, uint32_t data) {
    std::array<uint8_t, 4> bytes;
    bytes[0] = data & 0xff;
    bytes[1] = (data >> 8) & 0xff;
    bytes[2] = (data >> 16) & 0xff;
    bytes[3] = (data >> 24) & 0xff;
    return write(addr, bytes);
  }

  std::unique_ptr<std::vector<uint8_t>> read(uint64_t addr, size_t size) {
    const size_t dataBytes = dataBits / 8;

    const size_t addr_offset = addr % dataBytes;
    const size_t start_addr_aligned = addr - addr_offset;
    const size_t end_addr = addr + size;
    const size_t end_addr_aligned =
        ((end_addr + dataBytes - 1) / dataBytes) * dataBytes;
    const size_t beats_bytes = end_addr_aligned - start_addr_aligned;

    logger->info("read {} bytes from address 0x{:x}", size, addr);

    if (beats_bytes == dataBytes && std::has_single_bit(size) &&
        addr % size == 0) {
      queue.ar.push({addr, std::bit_width(size) - 1});
      while (queue.r.empty()) {
        if (is_shutting_down) {
          return nullptr;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      auto r_data_ptr = std::move(queue.r.front());
      queue.r.pop();
      auto ptr = std::make_unique<std::vector<uint8_t>>(
          r_data_ptr->begin() + addr_offset,
          r_data_ptr->begin() + addr_offset + size);
      return ptr;
    }

    for (size_t a = start_addr_aligned; a < end_addr; a += dataBytes) {
      queue.ar.push({a, std::log2(dataBytes)});
    }
    size_t read_bytes = 0;
    auto buf = std::vector<uint8_t>(beats_bytes);
    while (read_bytes < beats_bytes) {
      while (queue.r.empty()) {
        if (is_shutting_down) {
          return nullptr;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      auto r_data_ptr = std::move(queue.r.front());
      queue.r.pop();
      buf.insert(buf.begin() + read_bytes, r_data_ptr->begin(),
                 r_data_ptr->end());

      read_bytes += dataBytes;
    }

    return std::make_unique<std::vector<uint8_t>>(
        buf.begin() + addr_offset, buf.begin() + addr_offset + size);
  }

  uint32_t read32(uint64_t addr) {
    uint32_t data;
    auto bytes = *read(addr, 4);
    data = bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
    return data;
  }

  uint64_t read64(uint64_t addr) {
    uint64_t data;
    auto bytes = *read(addr, 8);
    data = bytes[0] | ((uint64_t)bytes[1] << 8) | ((uint64_t)bytes[2] << 16) |
           ((uint64_t)bytes[3] << 24) | ((uint64_t)bytes[4] << 32) |
           ((uint64_t)bytes[5] << 40) | ((uint64_t)bytes[6] << 48) |
           ((uint64_t)bytes[7] << 56);
    return data;
  }

  // hw tick
  void tickAW(svBit *valid, svBit ready, int32_t *id, int64_t *addr,
              int32_t *len, int32_t *size, int32_t *burst) {
    static uint32_t id_counter = 0;

    static bool last_valid = false;
    if (last_valid && ready) {
      logger->debug("AW {:d} {:x} {:d}", id_counter, queue.aw.front().first,
                    queue.aw.front().second);
      queue.aw.pop();
      id_counter++;
    }
    *valid = !queue.aw.empty();
    last_valid = *valid;

    if (*valid) {
      *(uint32_t *)id = id_counter;

      uint64_t addr_val = queue.aw.empty() ? 0 : queue.aw.front().first;
      *(uint64_t *)addr = addr_val;

      uint32_t size_val = queue.aw.empty() ? 0 : queue.aw.front().second;
      *(uint32_t *)size = size_val;

      *len = 0;
      *burst = 0;
    }
  }

  // tickW should be called after tickAW
  void tickW(svBit *valid, svBit ready, char *data, char *strb, svBit *last) {
    static bool last_valid = false;
    if (last_valid && ready) {
      logger->debug("W");
      logger->trace("{} {}", spdlog::to_hex(*queue.w.front().first),
                    spdlog::to_hex(*queue.w.front().second));
      queue.w.pop();
    }
    *valid = !queue.w.empty();
    last_valid = *valid;

    if (*valid) {
      const auto &front = queue.w.front();
      std::copy_n(front.first->begin(), front.first->size(), (uint8_t *)data);
      std::copy_n(front.second->begin(), front.second->size(), (uint8_t *)strb);

      *last = 1;
    }
  }

  void tickB(svBit valid, svBit *ready, int32_t id, int32_t resp) {
    static bool last_ready = false;
    if (valid && last_ready) {
      queue.b.push(uint32_t(id));
      logger->debug("B {:d}", uint32_t(id));
    }

    *ready = 1;
    last_ready = *ready;
  }

  void tickAR(svBit *valid, svBit ready, int32_t *id, int64_t *addr,
              int32_t *len, int32_t *size, int32_t *burst) {
    static uint32_t id_counter = 0;

    static bool last_valid = false;
    if (last_valid && ready) {
      logger->debug("AR {:d} {:x} {:d}", id_counter, queue.ar.front().first,
                    queue.ar.front().second);
      queue.ar.pop();
      id_counter++;
    }
    *valid = !queue.ar.empty();
    last_valid = *valid;

    if (*valid) {
      *(uint32_t *)id = id_counter;

      uint64_t addr_val = queue.ar.empty() ? 0 : queue.ar.front().first;
      *(uint64_t *)addr = addr_val;

      uint32_t size_val = queue.ar.empty() ? 0 : queue.ar.front().second;
      *(uint32_t *)size = size_val;

      *len = 0;
      *burst = 0;
    }
  }

  void tickR(svBit valid, svBit *ready, int32_t id, const char *data,
             int32_t resp, svBit last) {
    static bool last_ready = false;
    if (valid && last_ready) {
      auto r_data = std::make_unique<AXIUtils::Data<dataBits>>();
      std::copy_n((const uint8_t *)data, r_data->size(), r_data->begin());
      queue.r.push(std::move(r_data));

      // std::cout << "R " << uint32_t(id) << std::endl;
      logger->debug("R {:d}", uint32_t(id));
      logger->trace("{}", spdlog::to_hex(*queue.r.back()));
    }

    *ready = 1;
    last_ready = *ready;
  }
};

#define EXPORT_AXI_AW(width)                                                   \
  extern "C" void AXI4MasterDPIAW_##width##_tick(                              \
      svBit *valid, svBit ready, int32_t *id, int64_t *addr, int32_t *len,     \
      int32_t *size, int32_t *burst) {                                         \
    AXI4MasterDPI##width.tickAW(valid, ready, id, addr, len, size, burst);     \
  }

#define EXPORT_AXI_W(width)                                                    \
  extern "C" void AXI4MasterDPIW_##width##_tick(                               \
      svBit *valid, svBit ready, char *data, char *strb, svBit *last) {        \
    AXI4MasterDPI##width.tickW(valid, ready, data, strb, last);                \
  }

#define EXPORT_AXI_B(width)                                                    \
  extern "C" void AXI4MasterDPIB_##width##_tick(svBit valid, svBit *ready,     \
                                                int32_t id, int32_t resp) {    \
    AXI4MasterDPI##width.tickB(valid, ready, id, resp);                        \
  }

#define EXPORT_AXI_AR(width)                                                   \
  extern "C" void AXI4MasterDPIAR_##width##_tick(                              \
      svBit *valid, svBit ready, int32_t *id, int64_t *addr, int32_t *len,     \
      int32_t *size, int32_t *burst) {                                         \
    AXI4MasterDPI##width.tickAR(valid, ready, id, addr, len, size, burst);     \
  }

#define EXPORT_AXI_R(width)                                                    \
  extern "C" void AXI4MasterDPIR_##width##_tick(svBit valid, svBit *ready,     \
                                                int32_t id, const char *data,  \
                                                int32_t resp, svBit last) {    \
    AXI4MasterDPI##width.tickR(valid, ready, id, data, resp, last);            \
  }

#define EXPORT_AXI_DPI_ALL(width)                                              \
  EXPORT_AXI_AW(width)                                                         \
  EXPORT_AXI_W(width)                                                          \
  EXPORT_AXI_B(width)                                                          \
  EXPORT_AXI_AR(width)                                                         \
  EXPORT_AXI_R(width)

extern AXI4MasterDPI<512> AXI4MasterDPI512;
// extern AXI4MasterDPI<64> AXI4MasterDPI64;
#define AXI4Master AXI4MasterDPI512

#endif
