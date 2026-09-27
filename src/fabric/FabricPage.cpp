#include "FabricPage.h"

#include <cstring>

namespace fabric {
namespace {
bool oneOf(const char* value, const char* const* values, size_t count) {
  for (size_t index = 0; index < count; ++index)
    if (strcmp(value, values[index]) == 0) return true;
  return false;
}

bool isType(const char* type) {
  static constexpr const char* TYPES[] = {"column",  "row",  "card",     "text",   "metric",
                                          "divider", "list", "progress", "button", "chart"};
  for (const char* item : TYPES) {
    if (strcmp(type, item) == 0) return true;
  }
  return false;
}

bool validComponentId(const char* id) {
  if (!id || !((*id >= 'A' && *id <= 'Z') || (*id >= 'a' && *id <= 'z'))) return false;
  size_t length = 0;
  for (; id[length]; ++length) {
    const char c = id[length];
    if (length >= 64 ||
        !(c == '_' || c == '-' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (length && c >= '0' && c <= '9')))
      return false;
  }
  return true;
}

bool validBinding(const char* path) {
  if (!path || !*path || strlen(path) > 256) return false;
  size_t segment = 0;
  for (const char* cursor = path; *cursor; ++cursor) {
    const char c = *cursor;
    if (c == '.') {
      if (!segment) return false;
      segment = 0;
      continue;
    }
    if (segment >= 64 || !(c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                           (segment && (c == '-' || (c >= '0' && c <= '9')))))
      return false;
    ++segment;
  }
  return segment != 0;
}

Error checkComponent(JsonVariantConst node, size_t depth, size_t& count, uint8_t& actions) {
  if (!node.is<JsonObjectConst>() || depth > MAX_DEPTH || ++count > MAX_COMPONENTS) return Error::MalformedPage;
  const char* type = node["type"] | "";
  if (!isType(type)) return Error::UnsupportedComponent;
  static constexpr const char* TEXT_FIELDS[] = {"id", "label", "text", "bind", "x", "y", "style", "format", "suffix"};
  for (const char* field : TEXT_FIELDS)
    if (!node[field].isNull() && !node[field].is<const char*>()) return Error::MalformedPage;
  const char* id = node["id"] | "";
  if ((*id && !validComponentId(id)) ||
      (node["bind"].is<const char*>() && !validBinding(node["bind"].as<const char*>())) ||
      (node["x"].is<const char*>() && !validBinding(node["x"].as<const char*>())) ||
      (node["y"].is<const char*>() && !validBinding(node["y"].as<const char*>())))
    return Error::MalformedPage;
  if (strlen(id) > 64 || strlen(node["label"] | "") > 256 || strlen(node["text"] | "") > 4096 ||
      strlen(node["bind"] | "") > 256 || strlen(node["x"] | "") > 256 || strlen(node["y"] | "") > 256 ||
      strlen(node["suffix"] | "") > 32)
    return Error::MalformedPage;
  static constexpr const char* STYLES[] = {"heading", "subheading", "body", "muted", "emphasis"};
  static constexpr const char* FORMATS[] = {"plain", "duration", "currency", "signed", "percent", "compact"};
  const char* style = node["style"] | "";
  const char* format = node["format"] | "";
  if ((*style && !oneOf(style, STYLES, 5)) || (*format && !oneOf(format, FORMATS, 6))) return Error::MalformedPage;
  if (!node["action"].isNull()) {
    if (!node["action"].is<JsonObjectConst>() || !*id || ++actions > 24) return Error::MalformedPage;
    const char* action = node["action"]["type"] | "";
    if (strcmp(action, "refresh") && strcmp(action, "navigate") && strcmp(action, "back") && strcmp(action, "invoke"))
      return Error::MalformedPage;
  }
  if (strcmp(type, "chart") == 0 && (!(node["x"].is<const char*>()) || !(node["y"].is<const char*>())))
    return Error::MalformedPage;
  JsonVariantConst children = node["children"];
  if (!children.isNull()) {
    if (!children.is<JsonArrayConst>() || children.size() > MAX_COMPONENTS ||
        (strcmp(type, "row") == 0 && children.size() > 8))
      return Error::MalformedPage;
    for (JsonVariantConst child : children.as<JsonArrayConst>()) {
      const Error error = checkComponent(child, depth + 1, count, actions);
      if (error != Error::None) return error;
    }
  }
  return Error::None;
}
}  // namespace

bool validPageId(const char* id) {
  if (!id || *id < 'a' || *id > 'z') return false;
  size_t length = 0;
  for (; id[length]; ++length) {
    const char c = id[length];
    if (length >= 64 || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  }
  return true;
}

Error parsePage(const char* body, size_t size, Page& page) {
  if (!body || size > MAX_PAGE_BYTES) return Error::TooLarge;
  page.json.clear();
  page.id[0] = page.title[0] = 0;
  page.intervalSeconds = 0;
  page.fetchOnOpen = false;
  page.actionCount = 0;
  if (deserializeJson(page.json, body, size)) return Error::InvalidJson;
  return validatePage(page);
}

Error validatePage(Page& page) {
  JsonVariantConst root = page.json.as<JsonVariantConst>();
  if (!root.is<JsonObjectConst>()) return Error::MalformedPage;
  const char* version = root["fabric"] | "";
  if (strcmp(version, "0.2") != 0) return Error::UnsupportedVersion;
  const char* id = root["id"] | "";
  const char* title = root["title"] | "";
  if (!validPageId(id) || !*title || strlen(title) > 128 || !root["layout"].is<JsonObjectConst>())
    return Error::MalformedPage;
  size_t count = 0;
  const Error error = checkComponent(root["layout"], 0, count, page.actionCount);
  if (error != Error::None) return error;
  strcpy(page.id, id);
  strcpy(page.title, title);
  const char* strategy = root["data"]["refresh"]["strategy"] | "manual";
  if (strcmp(strategy, "on-open") == 0)
    page.fetchOnOpen = true;
  else if (strcmp(strategy, "interval") == 0) {
    const int seconds = root["data"]["refresh"]["seconds"] | 0;
    if (seconds < 1 || seconds > 86400) return Error::MalformedPage;
    page.intervalSeconds = seconds;
    page.fetchOnOpen = true;
  } else if (strcmp(strategy, "manual") != 0)
    return Error::MalformedPage;
  return Error::None;
}
}  // namespace fabric
