// #include <spdlog/common.h>
// #include <spdlog/fmt/bin_to_hex.h>
// #include <spdlog/sinks/stdout_color_sinks.h>
// #include <spdlog/spdlog.h>
// #include <svdpi.h>
// #include <sys/mman.h>
// #include <verilated_dpi.h>

// #include <array>
// #include <bitset>
// #include <cstdint>
// #include <memory>
// #include <queue>
// #include <span>

// #include "configuration.h"
// #include "dram_system.h"
// #include "sim.h"
// #include "utils/axi_utils.h"
// #include "utils/logger.h"

// namespace DDR4 {

// auto logger = spdlog::stderr_color_mt("DDR4");

// template <size_t dataBits_>
// class RWQueue {
//  public:
//   std::queue<std::tuple<uint32_t, uint64_t, uint32_t>> aw;
//   std::queue<uint32_t> b;
//   std::queue<std::pair<uint64_t, uint32_t>> r;
//   // std::queue<std::pair<uint32_t, std::unique_ptr<AXIUtils::Data<dataBits_>>>>
//   //     r_data;
// };

// RWQueue<512> rw_queue_512;

// const size_t mem_cap = 16 * 1024 * 1024 * 1024ULL;

// template <size_t cap>
// class Mem {
//   uint8_t* data_ptr;

//  public:
//   std::span<uint8_t> data_span;
//   Mem() {
//     data_ptr = (uint8_t*)mmap(NULL, cap, PROT_READ | PROT_WRITE,
//                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
//     data_span = std::span<uint8_t>(data_ptr, cap);
//   }
//   ~Mem() { munmap(data_ptr, cap); }
//   std::span<uint8_t> getData(size_t addr, size_t size) {
//     return data_span.subspan(addr, size);
//   }
//   std::span<const uint8_t> read(size_t addr, size_t size) const {
//     return data_span.subspan(addr, size);
//   }
//   void write(size_t addr, std::span<const uint8_t> data) {
//     std::copy_n(data.begin(), data.size(),
//                 data_span.subspan(addr, data.size()).begin());
//   }
// };

// std::unique_ptr<Mem<mem_cap>> mem = nullptr;

// std::unique_ptr<dramsim3::JedecDRAMSystem> sys = nullptr;

// template <size_t dataBits, RWQueue<dataBits>& queue,
//           size_t dataBytes = dataBits / 8>
// void read_callback(uint64_t addr, uint64_t id) {
//   queue.r.push({addr, id});
// };

// template <size_t dataBits, RWQueue<dataBits>& queue,
//           size_t dataBytes = dataBits / 8>
// void write_callback(uint64_t addr, uint64_t id) {
//   queue.b.push(id);
// };

// template <size_t dataBits, RWQueue<dataBits>& queue,
//           size_t dataBytes = dataBits / 8>
// void init(dramsim3::Config& config) {
//   if (!config.IsDDR4()) {
//     throw std::invalid_argument("Config is not for DDR4");
//   }
//   sys = std::make_unique<dramsim3::JedecDRAMSystem>(
//       config, ".", read_callback<dataBits, queue>,
//       write_callback<dataBits, queue>);

//   mem = std::make_unique<Mem<mem_cap>>();

//   // DDR4 specific methods here

//   logger->set_level(spdlog::level::info);
//   logger->set_formatter(std::move(make_sim_formatter()));
//   logger->info("DDR4 initialized");
// }

// void tick() { sys->ClockTick(); }

// void write(uint64_t addr, std::span<const uint8_t> data) {
//   mem->write(addr, data);
// }

// std::span<const uint8_t> read(uint64_t addr, size_t size) {
//   return mem->read(addr, size);
// }

// template <size_t idBits, size_t addrBits, size_t lenBits, size_t sizeBits,
//           size_t burstBits, size_t dataBits, RWQueue<dataBits>& queue>
// void tickAW(svBit valid, svBit* ready, int32_t id, int64_t addr, int32_t len,
//             int32_t size, int32_t burst) {
//   static_assert(idBits <= 32, "idBits too large");
//   static_assert(addrBits <= 64, "addrBits too large");
//   static_assert(lenBits <= 32, "lenBits too large");
//   static_assert(sizeBits <= 32, "sizeBits too large");
//   static_assert(burstBits <= 32, "burstBits too large");

//   static bool last_ready = false;

//   if (valid && last_ready) {
//     uint32_t len_val = uint32_t(len);
//     assert(len_val == 0);
//     // uint32_t burst_val = uint32_t(burst);
//     // assert(burst_val == 0);

