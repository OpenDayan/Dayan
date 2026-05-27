#ifndef LOGGER_H
#define LOGGER_H

#include <spdlog/pattern_formatter.h>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>

#include <string>

#include "sim.h"

class SimTimeFormatter : public spdlog::custom_flag_formatter {
 public:
  void format(const spdlog::details::log_msg&, const std::tm&,
              spdlog::memory_buf_t& dest) override {
    if (contextp) {
      std::string s = fmt::format("{:>8d}", contextp->time());
      dest.append(s.data(), s.data() + s.size());
    } else {
      const std::string s = "       0";
      dest.append(s.data(), s.data() + s.size());
    }
  }

  std::unique_ptr<custom_flag_formatter> clone() const override {
    return spdlog::details::make_unique<SimTimeFormatter>();
  }
};

inline std::unique_ptr<spdlog::pattern_formatter> make_sim_formatter() {
  auto formatter = std::make_unique<spdlog::pattern_formatter>();
  formatter->add_flag<SimTimeFormatter>('*').set_pattern(
      "[%Y-%m-%d %T.%e] [%*] [%n] [%^%l%$] %v");
  return formatter;
}

#endif  // LOGGER_H
