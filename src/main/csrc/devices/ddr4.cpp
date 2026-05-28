
#include "devices/ddr4.h"

DDR4DPI<512> DDR4DPI512;

void DDR4::fuse_write(uint64_t addr, std::span<const uint8_t> data) {
  assert(addr + data.size() <= mem_cap);
  DDR4Mod.write(addr, data);
}

std::span<const uint8_t> DDR4::fuse_read(uint64_t addr, size_t size) {
  assert(addr + size <= mem_cap);
  return DDR4Mod.read(addr, size);
}

EXPORT_DDR4_DPI_ALL(512)