//     uint32_t id_val = uint32_t(id);
//     uint64_t addr_val = uint64_t(addr);
//     uint32_t size_val = uint32_t(size);

//     queue.aw.push({id_val, addr_val, size_val});

//     logger->debug("AW {:d} {:x} {:d}", id_val, addr_val, size_val);
//   }

//   *ready = 1;
//   last_ready = *ready;
// }

// // tickW should be called after tickAW
// template <size_t dataBits, size_t strbBits, RWQueue<dataBits>& queue>
// void tickW(svBit valid, svBit* ready, const int8_t* data, const int8_t* strb,
//            svBit last) {
//   const size_t dataBytes = dataBits / 8;
//   static_assert(strbBits == dataBytes, "strbBits must be dataBits/8");

//   static bool buffer_empty = true;
//   static AXIUtils::Data<dataBits> wdata;
//   static AXIUtils::Strb<strbBits> wstrb;

//   if (valid && buffer_empty) {
//     std::copy_n((const uint8_t*)data, dataBytes, wdata.begin());
//     std::copy_n((const uint8_t*)strb, strbBits / 8, wstrb.begin());

//     buffer_empty = false;

//     logger->debug("W");
//     logger->trace("{} {}", spdlog::to_hex(wdata.begin(), wdata.end()),
//                   spdlog::to_hex(wstrb.begin(), wstrb.end()));
//   }

//   if (!buffer_empty && !queue.aw.empty()) {
//     auto [id_val, addr_val, size_val] = queue.aw.front();
//     if (sys->WillAcceptTransaction(addr_val, true)) {
//       sys->AddTransaction(addr_val, true, id_val);

//       auto addr_offset = addr_val % dataBytes;
//       auto wdata_it = wdata.begin() + addr_offset;
//       std::bitset<strbBits> wstrb_bits =
//           AXIUtils::arrayToBitset<strbBits / 8>(wstrb) >> addr_offset;
//       auto mem_it = mem->getData(addr_val, 1U << size_val).begin();
//       for (size_t i = 0; i < (1U << size_val); i++) {
//         if (wstrb_bits[i]) {
//           *mem_it = *wdata_it;
//         }
//         wdata_it++;
//         mem_it++;
//       }

//       queue.aw.pop();

//       buffer_empty = true;
//     }
//   }

//   *ready = buffer_empty;
// }

// template <size_t idBits, size_t respBits, size_t dataBits,
//           RWQueue<dataBits>& queue>
// void tickB(svBit* valid, svBit ready, int32_t* id, int32_t* resp) {
//   static_assert(idBits <= 32, "idBits too large");
//   static_assert(respBits <= 32, "respBits too large");

//   static bool last_valid = false;

//   if (last_valid && ready) {
//     logger->debug("B {:d}", uint32_t(*id));
//     queue.b.pop();
//   }

//   *valid = !queue.b.empty();
//   last_valid = *valid;

//   if (*valid) {
//     *id = queue.b.front();
//     *resp = 0;
//   }
// }

// template <size_t idBits, size_t addrBits, size_t lenBits, size_t sizeBits,
//           size_t burstBits, size_t dataBits, RWQueue<dataBits>& queue>
// void tickAR(svBit valid, svBit* ready, int32_t id, int64_t addr, int32_t len,
//             int32_t size, int32_t burst) {
//   const size_t dataBytes = dataBits / 8;
//   static_assert(idBits <= 32, "idBits too large");
//   static_assert(lenBits <= 32, "lenBits too large");
//   static_assert(sizeBits <= 32, "sizeBits too large");
//   static_assert(burstBits <= 32, "burstBits too large");

//   static bool buffer_empty = true;
//   static uint32_t id_val = 0;
//   static uint64_t addr_val = 0;
//   static uint32_t size_val = 0;

//   if (valid && buffer_empty) {
//     uint32_t len_val = uint32_t(len);
//     assert(len_val == 0);
//     // uint32_t burst_val = uint32_t(burst);
//     // assert(burst_val == 0);

//     id_val = uint32_t(id);
//     addr_val = uint64_t(addr);
//     size_val = uint32_t(size);

//     buffer_empty = false;

//     logger->debug("AR {:d} {:x} {:d}", id_val, addr_val, size_val);
//   }

//   if (!buffer_empty && sys->WillAcceptTransaction(addr_val, false)) {
//     sys->AddTransaction(addr_val, false, id_val);
//     buffer_empty = true;
//   }

//   *ready = buffer_empty;
// }

// template <size_t idBits, size_t dataBits, size_t respBits,
//           RWQueue<dataBits>& queue>
// void tickR(svBit* valid, svBit ready, int32_t* id, int8_t* data, int32_t* resp,
//            svBit* last) {
//   static_assert(idBits <= 32, "idBits too large");
//   static_assert(respBits <= 32, "respBits too large");

