#include "FabricTransport.h"

#include <Logging.h>
#include <Memory.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_wifi.h>

#include <cstring>

namespace fabric {
namespace {
struct WifiPowerSaveGuard {
  wifi_ps_type_t previous = WIFI_PS_MIN_MODEM;
  bool restore = false;
  WifiPowerSaveGuard() {
    if (esp_wifi_get_ps(&previous) == ESP_OK && previous != WIFI_PS_NONE)
      restore = esp_wifi_set_ps(WIFI_PS_NONE) == ESP_OK;
  }
  ~WifiPowerSaveGuard() {
    if (restore) esp_wifi_set_ps(previous);
  }
};

}  // namespace

Transport::~Transport() { close(); }

void Transport::close() {
  if (!handle) return;
  esp_http_client_cleanup(static_cast<esp_http_client_handle_t>(handle));
  handle = nullptr;
}

bool Transport::request(const char* url, const std::string& token, const char* body, const DataCallback& onData,
                        int* status) {
  if (status) *status = 0;
  if (token.size() != 64) return false;
  WifiPowerSaveGuard powerSave;
  auto client = static_cast<esp_http_client_handle_t>(handle);
  if (client) {
    if (esp_http_client_set_url(client, url) != ESP_OK) {
      close();
      return false;
    }
  } else {
    esp_http_client_config_t config = {};
    config.url = url;
    config.buffer_size = 2048;
    config.buffer_size_tx = 512;
    config.timeout_ms = 60000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = true;
    client = esp_http_client_init(&config);
    if (!client) return false;
    handle = client;
  }
  char authorization[72] = "Bearer ";
  memcpy(authorization + 7, token.data(), 64);
  authorization[71] = '\0';
  esp_http_client_set_header(client, "Authorization", authorization);
  esp_http_client_set_header(client, "User-Agent", "CrossPoint-Fabric-" CROSSPOINT_VERSION);
  esp_http_client_set_method(client, body ? HTTP_METHOD_POST : HTTP_METHOD_GET);
  if (body) {
    esp_http_client_set_header(client, "Content-Type", "application/json");
  } else {
    esp_http_client_delete_header(client, "Content-Type");
  }
  const size_t bodySize = body ? strlen(body) : 0;
  const esp_err_t opened = esp_http_client_open(client, bodySize);
  if (opened != ESP_OK) {
    LOG_ERR("FABRIC", "HTTPS open failed: %s errno=%d free=%u largest=%u", esp_err_to_name(opened),
            esp_http_client_get_errno(client), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    close();
    return false;
  }
  if (body && esp_http_client_write(client, body, bodySize) != static_cast<int>(bodySize)) {
    LOG_ERR("FABRIC", "Authenticated request body could not be sent: errno=%d", esp_http_client_get_errno(client));
    close();
    return false;
  }
  if (esp_http_client_fetch_headers(client) < 0) {
    LOG_ERR("FABRIC", "Response headers failed: errno=%d", esp_http_client_get_errno(client));
    close();
    return false;
  }
  const int responseStatus = esp_http_client_get_status_code(client);
  if (status) *status = responseStatus;
  if (responseStatus != 200) {
    close();
    return false;
  }
  auto buffer = makeUniqueNoThrow<uint8_t[]>(512);
  if (!buffer) {
    close();
    return false;
  }
  while (true) {
    const int count = esp_http_client_read(client, reinterpret_cast<char*>(buffer.get()), 512);
    if (count < 0) {
      LOG_ERR("FABRIC", "Response read failed: errno=%d", esp_http_client_get_errno(client));
      break;
    }
    if (count == 0) {
      const bool complete = esp_http_client_is_complete_data_received(client);
      if (!complete) close();
      return complete;
    }
    if (!onData(buffer.get(), count)) break;
  }
  close();
  return false;
}

bool Transport::get(const char* url, const std::string& token, const DataCallback& onData, int* status) {
  if (request(url, token, nullptr, onData, status)) return true;
  // Retry only when no HTTP response started, such as a failed TLS handshake
  // or an idle keep-alive connection closed by the server. GET is safe to
  // repeat; POST actions are deliberately never retried here.
  if (token.size() == 64 && status && *status == 0) {
    LOG_INF("FABRIC", "Retrying GET with fresh connection");
    close();
    return request(url, token, nullptr, onData, status);
  }
  return false;
}
bool Transport::post(const char* url, const std::string& token, const char* body, const DataCallback& onData,
                     int* status) {
  return request(url, token, body, onData, status);
}
}  // namespace fabric
