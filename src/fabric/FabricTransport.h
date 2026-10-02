#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace fabric {
// Authenticated requests never follow redirects, so the reader credential
// cannot be forwarded to another origin. HTTPS uses the ESP-IDF CA bundle.
class Transport {
 public:
  using DataCallback = std::function<bool(const uint8_t*, size_t)>;
  static bool get(const char* url, const std::string& token, const DataCallback& onData, int* status);
  static bool post(const char* url, const std::string& token, const char* body, const DataCallback& onData,
                   int* status);
};
}  // namespace fabric
