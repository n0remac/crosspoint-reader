#pragma once

constexpr int WL_CONNECTED = 3;
struct WiFiStub {
  int status() const { return WL_CONNECTED; }
};
inline WiFiStub WiFi;
