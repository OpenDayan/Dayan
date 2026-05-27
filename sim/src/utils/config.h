#ifndef CONFIG_H
#define CONFIG_H

#include <cstdlib>
#include <string>

static std::string getEnvOrDefault(const std::string& key,
                                   const std::string& defaultValue) {
  const char* val = std::getenv(key.c_str());
  return (val != nullptr) ? std::string(val) : defaultValue;
}

#endif