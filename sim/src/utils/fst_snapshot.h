// -*- mode: C++; c-file-style: "cc-mode" -*-
//=============================================================================
/// \file
/// \brief FST snapshot writer for Verilator simulations
///
/// Creates complete, viewable FST files at any point during simulation,
/// with full waveform history, without modifying Verilator source.
/// Each snapshot contains ALL history from the beginning.
///
/// Usage:
///   FstHistoryWriter writer(top);
///   writer.open("trace.fst");
///   for (...) {
///       top->eval();
///       writer.dump(time);
///       if (paused) {
///           writer.snapshot("pause.fst");  // Full history up to now
///       }
///   }
///   writer.close();
///
/// Requires: verilator --trace-fst, Verilated::traceEverOn(true)
//=============================================================================

#ifndef FST_SNAPSHOT_H_
#define FST_SNAPSHOT_H_

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "gtkwave/fstapi.h"
#include "verilated_fst_c.h"

// FST uses big-endian for multi-byte integers (fstWriterUint64 writes MSB
// first)
static inline uint64_t fstBE64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
  return v;
}
static inline void writeBE64(std::vector<uint8_t>& buf, uint64_t v) {
  for (int i = 7; i >= 0; i--) {
    buf.push_back(static_cast<uint8_t>(v & 0xff));
    v >>= 8;
  }
}

// Read entire file into byte vector
static inline std::vector<uint8_t> readFileBytes(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return {};
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> data(sz);
  fread(data.data(), 1, sz, f);
  fclose(f);
  return data;
}

// Write byte vector to file
static inline bool writeFileBytes(const char* path,
                                  const std::vector<uint8_t>& data) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  fwrite(data.data(), 1, data.size(), f);
  fclose(f);
  return true;
}

// Merge two complete FST files (A=earlier, B=later) into one.
// Result contains: A's VCDATA sections + B's VCDATA sections + B's GEOM + B's
// HIER with updated header (start_time from A, end_time from B, section counts
// summed).
static inline bool mergeFst(const char* pathA, const char* pathB,
                            const char* pathOut) {
  auto dataA = readFileBytes(pathA);
  auto dataB = readFileBytes(pathB);
  if (dataA.size() < 330 || dataB.size() < 330) return false;

  const uint64_t startTimeA = fstBE64(&dataA[9]);
  const uint64_t endTimeB = fstBE64(&dataB[17]);
  const uint64_t secCountA = fstBE64(&dataA[65]);
  const uint64_t secCountB = fstBE64(&dataB[65]);

  // FST section tag constants
  constexpr uint8_t FST_BL_VCDATA = 1;
  constexpr uint8_t FST_BL_VCDATA_DYN_ALIAS = 5;
  constexpr uint8_t FST_BL_VCDATA_DYN_ALIAS2 = 8;
  constexpr uint8_t FST_BL_GEOM = 3;
  constexpr uint8_t FST_BL_HIER = 4;
  constexpr uint8_t FST_BL_HIER_LZ4 = 6;
  constexpr uint8_t FST_BL_HIER_LZ4DUO = 7;

  auto isVCDATA = [](uint8_t tag) {
    return tag == FST_BL_VCDATA || tag == FST_BL_VCDATA_DYN_ALIAS ||
           tag == FST_BL_VCDATA_DYN_ALIAS2;
  };

  // Extract sections from a complete FST file (skip 330-byte header)
  struct Section {
    uint8_t tag;
    std::vector<uint8_t> raw;
  };  // raw = tag + body
  auto extractSections =
      [](const std::vector<uint8_t>& data) -> std::vector<Section> {
    std::vector<Section> secs;
    size_t pos = 330;
    while (pos + 9 <= data.size()) {
      uint8_t tag = data[pos];
      uint64_t slen = fstBE64(&data[pos + 1]);
      size_t sectionTotal = 1 + slen;  // tag(1) + body(slen)
      if (pos + sectionTotal > data.size()) break;
      Section s;
      s.tag = tag;
      s.raw.assign(data.begin() + pos, data.begin() + pos + sectionTotal);
      secs.push_back(std::move(s));
      pos += sectionTotal;
    }
    return secs;
  };

  auto secsA = extractSections(dataA);
  auto secsB = extractSections(dataB);

  // Build output: header + A's VCDATA + B's VCDATA + B's GEOM + B's HIER
  std::vector<uint8_t> out;
  // Start with B's header (330 bytes)
  out.assign(dataB.begin(), dataB.begin() + 330);
  // Patch header: start_time = A's, end_time = max(A,B), section_count = sum
  auto patchBE64 = [](std::vector<uint8_t>& buf, size_t off, uint64_t v) {
    for (int i = 7; i >= 0; i--) {
      buf[off + i] = v & 0xff;
      v >>= 8;
    }
  };
  uint64_t endTimeA = fstBE64(&dataA[17]);
  uint64_t maxEnd = (endTimeA > endTimeB) ? endTimeA : endTimeB;
  uint64_t totalSections = 0;
  for (auto& s : secsA)
    if (isVCDATA(s.tag)) totalSections++;
  for (auto& s : secsB)
    if (isVCDATA(s.tag)) totalSections++;
  patchBE64(out, 9, startTimeA);
  patchBE64(out, 17, maxEnd);
  patchBE64(out, 65, totalSections);

  // Append A's VCDATA sections
  for (auto& s : secsA)
    if (isVCDATA(s.tag)) out.insert(out.end(), s.raw.begin(), s.raw.end());

  // Append B's VCDATA sections, then GEOM, then HIER
  for (auto& s : secsB) {
    if (isVCDATA(s.tag) || s.tag == FST_BL_GEOM || s.tag == FST_BL_HIER ||
        s.tag == FST_BL_HIER_LZ4 || s.tag == FST_BL_HIER_LZ4DUO) {
      out.insert(out.end(), s.raw.begin(), s.raw.end());
    }
  }

  return writeFileBytes(pathOut, out);
}

