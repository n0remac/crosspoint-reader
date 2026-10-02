#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace fabric_firmware {

struct Build {
  std::string id;
  std::string version;
  std::string sha256;
  size_t size = 0;
};

enum class FetchResult { Ok, NotFound, Error };

// The bootstrap-only local header can seed this token into NVS. Subsequent OTA
// applications read NVS and contain no credential.
std::string readerToken();
std::string installedBuildId();
bool rememberInstalledBuildId(const std::string& id, const uint8_t imageDigest[32]);

// These requests use the firmware's compiled Fabric origin, never the editable
// Fabric page URL, so a changed SD settings file cannot receive the token.
bool configured();
FetchResult latest(const std::string& token, Build& build);
bool download(const std::string& token, const Build& build, const std::function<bool(const uint8_t*, size_t)>& onData);

}  // namespace fabric_firmware
