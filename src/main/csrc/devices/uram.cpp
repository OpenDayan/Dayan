#include <cstdint>

// Bank Count
// 1: 2, 2: 4, 3: 8, 4: 16, 5: 32
#define LOG2BANK 5
#define URAMSIZE ((1 << LOG2BANK) * 32 * 1024ull)
long int uram[URAMSIZE / 8];  // 1024KBytes URAM
static char* uramBase = (char*)&uram;

extern "C" long long UltraRamRead(int id, int raddr) {
  long long rtValue = uram[id | (raddr << LOG2BANK)];
  return rtValue;
}

extern "C" long long UltraRamWrite(int id, int byte_enable, int waddr,
                                   long long wdata) {
  if (byte_enable == 0xff) [[likely]] {
    uram[id | (waddr << LOG2BANK)] = wdata;
  } else [[unlikely]] {
    uint8_t* dst_addr = (uint8_t*)&(uram[id | (waddr << LOG2BANK)]);
    uint8_t* src_addr = (uint8_t*)&wdata;
    for (int i = 0; i < 8; i++) {
      if (byte_enable & 0x1) dst_addr[i] = src_addr[i];
      byte_enable >>= 1;
    }
  }
  return 0;
}