// Create a gap FST with 'x' for all signals at gapTime.
// Replays hierarchy from templatePath so handle assignments match.
static inline bool createGapFst(const char* outputPath, uint64_t gapTime,
                                const char* templatePath) {
  fstReaderContext* reader = fstReaderOpen(templatePath);
  if (!reader) return false;

  fstWriterContext* writer = fstWriterCreate(outputPath, 1);
  if (!writer) {
    fstReaderClose(reader);
    return false;
  }

  struct GapVar {
    fstHandle handle;
    uint32_t length;
  };
  std::vector<GapVar> gapVars;

  struct fstHier* hier;
  while ((hier = fstReaderIterateHier(reader)) != nullptr) {
    switch (hier->htyp) {
      case FST_HT_SCOPE:
        fstWriterSetScope(writer,
                          static_cast<enum fstScopeType>(hier->u.scope.typ),
                          hier->u.scope.name, hier->u.scope.component);
        break;
      case FST_HT_UPSCOPE:
        fstWriterSetUpscope(writer);
        break;
      case FST_HT_VAR: {
        fstHandle h = fstWriterCreateVar(
            writer, static_cast<enum fstVarType>(hier->u.var.typ),
            static_cast<enum fstVarDir>(hier->u.var.direction),
            hier->u.var.length, hier->u.var.name,
            hier->u.var.is_alias ? hier->u.var.handle : 0);
        if (!hier->u.var.is_alias && hier->u.var.length > 0 &&
            hier->u.var.typ != FST_VT_VCD_REAL &&
            hier->u.var.typ != FST_VT_VCD_REAL_PARAMETER &&
            hier->u.var.typ != FST_VT_VCD_REALTIME &&
            hier->u.var.typ != FST_VT_SV_SHORTREAL) {
          gapVars.push_back({h, hier->u.var.length});
        }
        break;
      }
      default:
        break;
    }
  }

  fstWriterEmitTimeChange(writer, gapTime);
  for (const auto& v : gapVars) {
    std::string xval(v.length, 'x');
    fstWriterEmitValueChange(writer, v.handle, xval.c_str());
  }

  fstWriterClose(writer);
  fstReaderClose(reader);
  return true;
}

//=============================================================================
// FstHistoryWriter — Full-history parallel trace with cumulative snapshots

class FstHistoryWriter {
  VerilatedFstC* m_tfp = nullptr;
  int m_levels = 99;
  std::string m_internalName;
  std::string m_accumName;  // Accumulated history file
  bool m_hasAccum = false;
  std::function<void(VerilatedFstC*, int)> m_traceFn;
  std::string m_timeUnit;
  std::string m_timeResolution;
  bool m_insertGaps = true;
  uint64_t m_dumpInterval = 1;

  void reopenInternal() {
    m_tfp = new VerilatedFstC;
    m_traceFn(m_tfp, m_levels);
    if (!m_timeUnit.empty()) m_tfp->set_time_unit(m_timeUnit);
    if (!m_timeResolution.empty()) m_tfp->set_time_resolution(m_timeResolution);
    m_tfp->open(m_internalName.c_str());
  }

