#include "FabricActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fabric/FabricClient.h"

namespace fui = freeink::ui;

FabricActivity::FabricActivity(GfxRenderer& renderer, MappedInputManager& input)
    : Activity("Fabric", renderer, input), UiAppHost(renderer), client(SETTINGS.fabricServerUrl) {}

void FabricActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(fabric::UiRenderer::ACTION_COMPONENT, &FabricActivity::componentFn, this);
  app.on(ACTION_RETRY, &FabricActivity::retryFn, this);
  app.on(ACTION_CONFIG, &FabricActivity::configFn, this);
  app.setScreen(&FabricActivity::screenFn, this);
  requestUpdate();
  if (!SETTINGS.fabricServerUrl[0])
    configure();
  else
    connectOrLoad();
}

void FabricActivity::onExit() {
  closeRouting();
  page.json.clear();
  data.clear();
  pages.clear();
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void FabricActivity::configure() {
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FABRIC_SERVER_URL),
                                                           SETTINGS.fabricServerUrl, 127, InputType::Url);
  if (!keyboard) {
    LOG_ERR("FABRIC", "OOM: URL keyboard");
    showError(fabric::Error::Network);
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      if (!SETTINGS.fabricServerUrl[0]) finish();
      return;
    }
    const auto& url = std::get<KeyboardResult>(result.data).text;
    if (url.size() >= sizeof(SETTINGS.fabricServerUrl)) {
      showError(fabric::Error::MalformedPage);
      return;
    }
    const bool changed = strcmp(SETTINGS.fabricServerUrl, url.c_str()) != 0;
    if (changed) strcpy(SETTINGS.fabricServerUrl, url.c_str());
    if (changed || !urlSaved) {
      urlSaved = SETTINGS.saveToFile();
      if (!urlSaved) {
        showError(fabric::Error::Persistence);
        return;
      }
    }
    connectOrLoad();
  });
}

void FabricActivity::connectOrLoad() {
  if (WiFi.status() == WL_CONNECTED) {
    if (listing)
      loadList();
    else
      loadPage();
    return;
  }
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("FABRIC", "OOM: WiFi selector");
    showError(fabric::Error::Network);
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled || WiFi.status() != WL_CONNECTED)
      showError(fabric::Error::Network);
    else if (listing)
      loadList();
    else
      loadPage();
  });
}

void FabricActivity::showError(fabric::Error next) {
  LOG_ERR("FABRIC", "Error %u on %s", static_cast<unsigned>(next), nav.current());
  error = next;
  requestUpdate();
}

void FabricActivity::loadList() {
  requestUpdateAndWait();
  closeRouting();
  RenderLock lock;
  const fabric::Error next = client.listPages(pages);
  if (next != fabric::Error::None) {
    showError(next);
    return;
  }
  JsonVariantConst root = pages.as<JsonVariantConst>();
  if (!root["pages"].is<JsonArrayConst>()) {
    showError(fabric::Error::MalformedPage);
    return;
  }
  size_t supported = 0;
  for (JsonVariantConst item : root["pages"].as<JsonArrayConst>())
    if (strcmp(item["fabric"] | "", "0.2") == 0 && fabric::validPageId(item["id"] | "")) ++supported;
  if (!supported) {
    showError(fabric::Error::NoPages);
    return;
  }
  listing = true;
  hasPage = false;
  scroll = maxScroll = 0;
  followFocus = true;
  nav.setFocus(0);
  error = fabric::Error::None;
  requestUpdate();
}

void FabricActivity::loadPage(bool useActionData) {
  if (!hasPage && !listing) requestUpdateAndWait();
  closeRouting();
  RenderLock lock;
  hasPage = false;
  const fabric::Error pageError = client.getPage(nav.current(), page);
  if (pageError != fabric::Error::None) {
    data.clear();
    showError(pageError);
    return;
  }
  hasPage = true;
  scroll = maxScroll = 0;
  followFocus = true;
  if (!useActionData && !page.fetchOnOpen) data.clear();
  if (!useActionData && page.fetchOnOpen) {
    const fabric::Error dataError = client.getPageData(nav.current(), data);
    if (dataError != fabric::Error::None) {
      showError(dataError);
      return;
    }
  }
  error = fabric::Error::None;
  lastFetch = millis();
  requestUpdate();
}

void FabricActivity::refresh() {
  if (!hasPage) return;
  RenderLock lock;
  JsonDocument fresh;
  const fabric::Error next = client.getPageData(nav.current(), fresh);
  lastFetch = millis();
  if (next != fabric::Error::None) {
    showError(next);
    return;
  }
  if (data.as<JsonVariantConst>() == fresh.as<JsonVariantConst>()) return;
  data = std::move(fresh);
  requestUpdate();
}

