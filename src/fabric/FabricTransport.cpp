#include "FabricTransport.h"

#include <Logging.h>
#include <Memory.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <cstring>

namespace fabric {
namespace {
bool request(const char* url, const std::string& token, const char* body, const Transport::DataCallback& onData,
             int* status) {
  if (status) *status = 0;
  if (token.size() != 64) return false;
  esp_http_client_config_t config = {};
  config.url = url;
  config.buffer_size = 2048;
  config.buffer_size_tx = 512;
  config.timeout_ms = 60000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;
  char authorization[72] = "Bearer ";
  memcpy(authorization + 7, token.data(), 64);
  authorization[71] = '\0';
  esp_http_client_set_header(client, "Authorization", authorization);
  esp_http_client_set_header(client, "User-Agent", "CrossPoint-Fabric-" CROSSPOINT_VERSION);
  if (body) {
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
  }
  const size_t bodySize = body ? strlen(body) : 0;
  if (esp_http_client_open(client, bodySize) != ESP_OK ||
      (body && esp_http_client_write(client, body, bodySize) != static_cast<int>(bodySize))) {
    LOG_ERR("FABRIC", "Authenticated request could not connect or write");
    esp_http_client_cleanup(client);
    return false;
  }
  if (esp_http_client_fetch_headers(client) < 0) {
    esp_http_client_cleanup(client);
    return false;
  }
  const int responseStatus = esp_http_client_get_status_code(client);
  if (status) *status = responseStatus;
  if (responseStatus != 200) {
    esp_http_client_cleanup(client);
    return false;
  }
  auto buffer = makeUniqueNoThrow<uint8_t[]>(512);
  if (!buffer) {
    esp_http_client_cleanup(client);
    return false;
  }
  while (true) {
    const int count = esp_http_client_read(client, reinterpret_cast<char*>(buffer.get()), 512);
    if (count < 0) break;
    if (count == 0) {
      const bool complete = esp_http_client_is_complete_data_received(client);
      esp_http_client_cleanup(client);
      return complete;
    }
    if (!onData(buffer.get(), count)) break;
  }
  esp_http_client_cleanup(client);
  return false;
}
}  // namespace

bool Transport::get(const char* url, const std::string& token, const DataCallback& onData, int* status) {
  return request(url, token, nullptr, onData, status);
}
bool Transport::post(const char* url, const std::string& token, const char* body, const DataCallback& onData,
                     int* status) {
  return request(url, token, body, onData, status);
}
}  // namespace fabric
