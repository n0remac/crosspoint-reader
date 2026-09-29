#include "FabricNavigator.h"

#include <cstring>

#include "FabricPage.h"

namespace fabric {
void RecoveryMenu::move(int delta, bool hasDefault) {
  const int count = hasDefault ? 3 : 2;
  const int next = (static_cast<int>(selectedAction) + delta) % count;
  selectedAction = static_cast<RecoveryAction>(next < 0 ? next + count : next);
}

bool Navigator::open(const char* id) {
  if (!validPageId(id)) return false;
  if (strcmp(currentId, id) == 0) return true;
  if (historyCount && strcmp(history[historyCount - 1], id) == 0) return back();
  if (*currentId) {
    if (historyCount == 8) {
      memmove(history, history + 1, sizeof(history) - sizeof(history[0]));
      --historyCount;
    }
    strcpy(history[historyCount++], currentId);
  }
  strcpy(currentId, id);
  focused = top = 0;
  return true;
}

Transition Navigator::applyAction(const char* type, const char* pageId) {
  if (!type) return Transition::Invalid;
  if (strcmp(type, "back") == 0) return back() ? Transition::Open : Transition::Exit;
  if (strcmp(type, "navigate") && strcmp(type, "invoke") && strcmp(type, "refresh")) return Transition::Invalid;
  if ((!pageId || !*pageId) && strcmp(type, "navigate") == 0) return Transition::Invalid;
  if (!pageId || !*pageId || strcmp(currentId, pageId) == 0) return Transition::Stay;
  return open(pageId) ? Transition::Open : Transition::Invalid;
}

bool Navigator::back() {
  if (!historyCount) return false;
  strcpy(currentId, history[--historyCount]);
  focused = top = 0;
  return true;
}
}  // namespace fabric
