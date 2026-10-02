#include <cassert>
#include <cstring>
#include <string>

#include "fabric/FabricClient.h"
#include "fabric/FabricTransport.h"

static std::string requested;
static int responseStatus = 200;
static const char* responseBody = "{}";
static bool transportOk = true;

bool fabric::Transport::get(const char* url, const std::string& token, const DataCallback& onData, int* httpStatus) {
  assert(token == std::string(64, 'a'));
  requested = url;
  if (httpStatus) *httpStatus = responseStatus;
  return transportOk && onData(reinterpret_cast<const uint8_t*>(responseBody), strlen(responseBody));
}

bool fabric::Transport::post(const char* url, const std::string& token, const char* body, const DataCallback& onData,
                             int* httpStatus) {
  assert(token == std::string(64, 'a'));
  requested = url;
  assert(std::string(body) == "{\"component_id\":\"refresh\"}");
  if (httpStatus) *httpStatus = responseStatus;
  return transportOk && onData(reinterpret_cast<const uint8_t*>(responseBody), strlen(responseBody));
}

int main() {
  JsonDocument json;
  for (const char* base :
       {"http://example.test", "https://example.test", "https://example.test/", "https://example.test:443/base/"}) {
    fabric::Client client(base, std::string(64, 'a'));
    std::string prefix(base);
    if (prefix.back() == '/') prefix.pop_back();
    assert(client.listPages(json) == fabric::Error::None);
    assert(requested == prefix + "/api/pages");
    assert(client.getPageData("stocks", json) == fabric::Error::None);
    assert(requested == prefix + "/api/pages/stocks/data");
    assert(client.executeAction("stocks", "refresh", json) == fabric::Error::None);
    assert(requested == prefix + "/api/pages/stocks/actions");
  }
  for (const char* base :
       {static_cast<const char*>(nullptr), "", "http://", "https://", "ftp://example.test", "htttp://example.test",
        "https:///path", "https://:443", "https://example.test/a b", "https://example.test/\\path",
        "https://example.test/\npath", "https://user:pass@example.test", "http://example.test?x=1",
        "https://example.test#fragment"}) {
    fabric::Client client(base, std::string(64, 'a'));
    requested.clear();
    assert(client.listPages(json) == fabric::Error::InvalidServerUrl);
    assert(client.executeAction("stocks", "refresh", json) == fabric::Error::InvalidServerUrl);
    assert(requested.empty());
    assert(client.httpStatus() == 0);
  }
  const std::string tooLong = "https://" + std::string(120, 'a');
  fabric::Client rejected(tooLong.c_str(), std::string(64, 'a'));
  assert(rejected.listPages(json) == fabric::Error::InvalidServerUrl);
  const std::string maxLength = "https://" + std::string(119, 'a');
  fabric::Client accepted(maxLength.c_str(), std::string(64, 'a'));
  assert(accepted.listPages(json) == fabric::Error::None);
  fabric::Client client("https://example.test", std::string(64, 'a'));
  transportOk = false;
  responseStatus = 404;
  assert(client.listPages(json) == fabric::Error::Http);
  assert(client.httpStatus() == 404);
  responseStatus = 503;
  assert(client.executeAction("stocks", "refresh", json) == fabric::Error::Action);
  assert(client.httpStatus() == 503);
  responseStatus = -1;
  assert(client.listPages(json) == fabric::Error::Http);
  assert(client.httpStatus() == -1);
  transportOk = true;
  responseStatus = 200;
  responseBody = "<html>not JSON</html>";
  assert(client.listPages(json) == fabric::Error::InvalidJson);
  assert(client.httpStatus() == 200);
  responseBody = "[]";
  assert(client.executeAction("stocks", "refresh", json) == fabric::Error::InvalidJson);
  client.resetDiagnostics();
  assert(client.httpStatus() == 0);
}
