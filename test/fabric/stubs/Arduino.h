#pragma once

#include <cstdint>

struct EspStub {
  unsigned getFreeHeap() const { return 100000; }
};
inline EspStub ESP;
