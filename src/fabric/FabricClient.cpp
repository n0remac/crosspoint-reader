#include "FabricClient.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "network/HttpDownloader.h"

namespace fabric {
namespace {
void logHeap(const char* stage) {
  LOG_DBG("FABRIC", "%s: free=%u largest=%u", stage, ESP.getFreeHeap(),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

bool validComponentId(const char* id) {
  if (!id || !((*id >= 'A' && *id <= 'Z') || (*id >= 'a' && *id <= 'z'))) return false;
  size_t length = 0;
  for (; id[length]; ++length) {
    const char c = id[length];
    if (length >= 64 ||
        !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
      return false;
  }
  return true;
}
}  // namespace

bool Client::isValidServerUrl(const char* value) {
  const char* reason = nullptr;
  if (!value || !*value) {
    reason = "empty address";
  } else {
    const size_t schemeLength = strncmp(value, "http://", 7) == 0 ? 7 : strncmp(value, "https://", 8) == 0 ? 8 : 0;
    if (!schemeLength)
      reason = "expected http:// or https://";
    else if (!value[schemeLength] || value[schemeLength] == '/' || value[schemeLength] == ':')
      reason = "missing host";
    else if (strlen(value) > 127)
      reason = "address exceeds 127 bytes";
    else if (strpbrk(value + schemeLength, "@?#"))
      reason = "credentials, query or fragment not allowed";
    else {
      for (const unsigned char* c = reinterpret_cast<const unsigned char*>(value); *c; ++c) {
        if (*c <= ' ' || *c == 127 || *c == '\\') {
          reason = "whitespace, control character or backslash in address";
          break;
        }
      }
    }
  }
  if (reason) LOG_ERR("FABRIC", "Invalid server URL: %s", reason);
  return reason == nullptr;
}

bool Client::makeUrl(const char* path, char* out, size_t capacity) const {
  if (!isValidServerUrl(baseUrl)) return false;
  const size_t length = strlen(baseUrl);
  const int count =
      snprintf(out, capacity, "%.*s%s", static_cast<int>(length - (baseUrl[length - 1] == '/')), baseUrl, path);
  return count > 0 && static_cast<size_t>(count) < capacity;
}

bool Client::ensureBuffer() {
  if (responseBuffer) return true;
  responseBuffer = makeUniqueNoThrow<char[]>(MAX_DATA_BYTES);
  if (responseBuffer) return true;
  LOG_ERR("FABRIC", "OOM: %u-byte response buffer", static_cast<unsigned>(MAX_DATA_BYTES));
  return false;
}

Error Client::get(const char* path, JsonDocument& out, size_t limit) {
  resetDiagnostics();
  if (!makeUrl(path, url, sizeof(url))) return Error::InvalidServerUrl;
  LOG_INF("FABRIC", "GET %s", url);
  logHeap("before GET");
  if (!ensureBuffer()) return Error::OutOfMemory;
  logHeap("with response buffer");
  size_t length = 0;
  bool overflow = false;
  const bool fetched = HttpDownloader::fetchUrl(
      url,
      [this, &length, &overflow, limit](const uint8_t* bytes, size_t chunk) {
        if (chunk > limit - length) {
          overflow = true;
          return false;
        }
        memcpy(responseBuffer.get() + length, bytes, chunk);
        length += chunk;
        return true;
      },
      "", "", &lastHttpStatus);
  if (!fetched) {
    LOG_ERR("FABRIC", "GET failed: HTTP=%d bytes=%u overflow=%d WiFi=%d", lastHttpStatus, static_cast<unsigned>(length),
            overflow, WiFi.status());
    return overflow ? Error::TooLarge : WiFi.status() == WL_CONNECTED ? Error::Http : Error::Network;
  }
  LOG_INF("FABRIC", "GET complete: HTTP=%d bytes=%u", lastHttpStatus, static_cast<unsigned>(length));
  logHeap("after GET");
  out.clear();
  // const input forces ArduinoJson to own strings after this buffer is reused.
  const auto parsed = deserializeJson(out, static_cast<const char*>(responseBuffer.get()), length);
  if (parsed) {
    LOG_ERR("FABRIC", "GET JSON parse failed: %s", parsed.c_str());
    return Error::InvalidJson;
  }
  logHeap("after parse");
  return Error::None;
}

Error Client::listPages(JsonDocument& pages) { return get("/api/pages", pages, MAX_PAGE_BYTES); }

Error Client::getPage(const char* id, Page& page) {
  if (!validPageId(id)) return Error::MalformedPage;
  char path[96];
  snprintf(path, sizeof(path), "/api/pages/%s", id);
  const Error error = get(path, page.json, MAX_PAGE_BYTES);
  if (error != Error::None) return error;
  const Error validated = validatePage(page);
  if (validated != Error::None) return validated;
  return strcmp(page.id, id) == 0 ? Error::None : Error::MalformedPage;
}

Error Client::getPageData(const char* id, JsonDocument& data) {
  if (!validPageId(id)) return Error::MalformedPage;
  char path[105];
  snprintf(path, sizeof(path), "/api/pages/%s/data", id);
  const Error result = get(path, data, MAX_DATA_BYTES);
  if (result != Error::None) return result;
  return data.is<JsonObject>() ? Error::None : Error::InvalidJson;
}

Error Client::executeAction(const char* id, const char* componentId, JsonDocument& result) {
  resetDiagnostics();
  if (!validPageId(id) || !validComponentId(componentId)) return Error::Action;
  char path[112], body[100];
  snprintf(path, sizeof(path), "/api/pages/%s/actions", id);
  if (!makeUrl(path, url, sizeof(url))) return Error::InvalidServerUrl;
  snprintf(body, sizeof(body), "{\"component_id\":\"%s\"}", componentId);
  LOG_INF("FABRIC", "POST %s component=%s", url, componentId);
  logHeap("before POST");
  if (!ensureBuffer()) return Error::OutOfMemory;
  logHeap("with response buffer");
  size_t length = 0;
  if (!HttpDownloader::postJson(url, body, responseBuffer.get(), MAX_DATA_BYTES, length, &lastHttpStatus)) {
    LOG_ERR("FABRIC", "POST failed: HTTP=%d WiFi=%d", lastHttpStatus, WiFi.status());
    return WiFi.status() == WL_CONNECTED ? Error::Action : Error::Network;
  }
  LOG_INF("FABRIC", "POST complete: HTTP=%d bytes=%u", lastHttpStatus, static_cast<unsigned>(length));
  result.clear();
  const auto parsed = deserializeJson(result, static_cast<const char*>(responseBuffer.get()), length);
  if (parsed || !result.is<JsonObject>()) {
    LOG_ERR("FABRIC", "POST JSON invalid: %s object=%d", parsed.c_str(), result.is<JsonObject>());
    return Error::InvalidJson;
  }
  return Error::None;
}
}  // namespace fabric
