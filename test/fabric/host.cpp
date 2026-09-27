#include <ArduinoJson.h>

#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "fabric/FabricBinding.h"
#include "fabric/FabricNavigator.h"
#include "fabric/FabricPage.h"

static std::string readFile(const char* path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

int main() {
  fabric::Page page;
  for (const char* name : {"stocks", "stock-detail"}) {
    const std::string fixture = readFile((std::string("test/fabric/fixtures/") + name + ".json").c_str());
    assert(!fixture.empty());
    assert(fabric::parsePage(fixture.data(), fixture.size(), page) == fabric::Error::None);
    assert(strcmp(page.id, name) == 0);
    assert(page.intervalSeconds == 60);
  }
  const char* invalidVersion =
      R"({"fabric":"0.3","id":"example","title":"Example","layout":{"type":"text","text":"x"}})";
  assert(fabric::parsePage(invalidVersion, strlen(invalidVersion), page) == fabric::Error::UnsupportedVersion);
  const char* missingLayout = R"({"fabric":"0.2","id":"example","title":"Example"})";
  assert(fabric::parsePage(missingLayout, strlen(missingLayout), page) == fabric::Error::MalformedPage);
  const char* unknownType = R"({"fabric":"0.2","id":"example","title":"Example","layout":{"type":"script"}})";
  assert(fabric::parsePage(unknownType, strlen(unknownType), page) == fabric::Error::UnsupportedComponent);
  const std::string oversized(fabric::MAX_PAGE_BYTES + 1, 'x');
  assert(fabric::parsePage(oversized.data(), oversized.size(), page) == fabric::Error::TooLarge);
  const char* badActionId =
      R"({"fabric":"0.2","id":"example","title":"Example","layout":{"type":"button","id":"bad id","label":"Go","action":{"type":"refresh"}}})";
  assert(fabric::parsePage(badActionId, strlen(badActionId), page) == fabric::Error::MalformedPage);
  const char* badBinding =
      R"({"fabric":"0.2","id":"example","title":"Example","layout":{"type":"text","bind":"stocks..price"}})";
  assert(fabric::parsePage(badBinding, strlen(badBinding), page) == fabric::Error::MalformedPage);
  const char* badStyle =
      R"({"fabric":"0.2","id":"example","title":"Example","layout":{"type":"text","text":"x","style":"css"}})";
  assert(fabric::parsePage(badStyle, strlen(badStyle), page) == fabric::Error::MalformedPage);
  const char* nested =
      R"({"fabric":"0.2","id":"example","title":"Example","layout":{"type":"column","children":[{"type":"row","children":[{"type":"text","text":"a"},{"type":"button","id":"go","label":"Go","action":{"type":"back"}}]}]}})";
  assert(fabric::parsePage(nested, strlen(nested), page) == fabric::Error::None);
  assert(page.actionCount == 1);

  JsonDocument data;
  assert(!deserializeJson(
      data,
      R"({"stocks":{"selected":{"price":182.34,"ok":true,"missing":null,"history":[{"time":"2026-01-01T00:00:00Z","price":1.2}]},"items":{"AAPL":{"price":1250000}}}})"));
  auto root = data.as<JsonVariantConst>();
  assert(std::fabs(fabric::resolve(root, "stocks.selected.price").as<double>() - 182.34) < 0.001);
  assert(fabric::resolve(root, "stocks.items.AAPL.price").as<int>() == 1250000);
  assert(fabric::resolve(root, "stocks.selected.history").is<JsonArrayConst>());
  assert(fabric::resolve(root, "stocks.selected.ok").as<bool>());
  assert(fabric::resolve(root, "stocks.selected.missing").isNull());
  assert(fabric::resolve(root, "stocks.selected.unknown").isNull());
  assert(fabric::resolve(root, "stocks.selected.price.value").isNull());
  char formatted[64];
  assert(fabric::formatValue(fabric::resolve(root, "stocks.selected.price"), "currency", "", formatted,
                             sizeof(formatted)));
  assert(strcmp(formatted, "$182.34") == 0);
  assert(
      fabric::formatValue(fabric::resolve(root, "stocks.selected.price"), "signed", "", formatted, sizeof(formatted)));
  assert(strcmp(formatted, "+182.34") == 0);
  assert(
      fabric::formatValue(fabric::resolve(root, "stocks.selected.price"), "percent", "", formatted, sizeof(formatted)));
  assert(strcmp(formatted, "+182.34%") == 0);
  assert(fabric::formatValue(fabric::resolve(root, "stocks.items.AAPL.price"), "compact", "", formatted,
                             sizeof(formatted)));
  assert(strcmp(formatted, "1.2M") == 0);
  assert(fabric::formatValue(fabric::resolve(root, "stocks.selected.ok"), "plain", "", formatted, sizeof(formatted)));
  assert(strcmp(formatted, "true") == 0);
  assert(!fabric::formatValue(fabric::resolve(root, "stocks.selected.missing"), "plain", "", formatted,
                              sizeof(formatted)));

  JsonDocument unchanged;
  assert(!deserializeJson(
      unchanged,
      R"({"stocks":{"selected":{"price":182.34,"ok":true,"missing":null,"history":[{"time":"2026-01-01T00:00:00Z","price":1.2}]},"items":{"AAPL":{"price":1250000}}}})"));
  assert(data.as<JsonVariantConst>() == unchanged.as<JsonVariantConst>());
  JsonDocument duration;
  duration.set(3600);
  assert(fabric::formatValue(duration.as<JsonVariantConst>(), "duration", "", formatted, sizeof(formatted)));
  assert(strcmp(formatted, "1h") == 0);
  duration.set(90061);
  assert(fabric::formatValue(duration.as<JsonVariantConst>(), "duration", "", formatted, sizeof(formatted)));
  assert(strcmp(formatted, "1d 1h") == 0);
  fabric::Navigator nav;
  assert(!nav.back());
  assert(nav.open("stocks"));
  assert(nav.open("stock-detail"));
  assert(nav.depth() == 1);
  assert(nav.open("stock-detail"));
  assert(nav.depth() == 1);
  assert(nav.open("stocks"));
  assert(strcmp(nav.current(), "stocks") == 0);
  assert(!nav.back());
  assert(!nav.open("../unsafe"));
  assert(nav.applyAction("refresh", "stocks") == fabric::Transition::Stay);
  assert(nav.applyAction("invoke", "stock-detail") == fabric::Transition::Open);
  assert(nav.applyAction("invoke", "stock-detail") == fabric::Transition::Stay);
  assert(nav.applyAction("navigate", "stocks") == fabric::Transition::Open);
  assert(nav.applyAction("back", "") == fabric::Transition::Exit);
  assert(nav.applyAction("navigate", "../unsafe") == fabric::Transition::Invalid);
}
