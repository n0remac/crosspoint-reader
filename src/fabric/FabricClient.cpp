#include "FabricClient.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "FabricTransport.h"

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

struct Response {
  std::unique_ptr<char[]> buffer;
  size_t length = 0;
  size_t capacity = 0;
  bool overflow = false;
  bool outOfMemory = false;

  bool append(const uint8_t* bytes, size_t chunk, size_t limit) {
    if (chunk > limit - length) {
      overflow = true;
      return false;
    }
    const size_t required = length + chunk;
    if (required > capacity) {
      size_t next = capacity ? capacity : 1024;
      while (next < required) next = next > limit / 2 ? limit : next * 2;
      auto resized = makeUniqueNoThrow<char[]>(next);
      if (!resized) {
        outOfMemory = true;
        LOG_ERR("FABRIC", "OOM: %u-byte response buffer", static_cast<unsigned>(next));
        return false;
      }
      if (length) memcpy(resized.get(), buffer.get(), length);
      buffer = std::move(resized);
      capacity = next;
    }
    memcpy(buffer.get() + length, bytes, chunk);
    length = required;
    return true;
  }
};
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

Error Client::get(const char* path, JsonDocument& out, size_t limit) {
  resetDiagnostics();
  if (!makeUrl(path, url, sizeof(url))) return Error::InvalidServerUrl;
  if (token.empty()) return Error::Authentication;
  LOG_INF("FABRIC", "GET %s", url);
  logHeap("before GET");
  Response response;
  const bool fetched = transport.get(
      url, token,
      [&response, limit](const uint8_t* bytes, size_t chunk) { return response.append(bytes, chunk, limit); },
      &lastHttpStatus);
  if (!fetched) {
    LOG_ERR("FABRIC", "GET failed: HTTP=%d bytes=%u overflow=%d oom=%d WiFi=%d", lastHttpStatus,
            static_cast<unsigned>(response.length), response.overflow, response.outOfMemory, WiFi.status());
    return response.overflow               ? Error::TooLarge
           : response.outOfMemory          ? Error::OutOfMemory
           : WiFi.status() == WL_CONNECTED ? Error::Http
                                           : Error::Network;
  }
  LOG_INF("FABRIC", "GET complete: HTTP=%d bytes=%u", lastHttpStatus, static_cast<unsigned>(response.length));
  logHeap("after GET");
  if (!response.length) return Error::InvalidJson;
  out.clear();
  // const input forces ArduinoJson to own strings after this buffer is released.
  const auto parsed = deserializeJson(out, static_cast<const char*>(response.buffer.get()), response.length);
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
  if (token.empty()) return Error::Authentication;
  snprintf(body, sizeof(body), "{\"component_id\":\"%s\"}", componentId);
  LOG_INF("FABRIC", "POST %s component=%s", url, componentId);
  logHeap("before POST");
  Response response;
  if (!transport.post(
          url, token, body,
          [&response](const uint8_t* bytes, size_t chunk) { return response.append(bytes, chunk, MAX_DATA_BYTES); },
          &lastHttpStatus)) {
    LOG_ERR("FABRIC", "POST failed: HTTP=%d WiFi=%d", lastHttpStatus, WiFi.status());
    return response.overflow               ? Error::TooLarge
           : response.outOfMemory          ? Error::OutOfMemory
           : WiFi.status() == WL_CONNECTED ? Error::Action
                                           : Error::Network;
  }
  LOG_INF("FABRIC", "POST complete: HTTP=%d bytes=%u", lastHttpStatus, static_cast<unsigned>(response.length));
  if (!response.length) return Error::InvalidJson;
  result.clear();
  const auto parsed = deserializeJson(result, static_cast<const char*>(response.buffer.get()), response.length);
  if (parsed || !result.is<JsonObject>()) {
    LOG_ERR("FABRIC", "POST JSON invalid: %s object=%d", parsed.c_str(), result.is<JsonObject>());
    return Error::InvalidJson;
  }
  return Error::None;
}
}  // namespace fabric
