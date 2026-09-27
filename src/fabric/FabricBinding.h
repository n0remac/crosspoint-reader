#pragma once

#include <ArduinoJson.h>

#include <cstddef>

namespace fabric {
JsonVariantConst resolve(JsonVariantConst data, const char* path);
bool formatValue(JsonVariantConst value, const char* format, const char* suffix, char* out, size_t capacity);
bool boundDisplay(JsonVariantConst data, JsonVariantConst component, char* out, size_t capacity);
bool numericValue(JsonVariantConst value, double& number);
}  // namespace fabric
