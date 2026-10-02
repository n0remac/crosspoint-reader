#include "FabricFirmware.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Memory.h>
#include <Preferences.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>

#include <cstring>
#include <string_view>

#include "CrossPointSettings.h"
#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"

#if __has_include("../FabricProvisioning.local.h")
#include "../FabricProvisioning.local.h"
#endif

namespace fabric_firmware {
namespace {
constexpr size_t MAX_METADATA = 2048;
constexpr char API_PATH[] = "/api/firmware/v1/x3";

bool lowercaseHex(std::string_view s, size_t expected) {
  if (s.size() != expected) return false;
  for (const char c : s) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

FetchResult get(const std::string& path, const std::string& token,
                const std::function<bool(const uint8_t*, size_t)>& onData, size_t expectedSize = 0) {
  if (!configured() || token.empty() || path.rfind(API_PATH, 0) != 0) return FetchResult::Error;
  std::string url(FABRIC_DEFAULT_SERVER_URL);
  while (!url.empty() && url.back() == '/') url.pop_back();
  url += path;
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.buffer_size = 2048;
  config.buffer_size_tx = 512;
  config.timeout_ms = 60000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return FetchResult::Error;
  const std::string auth = "Bearer " + token;
  esp_http_client_set_header(client, "Authorization", auth.c_str());
  esp_http_client_set_header(client, "User-Agent", "CrossPoint-Fabric-" CROSSPOINT_VERSION);

  if (esp_http_client_open(client, 0) != ESP_OK) {
    LOG_ERR("FABRIC_OTA", "HTTPS connection failed");
    esp_http_client_cleanup(client);
    return FetchResult::Error;
  }
  const int64_t contentLength = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (status == 404) {
    esp_http_client_cleanup(client);
    return FetchResult::NotFound;
  }
  if (status != 200 || contentLength < 0 ||
      (expectedSize && contentLength > 0 && static_cast<uint64_t>(contentLength) != expectedSize)) {
    LOG_ERR("FABRIC_OTA", "Unexpected response: status=%d size=%lld", status, static_cast<long long>(contentLength));
    esp_http_client_cleanup(client);
    return FetchResult::Error;
  }

  auto buffer = makeUniqueNoThrow<uint8_t[]>(1024);
  if (!buffer) {
    LOG_ERR("FABRIC_OTA", "Could not allocate download buffer");
    esp_http_client_cleanup(client);
    return FetchResult::Error;
  }
  size_t received = 0;
  while (true) {
    const int n = esp_http_client_read(client, reinterpret_cast<char*>(buffer.get()), 1024);
    if (n < 0) break;
    if (n == 0) {
      const bool complete = esp_http_client_is_complete_data_received(client);
      esp_http_client_cleanup(client);
      return complete && (!expectedSize || received == expectedSize) ? FetchResult::Ok : FetchResult::Error;
    }
    if ((expectedSize && static_cast<size_t>(n) > expectedSize - received) || !onData(buffer.get(), n)) break;
    received += n;
  }
  esp_http_client_cleanup(client);
  return FetchResult::Error;
}
}  // namespace

std::string readerToken() {
  Preferences preferences;
  if (!preferences.begin("fabric", false)) return {};
  String token = preferences.getString("reader", "");
#ifdef FABRIC_BOOTSTRAP_READER_TOKEN
  if (token.isEmpty()) {
    const char* bootstrap = FABRIC_BOOTSTRAP_READER_TOKEN;
    if (strlen(bootstrap) == 64 && preferences.putString("reader", bootstrap) == 64) {
      token = bootstrap;
      LOG_INF("FABRIC_OTA", "Reader credential provisioned to NVS");
    }
  }
#endif
  preferences.end();
  const std::string result(token.c_str());
  return lowercaseHex(result, 64) ? result : std::string();
}

std::string installedBuildId() {
  Preferences preferences;
  if (!preferences.begin("fabric", true)) return {};
  const String value = preferences.getString("build", "");
  uint8_t expected[32];
  const bool hasDigest = preferences.getBytesLength("image") == sizeof(expected) &&
                         preferences.getBytes("image", expected, sizeof(expected)) == sizeof(expected);
  preferences.end();
  const esp_partition_t* running = esp_ota_get_running_partition();
  uint8_t actual[32];
  return hasDigest && running && esp_partition_get_sha256(running, actual) == ESP_OK &&
                 memcmp(expected, actual, sizeof(expected)) == 0 && lowercaseHex(value.c_str(), 32)
             ? std::string(value.c_str())
             : std::string();
}

bool rememberInstalledBuildId(const std::string& id, const uint8_t imageDigest[32]) {
  if (!lowercaseHex(id, 32)) return false;
  Preferences preferences;
  if (!preferences.begin("fabric", false)) return false;
  preferences.remove("build");
  const bool saved =
      preferences.putBytes("image", imageDigest, 32) == 32 && preferences.putString("build", id.c_str()) == id.size();
  preferences.end();
  return saved;
}

bool configured() {
  std::string_view url(FABRIC_DEFAULT_SERVER_URL);
  while (!url.empty() && url.back() == '/') url.remove_suffix(1);
  return url.starts_with("https://") && url.size() > 8 && url.find_first_of("/?#@", 8) == std::string_view::npos &&
         firmware_flash::runningPartitionChipId() == 5 && board_tag::boardNameLen() == 2 &&
         memcmp(board_tag::boardName(), "x4", 2) == 0;
}

FetchResult latest(const std::string& token, Build& build) {
  std::string body;
  body.reserve(512);
  const auto result =
      get(std::string(API_PATH) + "/latest?channel=dev", token, [&body](const uint8_t* data, size_t size) {
        if (size > MAX_METADATA - body.size()) return false;
        body.append(reinterpret_cast<const char*>(data), size);
        return true;
      });
  if (result != FetchResult::Ok) return result;
  JsonDocument document;
  if (deserializeJson(document, body) || !document.is<JsonObject>()) return FetchResult::Error;
  if (strcmp(document["device"] | "", "x3") != 0 ||
      document["chip_id"].as<unsigned>() != firmware_flash::runningPartitionChipId() ||
      strcmp(document["board_tag"] | "", "x4") != 0)
    return FetchResult::Error;
  const char* id = document["id"] | "";
  const char* version = document["version"] | "";
  const char* sha = document["sha256"] | "";
  const char* downloadPath = document["download_path"] | "";
  const size_t size = document["size"].as<size_t>();
  const std::string expectedPath = std::string(API_PATH) + "/builds/" + id + "/download";
  if (!lowercaseHex(id, 32) || !lowercaseHex(sha, 64) || !*version || strlen(version) > 128 || size < 65536 ||
      size > 0x640000 || strcmp(downloadPath, expectedPath.c_str()) != 0) {
    return FetchResult::Error;
  }
  build = {id, version, sha, size};
  return FetchResult::Ok;
}

bool download(const std::string& token, const Build& build, const std::function<bool(const uint8_t*, size_t)>& onData) {
  if (!lowercaseHex(build.id, 32) || !lowercaseHex(build.sha256, 64) || build.size < 65536 || build.size > 0x640000)
    return false;
  return get(std::string(API_PATH) + "/builds/" + build.id + "/download", token, onData, build.size) == FetchResult::Ok;
}
}  // namespace fabric_firmware
