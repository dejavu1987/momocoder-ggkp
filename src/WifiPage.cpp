#include "WifiPage.h"
#include "WifiConfigs.h"
#include "WifiSetup.h"     // forward — full impl in Task 6
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// Item layout (when configs exist):
//   slot 0..N-1  -> Saved       (label = SSID truncated)
//   slot N       -> Add         (label = "+ Add new...")
//   slot N+1     -> Delete      (label = "Delete current") -- only if active
//
// When configs is empty: just one Add row.
static ListPickerItem items[WIFI_MAX_CONFIGS + 2];
static char           labels[WIFI_MAX_CONFIGS][16];  // SSID truncations
static uint16_t       itemCount = 0;
static ListPickerView view;

static void buildItems() {
  itemCount = 0;
  uint8_t n = wifiConfigsCount();
  int8_t  active = wifiConfigsActiveIdx();
  for (uint8_t i = 0; i < n; ++i) {
    const WifiConfig* c = wifiConfigsGet(i);
    if (!c) continue;
    strncpy(labels[i], c->ssid, sizeof(labels[i]) - 1);
    labels[i][sizeof(labels[i]) - 1] = 0;
    items[itemCount++] = {labels[i], (uint8_t)WifiItemKind::Saved, i};
  }
  items[itemCount++] = {"+Add new...", (uint8_t)WifiItemKind::Add, 0};
  if (active >= 0) {
    items[itemCount++] = {"Delete current", (uint8_t)WifiItemKind::Delete, 0};
  }
}

void wifiPageRefresh() {
  buildItems();
  uint16_t activeIdx = LIST_PICKER_NO_ACTIVE;
  int8_t a = wifiConfigsActiveIdx();
  if (a >= 0) activeIdx = (uint16_t)a;
  listPickerInit(view, items, itemCount, activeIdx);
}

ListPickerView* wifiPageGetView() { return &view; }

void wifiPageOnConfirm() {
  int32_t idx = listPickerOnOk(view);
  if (idx < 0) return;
  const ListPickerItem& it = items[idx];
  switch ((WifiItemKind)it.kind) {
    case WifiItemKind::Saved: {
      wifiConfigsSetActive((int8_t)it.userId);
      wifiPageRefresh();
      break;
    }
    case WifiItemKind::Add: {
      wifiSetupBegin();
      break;
    }
    case WifiItemKind::Delete: {
      wifiConfigsDeleteActive();
      wifiPageRefresh();
      break;
    }
  }
}

void wifiPageRender() {
  listPickerRender(view);
}

WifiPageDigest wifiPageGetDigest() {
  return {view.pageIdx, view.highlightSlot, view.activeIdx, view.count};
}
