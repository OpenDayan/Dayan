#ifndef DDR4_H
#define DDR4_H

#include <cstddef>
#include <cstdint>
#include <span>

#include "configuration.h"

namespace DDR4 {
// void tick();
void fuse_write(uint64_t addr, std::span<const uint8_t> data);
std::span<const uint8_t> fuse_read(uint64_t addr, size_t size);
};  // namespace DDR4

// void AXI4SlaveDPIInit_512(dramsim3::Config& config);

#endif