//   static bool last_valid = false;
//   if (last_valid && ready) {
//     queue.r.pop();
//   }

//   *valid = !queue.r.empty();
//   last_valid = *valid;

//   if (*valid) {
//     auto addr_val = queue.r.front().first;
//     auto id_val = queue.r.front().second;
//     auto data_span = mem->read(addr_val & ~(dataBits / 8ULL - 1), dataBits / 8);

//     *id = id_val;
//     std::copy_n(data_span.begin(), data_span.size(), (uint8_t*)data);
//     *resp = 0;
//     *last = 1;

//     logger->debug("R {:d}", id_val);
//     logger->trace("{}", spdlog::to_hex(data_span.begin(), data_span.end()));
//   }
// }

// };  // namespace DDR4

// // export macros

// #define EXPORT_AXI4_SLAVE_DPI_AW_TICK(idBits, addrBits, lenBits, sizeBits,                \
//                                       burstBits, dataBits, queue)                         \
//   extern "C" void                                                                         \
//       AXI4SlaveDPIAW_##idBits##_##addrBits##_##lenBits##_##sizeBits##_##burstBits##_tick( \
//           svBit valid, svBit* ready, int32_t id, int64_t addr, int32_t len,               \
//           int32_t size, int32_t burst) {                                                  \
//     DDR4::tickAW<idBits, addrBits, lenBits, sizeBits, burstBits, dataBits,                \
//                  queue>(valid, ready, id, addr, len, size, burst);                        \
//   }

// #define EXPORT_AXI4_SLAVE_DPI_W_TICK(dataBits, strbBits, queue)             \
//   extern "C" void AXI4SlaveDPIW_##dataBits##_##strbBits##_tick(             \
//       svBit valid, svBit* ready, const int8_t* data, const int8_t* strb,    \
//       svBit last) {                                                         \
//     DDR4::tickW<dataBits, strbBits, queue>(valid, ready, data, strb, last); \
//   }

// #define EXPORT_AXI4_SLAVE_DPI_B_TICK(idBits, respBits, dataBits, queue)     \
//   extern "C" void AXI4SlaveDPIB_##idBits##_##respBits##_tick(               \
//       svBit* valid, svBit ready, int32_t* id, int32_t* resp) {              \
//     DDR4::tickB<idBits, respBits, dataBits, queue>(valid, ready, id, resp); \
//   }

// #define EXPORT_AXI4_SLAVE_DPI_AR_TICK(idBits, addrBits, lenBits, sizeBits,                \
//                                       burstBits, dataBits, queue)                         \
//   extern "C" void                                                                         \
//       AXI4SlaveDPIAR_##idBits##_##addrBits##_##lenBits##_##sizeBits##_##burstBits##_tick( \
//           svBit valid, svBit* ready, int32_t id, int64_t addr, int32_t len,               \
//           int32_t size, int32_t burst) {                                                  \
//     DDR4::tickAR<idBits, addrBits, lenBits, sizeBits, burstBits, dataBits,                \
//                  queue>(valid, ready, id, addr, len, size, burst);                        \
//   }

// #define EXPORT_AXI4_SLAVE_DPI_R_TICK(idBits, dataBits, respBits, queue)    \
//   extern "C" void AXI4SlaveDPIR_##idBits##_##dataBits##_##respBits##_tick( \
//       svBit* valid, svBit ready, int32_t* id, int8_t* data, int32_t* resp, \
//       svBit* last) {                                                       \
//     DDR4::tickR<idBits, dataBits, respBits, queue>(valid, ready, id, data, \
//                                                    resp, last);            \
//   }

// // Instantiate DPI functions for common AXI4 configurations
// EXPORT_AXI4_SLAVE_DPI_AW_TICK(4, 33, 8, 3, 2, 512, DDR4::rw_queue_512)
// EXPORT_AXI4_SLAVE_DPI_W_TICK(512, 64, DDR4::rw_queue_512)
// EXPORT_AXI4_SLAVE_DPI_B_TICK(4, 2, 512, DDR4::rw_queue_512)
// EXPORT_AXI4_SLAVE_DPI_AR_TICK(4, 33, 8, 3, 2, 512, DDR4::rw_queue_512)
// EXPORT_AXI4_SLAVE_DPI_R_TICK(4, 512, 2, DDR4::rw_queue_512)

// void AXI4SlaveDPIInit_512(dramsim3::Config& config) {
//   DDR4::init<512, DDR4::rw_queue_512>(config);
// }