void FabricActivity::activate(uint8_t index) {
  if (listing) {
    uint8_t seen = 0;
    for (JsonVariantConst item : pages["pages"].as<JsonArrayConst>()) {
      if (strcmp(item["fabric"] | "", "0.2") != 0) continue;
      const char* id = item["id"] | "";
      if (!fabric::validPageId(id)) continue;
      if (seen++ == index) {
        closeRouting();
        {
          RenderLock lock;
          if (!nav.open(id)) {
            showError(fabric::Error::MalformedPage);
            return;
          }
          listing = false;
          pages.clear();
        }
        loadPage();
        return;
      }
      if (seen >= 96) break;
    }
    return;
  }
  if (!hasPage) return;
  fabric::UiRenderer ui(page.json.as<JsonVariantConst>(), data.as<JsonVariantConst>(), displayScratch,
                        sizeof(displayScratch));
  const char* id = ui.actionIdAt(index);
  if (!id) return;
  char componentId[65];
  snprintf(componentId, sizeof(componentId), "%s", id);
  JsonDocument result;
  const fabric::Error next = client.executeAction(nav.current(), componentId, result);
  if (next != fabric::Error::None) {
    showError(next);
    return;
  }
  const char* type = result["type"] | "";
  const char* target = result["page_id"] | "";
  const fabric::Transition transition = nav.applyAction(type, target);
  if (transition == fabric::Transition::Invalid) {
    showError(fabric::Error::Action);
    return;
  }
  if (transition == fabric::Transition::Exit) {
    finish();
    return;
  }
  if (transition == fabric::Transition::Open) {
    if (result["data"].is<JsonObjectConst>()) {
      bool copied;
      {
        RenderLock lock;
        data.clear();
        copied = data.set(result["data"]);
      }
      if (!copied) {
        showError(fabric::Error::OutOfMemory);
        return;
      }
      loadPage(true);
    } else
      loadPage();
    return;
  }
  if (result["data"].is<JsonObjectConst>()) {
    bool copied;
    {
      RenderLock lock;
      data.clear();
      copied = data.set(result["data"]);
    }
    if (!copied) {
      showError(fabric::Error::OutOfMemory);
      return;
    }
    lastFetch = millis();
    error = fabric::Error::None;
    requestUpdate();
  } else
    refresh();
}

void FabricActivity::goBack() {
  if (listing || !nav.back()) {
    finish();
    return;
  }
  loadPage();
}

void FabricActivity::moveFocus(int delta) {
  if (!actionCount) return;
  int index = static_cast<int>(nav.focus()) + delta;
  if (index < 0) index = actionCount - 1;
  if (index >= actionCount) index = 0;
  nav.setFocus(index);
  followFocus = true;
  // The next paint measures the selected button's actual position.
  requestUpdate();
}

void FabricActivity::loop() {
  const auto route = routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (pendingConfig) {
    pendingConfig = false;
    configure();
    return;
  }
  if (pendingRetry) {
    pendingRetry = false;
    connectOrLoad();
    return;
  }
  if (pendingComponent >= 0) {
    const int selected = pendingComponent;
    pendingComponent = -1;
    activate(selected);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goBack();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (error != fabric::Error::None)
      connectOrLoad();
    else
      activate(nav.focus());
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
      mappedInput.wasReleased(MappedInputManager::Button::Left))
    moveFocus(-1);
  else if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
           mappedInput.wasReleased(MappedInputManager::Button::Right))
    moveFocus(1);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int step = renderer.getScreenHeight() / 2;
    const int offset = swipe == MappedInputManager::SwipeDir::Up ? step : -step;
    const uint16_t next =
        static_cast<uint16_t>(std::clamp(static_cast<int>(scroll) + offset, 0, static_cast<int>(maxScroll)));
    if (next != scroll) {
      scroll = next;
      followFocus = false;
      requestUpdate();
    }
  }
  if (hasPage && error == fabric::Error::None && page.intervalSeconds &&
      millis() - lastFetch >= page.intervalSeconds * 1000UL)
    refresh();
}

void FabricActivity::screenFn(UiScreen& screen, void* user) { static_cast<FabricActivity*>(user)->drawScreen(screen); }
void FabricActivity::componentFn(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<FabricActivity*>(user);
  if (event.value < 0 || event.value >= self->actionCount) return;
  self->nav.setFocus(event.value);
  self->pendingComponent = event.value;
}
void FabricActivity::retryFn(const fui::ActionEvent&, void* user) {
  static_cast<FabricActivity*>(user)->pendingRetry = true;
}
void FabricActivity::configFn(const fui::ActionEvent&, void* user) {
  static_cast<FabricActivity*>(user)->pendingConfig = true;
}

