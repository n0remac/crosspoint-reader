#include "FabricUiRenderer.h"

#include <I18n.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "FabricBinding.h"
#include "FabricPage.h"

namespace fabric {
namespace fui = freeink::ui;
namespace {
constexpr auto INK = fui::Paint::solid(fui::Color::Black);

int16_t leafHeight(const char* type, const fui::ThemeTokens& theme) {
  if (strcmp(type, "chart") == 0) return 140;
  if (strcmp(type, "divider") == 0) return 12;
  if (strcmp(type, "metric") == 0) return theme.rowHeight + 4;
  if (strcmp(type, "progress") == 0) return theme.rowHeight;
  return theme.rowHeight;
}

bool chartX(JsonVariantConst value, double& output) {
  if (numericValue(value, output)) return true;
  if (!value.is<const char*>()) return false;
  const char* text = value.as<const char*>();
  // RFC3339 timestamps are converted to seconds without allocating or relying
  // on libc timezone state. Fractional seconds are unnecessary at e-ink scale.
  if (strlen(text) < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':')
    return false;
  auto digits = [text](size_t at, size_t count, int& result) {
    result = 0;
    for (size_t i = 0; i < count; ++i) {
      if (text[at + i] < '0' || text[at + i] > '9') return false;
      result = result * 10 + text[at + i] - '0';
    }
    return true;
  };
  int year, month, day, hour, minute, second;
  if (!digits(0, 4, year) || !digits(5, 2, month) || !digits(8, 2, day) || !digits(11, 2, hour) ||
      !digits(14, 2, minute) || !digits(17, 2, second) || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
      minute > 59 || second > 60)
    return false;
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097 + static_cast<int>(doe) - 719468;
  output = static_cast<double>(days * 86400 + hour * 3600 + minute * 60 + second);
  size_t tail = 19;
  if (text[tail] == '.') {
    ++tail;
    while (text[tail] >= '0' && text[tail] <= '9') ++tail;
  }
  if (text[tail] == 'Z' && !text[tail + 1]) return true;
  if ((text[tail] == '+' || text[tail] == '-') && strlen(text + tail) == 6 && text[tail + 3] == ':') {
    int offsetHours, offsetMinutes;
    if (!digits(tail + 1, 2, offsetHours) || !digits(tail + 4, 2, offsetMinutes) || offsetHours > 23 ||
        offsetMinutes > 59)
      return false;
    output -= (text[tail] == '+' ? 1 : -1) * (offsetHours * 3600 + offsetMinutes * 60);
    return true;
  }
  return false;
}

bool visible(fui::Rect rect, int16_t top, int16_t bottom) {
  return rect.bottom() > top && rect.y < bottom && rect.width > 0;
}
}  // namespace

void UiRenderer::render(UiAppHost::UiScreen& screen, uint8_t focused, uint16_t scroll) {
  actions = 0;
  auto area = screen.body();
  viewportTop = area.y;
  viewportBottom = area.bottom();
  const int16_t x = area.x + 6;
  const int16_t y = area.y - static_cast<int16_t>(scroll);
  const auto oldClip = screen.target().clipRect();
  screen.target().setClipRect(area);
  bottom = renderNode(screen, page["layout"], x, y, area.width - 12, focused, 0);
  screen.target().setClipRect(oldClip);
}

int16_t UiRenderer::measureNode(const fui::ThemeTokens& theme, JsonVariantConst node, uint8_t depth) const {
  if (!node.is<JsonObjectConst>() || depth > MAX_DEPTH) return 0;
  const char* type = node["type"] | "";
  JsonArrayConst children = node["children"].as<JsonArrayConst>();
  if (strcmp(type, "row") == 0) {
    int16_t height = 0;
    for (JsonVariantConst child : children) height = std::max(height, measureNode(theme, child, depth + 1));
    return height;
  }
  if (strcmp(type, "column") == 0 || strcmp(type, "card") == 0 || strcmp(type, "list") == 0) {
    int16_t height = strcmp(type, "card") == 0 ? 10 : 0;
    if (strcmp(type, "list") == 0 && !children) {
      JsonVariantConst bound = resolve(data, node["bind"] | "");
      if (bound.is<JsonArrayConst>())
        height += std::min(static_cast<size_t>(bound.size()), size_t{32}) * theme.rowHeight;
    }
    for (JsonVariantConst child : children) height += measureNode(theme, child, depth + 1);
    return std::max(height, theme.rowHeight);
  }
  return leafHeight(type, theme) + theme.spaceSm;
}

int16_t UiRenderer::renderNode(UiAppHost::UiScreen& screen, JsonVariantConst node, int16_t x, int16_t y, int16_t width,
                               uint8_t focused, uint8_t depth) {
  if (!node.is<JsonObjectConst>() || depth > MAX_DEPTH || width <= 0) return y;
  const char* type = node["type"] | "";
  JsonArrayConst children = node["children"].as<JsonArrayConst>();
  const char* id = node["id"] | "";
  const bool actionable = *id && node["action"].is<JsonObjectConst>();
  const uint8_t actionIndex = actions;
  if (actionable) {
    ++actions;
    const int16_t height = measureNode(screen.theme(), node, depth);
    if (actionIndex == focused) {
      focusTop = y;
      focusBottom = y + height;
    }
    if (strcmp(type, "button") != 0) {
      const int16_t top = std::max(y, viewportTop);
      const int16_t end = std::min(static_cast<int16_t>(y + height), viewportBottom);
      if (end > top) {
        screen.frame().hit(fui::Rect{x, top, width, static_cast<int16_t>(end - top)}, ACTION_COMPONENT, actionIndex,
                           fui::InputTouch, actionIndex == focused ? fui::StateSelected : fui::StateNormal);
      }
    }
  }
  if (strcmp(type, "column") == 0 || strcmp(type, "card") == 0 || strcmp(type, "list") == 0) {
    const int16_t start = y;
    if (strcmp(type, "card") == 0) {
      x += 5;
      width -= 10;
      y += 5;
    }
    if (strcmp(type, "list") == 0 && !children && node["bind"].is<const char*>()) {
      JsonVariantConst value = resolve(data, node["bind"] | "");
      if (value.is<JsonArrayConst>()) {
        size_t count = 0;
        for (JsonVariantConst item : value.as<JsonArrayConst>()) {
          if (++count > 32) break;
          if (!formatValue(item, "plain", "", display, displayCapacity))
            snprintf(display, displayCapacity, "%s", tr(STR_FABRIC_VALUE_MISSING));
          fui::Rect rect{x, y, width, screen.theme().rowHeight};
          if (visible(rect, viewportTop, viewportBottom)) screen.target().text(rect, display, screen.theme().bodyText);
          y += screen.theme().rowHeight;
        }
      } else {
        fui::Rect rect{x, y, width, screen.theme().rowHeight};
        if (visible(rect, viewportTop, viewportBottom))
          screen.target().text(rect, tr(STR_FABRIC_VALUE_MISSING), screen.theme().smallText);
        y += screen.theme().rowHeight;
      }
    }
    for (JsonVariantConst child : children) y = renderNode(screen, child, x, y, width, focused, depth + 1);
    if (strcmp(type, "card") == 0) {
      y += 5;
      fui::Rect border{static_cast<int16_t>(x - 5), start, static_cast<int16_t>(width + 10),
                       static_cast<int16_t>(y - start)};
      if (visible(border, viewportTop, viewportBottom)) screen.target().stroke(border, INK, 1);
    }
    return y;
  }
  if (strcmp(type, "row") == 0) {
    const size_t count = children.size();
    if (!count) return y;
    const int16_t gap = screen.theme().spaceSm;
    const int16_t cellWidth = (width - gap * (count - 1)) / count;
    int16_t maxBottom = y;
    size_t index = 0;
    for (JsonVariantConst child : children) {
      const int16_t childX = x + index * (cellWidth + gap);
      maxBottom = std::max(maxBottom, renderNode(screen, child, childX, y, cellWidth, focused, depth + 1));
      ++index;
    }
    return maxBottom;
  }
  const int16_t height = leafHeight(type, screen.theme());
  fui::Rect rect{x, y, width, height};
  if (strcmp(type, "button") == 0) {
    if (rect.y >= viewportTop && rect.bottom() <= viewportBottom) {
      fui::ButtonProps props;
      props.label = node["label"] | "";
      props.action = actionable ? ACTION_COMPONENT : fui::NO_ACTION;
      props.value = actionIndex;
      props.inputMask = fui::InputTouch;
      props.state = actionable && actionIndex == focused ? fui::StateSelected : fui::StateNormal;
      screen.button(props, rect);
    }
  } else if (strcmp(type, "divider") == 0) {
    if (visible(rect, viewportTop, viewportBottom))
      screen.target().line(fui::Point{x, static_cast<int16_t>(y + height / 2)},
                           fui::Point{static_cast<int16_t>(x + width), static_cast<int16_t>(y + height / 2)}, 1, INK);
  } else if (strcmp(type, "chart") == 0) {
    if (visible(rect, viewportTop, viewportBottom)) chart(screen, node, rect);
  } else if (strcmp(type, "progress") == 0) {
    double number = 0;
    numericValue(resolve(data, node["bind"] | ""), number);
    if (visible(rect, viewportTop, viewportBottom)) {
      const char* label = node["label"] | "";
      screen.target().text(fui::Rect{x, y, width, static_cast<int16_t>(height - 12)}, label, screen.theme().bodyText);
      fui::ProgressBarProps props;
      props.value = static_cast<int>(std::clamp(number, 0.0, 100.0));
      fui::progressBar(screen.frame(), fui::Rect{x, static_cast<int16_t>(y + height - 10), width, 8}, props);
    }
  } else {
    const char* shown = display;
    bool formatted = false;
    if (strcmp(type, "text") == 0 && !(node["bind"].is<const char*>())) {
      shown = node["text"] | "";
      formatted = true;
    } else if (strcmp(type, "text") == 0 && strcmp(node["format"] | "plain", "plain") == 0 &&
               strcmp(node["suffix"] | "", "") == 0) {
      JsonVariantConst bound = resolve(data, node["bind"] | "");
      if (bound.is<const char*>()) {
        shown = bound.as<const char*>();
        formatted = true;
      }
    }
    if (!formatted) formatted = boundDisplay(data, node, display, displayCapacity);
    if (strcmp(type, "metric") == 0) {
      const char* label = node["label"] | "";
      char value[96];
      snprintf(value, sizeof(value), "%s", formatted ? display : tr(STR_FABRIC_VALUE_MISSING));
      snprintf(display, displayCapacity, "%s  %s", label, value);
      formatted = true;
      shown = display;
    }
    if (!formatted) {
      snprintf(display, displayCapacity, "%s", tr(STR_FABRIC_VALUE_MISSING));
      shown = display;
    }
    fui::TextStyle style = screen.theme().bodyText;
    const char* semantic = node["style"] | "body";
    if (strcmp(semantic, "heading") == 0)
      style = screen.theme().titleText;
    else if (strcmp(semantic, "subheading") == 0 || strcmp(semantic, "emphasis") == 0)
      style.bold = true;
    else if (strcmp(semantic, "muted") == 0)
      style = screen.theme().smallText;
    style.maxLines = 2;
    if (visible(rect, viewportTop, viewportBottom)) screen.target().text(rect, shown, style);
  }
  return y + height + screen.theme().spaceSm;
}

void UiRenderer::chart(UiAppHost::UiScreen& screen, JsonVariantConst node, fui::Rect rect) {
  JsonVariantConst series = resolve(data, node["bind"] | "");
  if (!series.is<JsonArrayConst>()) {
    screen.target().text(rect, tr(STR_FABRIC_CHART_UNAVAILABLE), screen.theme().smallText);
    return;
  }
  JsonArrayConst points = series.as<JsonArrayConst>();
  const char* yKey = node["y"] | "";
  const char* xKey = node["x"] | "";
  double minY = INFINITY, maxY = -INFINITY, minX = INFINITY, maxX = -INFINITY;
  size_t count = 0, index = 0;
  for (JsonVariantConst point : points) {
    if (index >= MAX_CHART_POINTS) break;
    double value, xValue;
    if (numericValue(point[yKey], value)) {
      if (!chartX(point[xKey], xValue)) {
        ++index;
        continue;
      }
      minX = std::min(minX, xValue);
      maxX = std::max(maxX, xValue);
      minY = std::min(minY, value);
      maxY = std::max(maxY, value);
      ++count;
    }
    ++index;
  }
  if (!count || !std::isfinite(maxX - minX) || !std::isfinite(maxY - minY)) {
    screen.target().text(rect, tr(STR_FABRIC_CHART_UNAVAILABLE), screen.theme().smallText);
    return;
  }
  screen.target().stroke(rect, INK, 1);
  const double spanX = maxX > minX ? maxX - minX : 1;
  const double spanY = maxY > minY ? maxY - minY : 1;
  fui::Point previous{};
  bool hasPrevious = false;
  index = 0;
  for (JsonVariantConst point : points) {
    if (index >= MAX_CHART_POINTS) break;
    double value, xValue;
    if (numericValue(point[yKey], value)) {
      if (!chartX(point[xKey], xValue)) {
        ++index;
        continue;
      }
      fui::Point current{static_cast<int16_t>(rect.x + 3 + (xValue - minX) * (rect.width - 6) / spanX),
                         static_cast<int16_t>(rect.bottom() - 3 - (value - minY) * (rect.height - 6) / spanY)};
      if (hasPrevious) screen.target().line(previous, current, 2, INK);
      previous = current;
      hasPrevious = true;
    }
    ++index;
  }
}

const char* UiRenderer::findAction(JsonVariantConst node, uint8_t wanted, uint8_t& seen) const {
  if (!node.is<JsonObjectConst>()) return nullptr;
  const char* id = node["id"] | "";
  if (*id && strcmp(node["type"] | "", "button") == 0 && node["action"].is<JsonObjectConst>() && seen++ == wanted)
    return id;
  for (JsonVariantConst child : node["children"].as<JsonArrayConst>()) {
    const char* found = findAction(child, wanted, seen);
    if (found) return found;
  }
  return nullptr;
}

const char* UiRenderer::actionIdAt(uint8_t index) const {
  uint8_t seen = 0;
  return findAction(page["layout"], index, seen);
}
}  // namespace fabric
