#include <stdint.h>
#include <svdpi.h>
#include <sys/mman.h>

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "configuration.h"
#include "ddr4.h"
#include "dram_system.h"

std::vector<bool> dpi_axi4_ar_id_busy(1 << 12);

struct Mem {
  size_t cap = 0;
  uint8_t *data = nullptr;
  Mem(size_t cap) {
    data = (uint8_t *)mmap(NULL, cap, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  }
  ~Mem() { munmap(data, cap); }
};

extern double npu_cycle, dram_cycle, npu_freq, dram_freq;

#define DRAM_CYCLE (dram_cycle)  // 1200MHz -> 0.833ns
#define NPU_CYCLE (npu_cycle)    // 1000MHz -> 1ns

std::queue<std::pair<uint64_t, uint64_t>> read_resp;
static void call_back_read(uint64_t addr, uint64_t id) {
  read_resp.push(std::make_pair(addr, id));
}

std::queue<std::pair<uint64_t, uint64_t>> write_resp;
static void call_back_write(uint64_t addr, uint64_t id) {
  write_resp.push(std::make_pair(addr, id));
}

static int data_bytes_ = 0;
std::unique_ptr<Mem> mem = nullptr;
const size_t mem_cap = 16 * 1024 * 1024 * 1024ULL;  // 16GB ??
std::unique_ptr<dramsim3::JedecDRAMSystem> sys = nullptr;

void DPIAXI4InitMem(dramsim3::Config &config) {
  mem = std::make_unique<Mem>(mem_cap);
  sys = std::make_unique<dramsim3::JedecDRAMSystem>(config, ".", call_back_read,
                                                    call_back_write);
}

extern "C" void *DPIAXI4Init(int id, int addr_bits, int data_bits) {
  printf(
      "Memory Simulator: Get Init signal id:%d, addr_bits:%d, data_bits:%d.\n",
      id, addr_bits, data_bits);
  // 创建 DRAMSim3 handler

  assert(mem != nullptr);
  assert(sys != nullptr);

  data_bytes_ = data_bits / 8;

  return (void *)sys.get();
}

extern "C" void DPIAXI4Tick(
    void *channel,

    unsigned char reset,

    unsigned char ar_valid, unsigned char *ar_ready, long long int ar_addr,
    int ar_id, int ar_size, int ar_len,

    unsigned char aw_valid, unsigned char *aw_ready, long long int aw_addr,
    int aw_id, int aw_size, int aw_len,

    unsigned char w_valid, unsigned char *w_ready, long long int w_strb,
    const svOpenArrayHandle w_data, unsigned char w_last,

    unsigned char *r_valid, unsigned char r_ready, int *r_id, int *r_resp,
    const svOpenArrayHandle r_data, unsigned char *r_last,

    unsigned char *b_valid, unsigned char b_ready, int *b_id, int *b_resp) {
  dramsim3::JedecDRAMSystem *sys = (dramsim3::JedecDRAMSystem *)channel;
  static bool local_aw_ready = 1;
  static bool local_w_ready = 1;
  static bool local_ar_ready = 1;
  static bool local_r_valid = 0;
  static bool local_b_valid = 0;
  static uint64_t local_ar_addr, local_ar_id, local_aw_addr, local_aw_id,
      local_aw_size;
  static char local_w_data[64];
  bool ar_fire = local_ar_ready & ar_valid;
  bool aw_fire = local_aw_ready & aw_valid;
  bool w_fire = local_w_ready & w_valid;
  bool r_deny = local_r_valid & (!r_ready);
  bool b_deny = local_b_valid & (!b_ready);
  bool aw_back_fire = false;
  if (ar_fire) {
    local_ar_ready = 0;
    local_ar_addr = ar_addr;
    local_ar_id = ar_id;
  }
  if (!local_ar_ready) {
    if(dpi_axi4_ar_id_busy[local_ar_id]) {
      // ID 正在使用，无法接受新请求
      printf("DPIAXI4: AR ID %lu is busy!\n", local_ar_id);
    }
    else if (sys->WillAcceptTransaction(local_ar_addr, false)) {
      // 添加请求进入队列
      sys->AddTransaction(local_ar_addr, false, local_ar_id);
      dpi_axi4_ar_id_busy[local_ar_id] = true;
      local_ar_ready = 1;
    }
  }

  if (aw_fire) {
    local_aw_ready = 0;
    local_aw_addr = aw_addr;
    local_aw_size = aw_size;
    local_aw_id = aw_id;
  }
  if (!local_aw_ready) {
    if (w_valid | (!local_w_ready)) {
      if (sys->WillAcceptTransaction(local_aw_addr, true)) {
        sys->AddTransaction(local_aw_addr, true, local_aw_id);
        // 立即完成写请求
        // backend_dram_write(fdMsg, local_w_ready ?
        // ((char*)svGetArrayPtr(w_data)) : local_w_data,
        //     data_bytes_, local_aw_addr);
        uint64_t write_bytes = 1 << local_aw_size;
        uint64_t write_offset = local_aw_addr % data_bytes_;
        memcpy(
            mem->data + local_aw_addr,
            (local_w_ready ? ((char *)svGetArrayPtr(w_data)) : local_w_data) +
                write_offset,
            write_bytes);
        local_aw_ready = 1;
        local_w_ready = 1;
        aw_back_fire = 1;
      }
    }
  }

  if (w_fire & !aw_back_fire) {
    local_w_ready = 0;
    memcpy((void *)local_w_data, (void *)svGetArrayPtr(w_data), data_bytes_);
  }

  static double npu_time = 0;
  static double dram_time = 0;
  static bool dram_ticked = false;

  npu_time += NPU_CYCLE;
  while (npu_time > dram_time) {
    sys->ClockTick();
    dram_time += DRAM_CYCLE;
  }

  if (!r_deny) {
    if (!read_resp.empty()) {
      // TODO: Note, this is not actually correct with axi standard.
      auto resp = read_resp.front();
      local_r_valid = 1;
      *r_id = resp.second;
      *r_last = 1;
      *r_resp = 0;
      // backend_dram_read(fdMsg, (char*)svGetArrayPtr(r_data), data_bytes_,
      // resp.first);
      auto r_data_ptr = (char *)svGetArrayPtr(r_data);
      memcpy(r_data_ptr, mem->data + (resp.first & ~(data_bytes_ - 1)),
             data_bytes_);
      read_resp.pop();
      dpi_axi4_ar_id_busy[resp.second] = false;
    } else {
      local_r_valid = 0;
    }
  }

  if (!b_deny) {
    if (!write_resp.empty()) {
      local_b_valid = 1;
      *b_resp = 0;
      *b_id = write_resp.front().second;
      write_resp.pop();
    } else {
      local_b_valid = 0;
    }
  }
  *ar_ready = local_ar_ready;
  *aw_ready = local_aw_ready;
  *w_ready = local_w_ready;
  *r_valid = local_r_valid;
  *b_valid = local_b_valid;
}

// fuse 机制处理
void DDR4::fuse_write(uint64_t addr, std::span<const uint8_t> data) {
  assert(addr + data.size() <= mem_cap);
  std::copy_n(data.begin(), data.size(), (uint8_t *)(mem->data + addr));
}

std::span<const uint8_t> DDR4::fuse_read(uint64_t addr, size_t size) {
  assert(addr + size <= mem_cap);
  return std::span<const uint8_t>((const uint8_t *)(mem->data + addr), size);
}
