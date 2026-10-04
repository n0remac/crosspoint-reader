#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <string>
#include <utility>

#include "FabricPage.h"
#include "FabricTransport.h"

namespace fabric {
class Client {
 public:
  explicit Client(const char* baseUrl, std::string token) : baseUrl(baseUrl), token(std::move(token)) {}
  static bool isValidServerUrl(const char* value);
  int httpStatus() const { return lastHttpStatus; }
  void resetDiagnostics() { lastHttpStatus = 0; }
  Error listPages(JsonDocument& pages);
  Error getPage(const char* id, Page& page);
  Error getPageData(const char* id, JsonDocument& data);
  Error executeAction(const char* id, const char* componentId, JsonDocument& result);

 private:
  const char* baseUrl;
  std::string token;
  Transport transport;
  char url[224]{};
  int lastHttpStatus = 0;
  Error get(const char* path, JsonDocument& out, size_t limit);
  bool makeUrl(const char* path, char* out, size_t capacity) const;
};
}  // namespace fabric
