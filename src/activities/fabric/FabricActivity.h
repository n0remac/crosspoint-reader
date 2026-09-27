#pragma once

#include <ArduinoJson.h>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "fabric/FabricClient.h"
#include "fabric/FabricNavigator.h"
#include "fabric/FabricPage.h"
#include "fabric/FabricUiRenderer.h"

class FabricActivity final : public Activity, private UiAppHost {
 public:
  FabricActivity(GfxRenderer& renderer, MappedInputManager& input);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  static constexpr freeink::ui::ActionId ACTION_RETRY = 2;
  static constexpr freeink::ui::ActionId ACTION_CONFIG = 3;
  static void screenFn(UiScreen& screen, void* user);
  static void componentFn(const freeink::ui::ActionEvent& event, void* user);
  static void retryFn(const freeink::ui::ActionEvent&, void* user);
  static void configFn(const freeink::ui::ActionEvent&, void* user);
  void drawScreen(UiScreen& screen);
  void configure();
  void connectOrLoad();
  void loadList();
  void loadPage(bool useActionData = false);
  void refresh();
  void activate(uint8_t index);
  void goBack();
  void moveFocus(int delta);
  void showError(fabric::Error error);
  const char* errorText() const;

  fabric::Client client;
  fabric::Navigator nav;
  fabric::Page page;
  JsonDocument data;
  JsonDocument pages;
  fabric::Error error = fabric::Error::None;
  uint32_t lastFetch = 0;
  uint16_t scroll = 0;
  uint16_t maxScroll = 0;
  bool followFocus = true;
  uint8_t actionCount = 0;
  int16_t focusTop = 0;
  int16_t focusBottom = 0;
  bool hasPage = false;
  bool listing = true;
  bool urlSaved = true;
  int pendingComponent = -1;
  bool pendingRetry = false;
  bool pendingConfig = false;
  char errorBuffer[160]{};
  char displayScratch[256]{};
};
