#ifndef AXI_UTILS_H
#define AXI_UTILS_H

#include <svdpi.h>
#include <verilated_dpi.h>

#include <array>
#include <bitset>
#include <cstdint>
#include <span>

namespace AXIUtils {

template <size_t dataBits>
using Data = std::array<uint8_t, (dataBits + 7) / 8>;

template <size_t dataBits>
using DataSpan = std::span<uint8_t, (dataBits + 7) / 8>;

template <size_t strbBits>
using Strb = std::array<uint8_t, (strbBits + 7) / 8>;

template <size_t strbBits>
using StrbSpan = std::span<uint8_t, (strbBits + 7) / 8>;

template <size_t N>
std::bitset<N * 8> arrayToBitset(const std::array<uint8_t, N>& arr) {
  std::bitset<N * 8> bits;
  for (size_t i = 0; i < N; ++i) {
    bits |= (std::bitset<N * 8>(arr[i]) << (i * 8));
  }
  return bits;
}

template <size_t N>
std::bitset<N * 8> arrayToBitset(const std::span<uint8_t, N> arr) {
  std::bitset<N * 8> bits;
  for (size_t i = 0; i < N; ++i) {
    bits |= (std::bitset<N * 8>(arr[i]) << (i * 8));
  }
  return bits;
}

}  // namespace AXIUtils

#endif
