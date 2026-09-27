#pragma once

#include <cstdint>

namespace fabric {
enum class Transition : uint8_t { Stay, Open, Exit, Invalid };

class Navigator {
 public:
  bool open(const char* id);
  bool back();
  Transition applyAction(const char* type, const char* pageId);
  const char* current() const { return currentId; }
  uint8_t depth() const { return historyCount; }
  uint8_t focus() const { return focused; }
  void setFocus(uint8_t index) { focused = index; }
  uint8_t viewport() const { return top; }
  void setViewport(uint8_t index) { top = index; }

 private:
  char currentId[65]{};
  char history[8][65]{};
  uint8_t historyCount = 0;
  uint8_t focused = 0;
  uint8_t top = 0;
};
}  // namespace fabric
