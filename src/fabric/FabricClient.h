#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <memory>

#include "FabricPage.h"

namespace fabric {
class Client {
 public:
  explicit Client(const char* baseUrl) : baseUrl(baseUrl) {}
  Error listPages(JsonDocument& pages);
  Error getPage(const char* id, Page& page);
  Error getPageData(const char* id, JsonDocument& data);
  Error executeAction(const char* id, const char* componentId, JsonDocument& result);

 private:
  const char* baseUrl;
  char url[224]{};
  std::unique_ptr<char[]> responseBuffer;
  bool ensureBuffer();
  Error get(const char* path, JsonDocument& out, size_t limit);
  bool makeUrl(const char* path, char* out, size_t capacity) const;
};
}  // namespace fabric
