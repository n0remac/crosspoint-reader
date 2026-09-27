#!/usr/bin/env bash
set -euo pipefail
c++ -std=c++20 -O2 -I.pio/libdeps/default/ArduinoJson/src -Isrc \
  test/fabric/host.cpp src/fabric/FabricPage.cpp src/fabric/FabricBinding.cpp src/fabric/FabricNavigator.cpp \
  -o /tmp/crosspoint-fabric-host-test
/tmp/crosspoint-fabric-host-test
