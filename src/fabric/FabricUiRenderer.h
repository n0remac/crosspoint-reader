#pragma once

#include <ArduinoJson.h>
#include <FreeInkApp.h>

#include <cstddef>
#include <cstdint>

#include "components/UiAppHost.h"

namespace fabric {
class UiRenderer {
 public:
  static constexpr freeink::ui::ActionId ACTION_COMPONENT = 1;
  UiRenderer(JsonVariantConst page, JsonVariantConst data, char* scratch, size_t scratchSize)
      : page(page), data(data), display(scratch), displayCapacity(scratchSize) {}
  void render(UiAppHost::UiScreen& screen, uint8_t focused, uint16_t scroll);
  const char* actionIdAt(uint8_t index) const;
  uint8_t actionCount() const { return actions; }
  int16_t focusedTop() const { return focusTop; }
  int16_t focusedBottom() const { return focusBottom; }
  int16_t contentBottom() const { return bottom; }

 private:
  JsonVariantConst page;
  JsonVariantConst data;
  uint8_t actions = 0;
  int16_t focusTop = 0;
  int16_t focusBottom = 0;
  int16_t bottom = 0;
  int16_t viewportTop = 0;
  int16_t viewportBottom = 0;
  char* display;
  size_t displayCapacity;

  int16_t measureNode(const freeink::ui::ThemeTokens& theme, JsonVariantConst node, uint8_t depth) const;
  int16_t renderNode(UiAppHost::UiScreen& screen, JsonVariantConst node, int16_t x, int16_t y, int16_t width,
                     uint8_t focused, uint8_t depth);
  void chart(UiAppHost::UiScreen& screen, JsonVariantConst node, freeink::ui::Rect rect);
  const char* findAction(JsonVariantConst node, uint8_t wanted, uint8_t& seen) const;
};
}  // namespace fabric