const char* FabricActivity::errorText() const {
  switch (error) {
    case fabric::Error::Network:
      return tr(STR_FABRIC_NETWORK_ERROR);
    case fabric::Error::InvalidServerUrl:
      return tr(STR_FABRIC_URL_ERROR);
    case fabric::Error::Http:
      return tr(STR_FABRIC_SERVER_ERROR);
    case fabric::Error::InvalidJson:
      return tr(STR_FABRIC_JSON_ERROR);
    case fabric::Error::NoPages:
      return tr(STR_FABRIC_NO_PAGES);
    case fabric::Error::UnsupportedComponent:
      return tr(STR_FABRIC_COMPONENT_ERROR);
    case fabric::Error::MalformedPage:
      return tr(STR_FABRIC_PAGE_ERROR);
    case fabric::Error::Action:
      return tr(STR_FABRIC_ACTION_ERROR);
    case fabric::Error::TooLarge:
      return tr(STR_FABRIC_SIZE_ERROR);
    case fabric::Error::OutOfMemory:
      return tr(STR_MEMORY_ERROR);
    case fabric::Error::Persistence:
      return tr(STR_FABRIC_SAVE_ERROR);
    default:
      return tr(STR_PAGE_LOAD_ERROR);
  }
}

void FabricActivity::drawScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (error != fabric::Error::None) {
    auto area = screen.body();
    const char* message = errorText();
    if (error == fabric::Error::UnsupportedVersion) {
      snprintf(errorBuffer, sizeof(errorBuffer), tr(STR_FABRIC_VERSION_ERROR), page.json["fabric"] | "?");
      message = errorBuffer;
    }
    screen.target().text(screen.takeTop(screen.theme().rowHeight * 2), message, screen.theme().bodyText);
    screen.button(tr(STR_RETRY), ACTION_RETRY);
    screen.button(tr(STR_FABRIC_SERVER_URL), ACTION_CONFIG);
    actionCount = 0;
    return;
  }
  if (listing && pages["pages"].is<JsonArray>()) {
    auto area = screen.body();
    const auto oldClip = screen.target().clipRect();
    screen.target().setClipRect(area);
    actionCount = 0;
    int16_t y = area.y - static_cast<int16_t>(scroll);
    for (JsonVariantConst item : pages["pages"].as<JsonArrayConst>()) {
      if (strcmp(item["fabric"] | "", "0.2") != 0) continue;
      if (!fabric::validPageId(item["id"] | "")) continue;
      if (actionCount >= 96) break;
      fui::Rect rect{area.x, y, area.width, screen.theme().rowHeight};
      if (actionCount == nav.focus()) {
        focusTop = rect.y;
        focusBottom = rect.bottom();
      }
      if (rect.y >= area.y && rect.bottom() <= area.bottom()) {
        fui::ButtonProps props;
        const char* title = item["title"] | "";
        props.label = *title ? title : (item["id"] | "");
        props.action = fabric::UiRenderer::ACTION_COMPONENT;
        props.value = actionCount;
        props.inputMask = fui::InputTouch;
        props.state = actionCount == nav.focus() ? fui::StateSelected : fui::StateNormal;
        screen.button(props, rect);
      }
      ++actionCount;
      y += screen.theme().rowHeight + screen.theme().spaceSm;
    }
    screen.target().setClipRect(oldClip);
    maxScroll = static_cast<uint16_t>(std::max(0, static_cast<int>(scroll) + y - area.bottom()));
    if (scroll > maxScroll) {
      scroll = maxScroll;
      requestUpdate();
    }
    if (followFocus && actionCount && (focusBottom > area.bottom() || focusTop < area.y)) {
      const int adjusted =
          static_cast<int>(scroll) + (focusBottom > area.bottom() ? focusBottom - area.bottom() + screen.theme().spaceSm
                                                                  : focusTop - area.y - screen.theme().spaceSm);
      const uint16_t next = static_cast<uint16_t>(std::clamp(adjusted, 0, static_cast<int>(maxScroll)));
      if (next != scroll) {
        scroll = next;
        requestUpdate();
      }
    }
    return;
  }
  if (!hasPage) {
    screen.target().text(screen.body(), tr(STR_LOADING_POPUP), screen.theme().bodyText);
    return;
  }
  fabric::UiRenderer ui(page.json.as<JsonVariantConst>(), data.as<JsonVariantConst>(), displayScratch,
                        sizeof(displayScratch));
  ui.render(screen, nav.focus(), scroll);
  actionCount = ui.actionCount();
  focusTop = ui.focusedTop();
  focusBottom = ui.focusedBottom();
  const auto area = screen.body();
  maxScroll = static_cast<uint16_t>(std::max(0, static_cast<int>(scroll) + ui.contentBottom() - area.bottom()));
  if (scroll > maxScroll) {
    scroll = maxScroll;
    requestUpdate();
  }
  if (followFocus && actionCount &&
      (focusTop < area.y || (focusBottom - focusTop <= area.height && focusBottom > area.bottom()))) {
    const int adjusted =
        static_cast<int>(scroll) + (focusBottom > area.bottom() ? focusBottom - area.bottom() + screen.theme().spaceSm
                                                                : focusTop - area.y - screen.theme().spaceSm);
    const uint16_t next = static_cast<uint16_t>(std::clamp(adjusted, 0, static_cast<int>(maxScroll)));
    if (next != scroll) {
      scroll = next;
      requestUpdate();
    }
  }
}

void FabricActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 hasPage ? page.title : tr(STR_FABRIC));
  renderUi();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  LOG_DBG("FABRIC", "after render: free=%u largest=%u", ESP.getFreeHeap(),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}
