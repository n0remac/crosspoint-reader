#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>

namespace fabric {

constexpr size_t MAX_PAGE_BYTES = 24576;
constexpr size_t MAX_DATA_BYTES = 32768;
constexpr size_t MAX_COMPONENTS = 96;
constexpr size_t MAX_DEPTH = 8;
constexpr size_t MAX_CHART_POINTS = 256;

enum class Error : uint8_t {
  None,
  Network,
  InvalidServerUrl,
  Http,
  InvalidJson,
  UnsupportedVersion,
  NoPages,
  UnsupportedComponent,
  MalformedPage,
  Action,
  TooLarge,
  OutOfMemory,
  Persistence,
};

struct Page {
  JsonDocument json;
  char id[65]{};
  char title[129]{};
  uint32_t intervalSeconds = 0;
  bool fetchOnOpen = false;
  uint8_t actionCount = 0;
};

Error parsePage(const char* body, size_t size, Page& page);
Error validatePage(Page& page);
bool validPageId(const char* id);

}  // namespace fabric
