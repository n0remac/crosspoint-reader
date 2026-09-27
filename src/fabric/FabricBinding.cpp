#include "FabricBinding.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fabric {
JsonVariantConst resolve(JsonVariantConst data, const char* path) {
  if (!path || !*path) return {};
  JsonVariantConst current = data;
  char segment[65];
  while (*path) {
    size_t length = 0;
    while (path[length] && path[length] != '.') {
      const char c = path[length];
      if (length >= 64 || !(c == '_' || c == '-' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (length && c >= '0' && c <= '9')))
        return {};
      segment[length] = c;
      ++length;
    }
    if (!length || !current.is<JsonObjectConst>()) return {};
    segment[length] = 0;
    current = current[segment];
    if (current.isNull()) return {};
    path += length;
    if (*path == '.') {
      ++path;
      if (!*path) return {};
    }
  }
  return current;
}

bool numericValue(JsonVariantConst value, double& number) {
  if (value.is<bool>() || value.isNull()) return false;
  if (value.is<double>() || value.is<int>() || value.is<long>() || value.is<unsigned long>()) {
    number = value.as<double>();
  } else if (value.is<const char*>()) {
    const char* text = value.as<const char*>();
    char* end = nullptr;
    number = strtod(text, &end);
    if (end == text || *end) return false;
  } else
    return false;
  return std::isfinite(number);
}

bool formatValue(JsonVariantConst value, const char* format, const char* suffix, char* out, size_t capacity) {
  if (!out || !capacity) return false;
  out[0] = 0;
  if (value.isNull() || !format || !suffix) return false;
  int used = -1;
  if (!*format || strcmp(format, "plain") == 0) {
    if (value.is<const char*>())
      used = snprintf(out, capacity, "%s", value.as<const char*>());
    else if (value.is<bool>())
      used = snprintf(out, capacity, "%s", value.as<bool>() ? "true" : "false");
    else {
      double number;
      if (numericValue(value, number)) used = snprintf(out, capacity, "%g", number);
    }
  } else {
    double number;
    if (!numericValue(value, number)) return false;
    if (strcmp(format, "currency") == 0)
      used = snprintf(out, capacity, "$%.2f", number);
    else if (strcmp(format, "signed") == 0)
      used = snprintf(out, capacity, "%+.2f", number);
    else if (strcmp(format, "percent") == 0)
      used = snprintf(out, capacity, "%+.2f%%", number);
    else if (strcmp(format, "compact") == 0) {
      const double magnitude = fabs(number);
      const double scale = magnitude >= 1e9 ? 1e9 : magnitude >= 1e6 ? 1e6 : magnitude >= 1e3 ? 1e3 : 1;
      const char* unit = scale == 1e9 ? "B" : scale == 1e6 ? "M" : scale == 1e3 ? "K" : "";
      used = scale == 1 ? snprintf(out, capacity, "%.0f", number)
                        : snprintf(out, capacity, "%.1f%s", number / scale, unit);
    } else if (strcmp(format, "duration") == 0 && number >= 0 && number < 9e18) {
      int64_t remaining = static_cast<int64_t>(number);
      if (remaining < 60)
        used = snprintf(out, capacity, "%llds", static_cast<long long>(remaining));
      else {
        static constexpr int64_t UNIT_SECONDS[] = {86400, 3600, 60};
        static constexpr char UNIT_LABELS[] = {'d', 'h', 'm'};
        used = 0;
        int parts = 0;
        for (size_t index = 0; index < 3 && parts < 2; ++index) {
          const int64_t amount = remaining / UNIT_SECONDS[index];
          if (!amount) continue;
          const int written = snprintf(out + used, capacity - used, "%s%lld%c", parts ? " " : "",
                                       static_cast<long long>(amount), UNIT_LABELS[index]);
          if (written < 0 || static_cast<size_t>(written) >= capacity - used) return false;
          used += written;
          remaining %= UNIT_SECONDS[index];
          ++parts;
        }
      }
    }
  }
  if (used < 0 || static_cast<size_t>(used) >= capacity) return false;
  const int appended = snprintf(out + used, capacity - used, "%s", suffix);
  return appended >= 0 && static_cast<size_t>(appended) < capacity - used;
}

bool boundDisplay(JsonVariantConst data, JsonVariantConst component, char* out, size_t capacity) {
  const char* path = component["bind"] | "";
  if (!*path) return formatValue(component["text"], "plain", "", out, capacity);
  return formatValue(resolve(data, path), component["format"] | "plain", component["suffix"] | "", out, capacity);
}
}  // namespace fabric