 public:
  template <typename Module>
  explicit FstHistoryWriter(Module* topp, int levels = 99,
                            uint64_t dumpInterval = 1)
      : m_levels{levels}, m_dumpInterval{dumpInterval} {
    m_traceFn = [topp](VerilatedFstC* tfp, int lv) { topp->trace(tfp, lv); };
  }

  ~FstHistoryWriter() { close(); }

  /// Set time unit (e.g. "ns", "1ps"). Must be called before open().
  void set_time_unit(const char* unit) {
    m_timeUnit = unit;
    if (m_tfp) m_tfp->set_time_unit(unit);
  }
  void set_time_unit(const std::string& unit) {
    m_timeUnit = unit;
    if (m_tfp) m_tfp->set_time_unit(unit);
  }

  /// Set time resolution (e.g. "1ps"). Must be called before open().
  void set_time_resolution(const char* unit) {
    m_timeResolution = unit;
    if (m_tfp) m_tfp->set_time_resolution(unit);
  }
  void set_time_resolution(const std::string& unit) {
    m_timeResolution = unit;
    if (m_tfp) m_tfp->set_time_resolution(unit);
  }

  /// Enable/disable 'x' gap insertion between snapshot segments (default: true)
  void set_insert_gaps(bool enable) { m_insertGaps = enable; }

  /// Start recording.
  void open(const char* internalName) {
    m_internalName = internalName;
    m_accumName = std::string(internalName) + ".accum";
    m_hasAccum = false;
    reopenInternal();
  }

  /// Record one time step.
  void dump(uint64_t time) {
    if (m_tfp) m_tfp->dump(time);
  }

  /// Export a complete FST with ALL history from the beginning.
  void snapshot(const char* filename) {
    if (!m_tfp) return;

    // Close internal trace -> produces complete FST (recent segment)
    m_tfp->close();
    delete m_tfp;
    m_tfp = nullptr;

    // Check if the internal file has new data (end_time > 0)
    auto segData = readFileBytes(m_internalName.c_str());
    uint64_t segEnd = (segData.size() >= 25) ? fstBE64(&segData[17]) : 0;

    if (!m_hasAccum) {
      // First snapshot: internal file IS the full history so far
      rename(m_internalName.c_str(), m_accumName.c_str());
      m_hasAccum = true;
    } else if (segEnd > 0) {
      // segStart = first dump time of the new segment
      uint64_t segStart = (segData.size() >= 17) ? fstBE64(&segData[9]) : 0;
      bool gapInserted = false;

      if (m_insertGaps) {
        auto accumData = readFileBytes(m_accumName.c_str());
        uint64_t accumEnd =
            (accumData.size() >= 25) ? fstBE64(&accumData[17]) : 0;

        // Only insert gap when the time gap exceeds one dump interval
        if (segStart > accumEnd + m_dumpInterval) {
          std::string gapName = m_accumName + ".gap";
          createGapFst(gapName.c_str(), accumEnd + m_dumpInterval,
                       m_accumName.c_str());

          // Merge: accum + gap -> temp
          std::string tempAccum = m_accumName + ".withgap";
          mergeFst(m_accumName.c_str(), gapName.c_str(), tempAccum.c_str());
          remove(m_accumName.c_str());
          remove(gapName.c_str());

          // Merge: temp + segment -> new accum
          mergeFst(tempAccum.c_str(), m_internalName.c_str(),
                   m_accumName.c_str());
          remove(tempAccum.c_str());
          remove(m_internalName.c_str());
          gapInserted = true;
        }
      }

      if (!gapInserted) {
        // No gap or gap insertion disabled: direct merge
        std::string newAccum = m_accumName + ".new";
        mergeFst(m_accumName.c_str(), m_internalName.c_str(), newAccum.c_str());
        remove(m_accumName.c_str());
        remove(m_internalName.c_str());
        rename(newAccum.c_str(), m_accumName.c_str());
      }
    } else {
      // No new data since last snapshot, just remove the empty segment
      remove(m_internalName.c_str());
    }

    // Copy accumulated file to requested snapshot name
    auto data = readFileBytes(m_accumName.c_str());
    writeFileBytes(filename, data);

    // Start fresh internal trace for the next segment
    reopenInternal();
  }

  /// Close and clean up. Removes internal temp files.
  void close() {
    if (m_tfp) {
      m_tfp->close();
      delete m_tfp;
      m_tfp = nullptr;
    }
    if (!m_internalName.empty()) remove(m_internalName.c_str());
    if (m_hasAccum) remove(m_accumName.c_str());
    m_internalName.clear();
    m_accumName.clear();
    m_hasAccum = false;
  }
};

#endif  // FST_SNAPSHOT_H_
