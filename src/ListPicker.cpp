#include "ListPicker.h"
#include "Display.h"
#include <stdio.h>

static const char ROW_LETTER[LIST_PICKER_ROWS] = {'A', 'B', 'C', 'D'};

void listPickerInit(ListPickerView& v, const ListPickerItem* items,
                    uint16_t count, uint16_t activeIdx) {
  v.items = items;
  v.count = count;
  v.pageIdx = 0;
  v.highlightSlot = -1;
  v.activeIdx = activeIdx;
  // Auto-jump page so the active item is visible on first render.
  if (activeIdx != LIST_PICKER_NO_ACTIVE && activeIdx < count) {
    v.pageIdx = activeIdx / LIST_PICKER_ROWS;
  }
}

uint16_t listPickerPageCount(const ListPickerView& v) {
  if (v.count == 0) return 1;
  return (v.count + LIST_PICKER_ROWS - 1) / LIST_PICKER_ROWS;
}

void listPickerOnSlot(ListPickerView& v, uint8_t slot) {
  if (slot >= LIST_PICKER_ROWS) return;
  uint32_t globalIdx = (uint32_t)v.pageIdx * LIST_PICKER_ROWS + slot;
  if (globalIdx >= v.count) return;  // pressed empty slot — no-op
  v.highlightSlot = (int8_t)slot;
}

void listPickerOnLeft(ListPickerView& v) {
  if (v.pageIdx == 0) return;
  v.pageIdx--;
  v.highlightSlot = -1;
}

void listPickerOnRight(ListPickerView& v) {
  if (v.pageIdx + 1 >= listPickerPageCount(v)) return;
  v.pageIdx++;
  v.highlightSlot = -1;
}

int32_t listPickerOnOk(const ListPickerView& v) {
  if (v.highlightSlot < 0) return -1;
  uint32_t globalIdx = (uint32_t)v.pageIdx * LIST_PICKER_ROWS + v.highlightSlot;
  if (globalIdx >= v.count) return -1;
  return (int32_t)globalIdx;
}

void listPickerRender(const ListPickerView& v) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  for (int slot = 0; slot < LIST_PICKER_ROWS; ++slot) {
    uint32_t globalIdx = (uint32_t)v.pageIdx * LIST_PICKER_ROWS + slot;
    if (globalIdx >= v.count) break;

    const int rowY = slot * 12;        // 0, 12, 24, 36 — each row is 12 px tall
    const int baseline = rowY + 9;     // ~bottom of 6x10 ascender row
    const bool highlighted = (v.highlightSlot == slot);
    const bool active = (globalIdx == v.activeIdx);

    if (highlighted) {
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, rowY, SCREEN_WIDTH, 12);
      u8g2.setDrawColor(0);
    }

    char buf[24];
    snprintf(buf, sizeof(buf), "%c. %s", ROW_LETTER[slot],
             v.items[globalIdx].label ? v.items[globalIdx].label : "");
    u8g2.drawStr(0, baseline, buf);

    if (active) {
      u8g2.drawDisc(SCREEN_WIDTH - 3, rowY + 6, 1);
    }

    if (highlighted) u8g2.setDrawColor(1);
  }

  uint16_t pageCount = listPickerPageCount(v);
  if (pageCount > 1) {
    u8g2.setFont(u8g2_font_4x6_tr);
    char pbuf[8];
    snprintf(pbuf, sizeof(pbuf), "%u/%u",
             (unsigned)(v.pageIdx + 1), (unsigned)pageCount);
    int pw = u8g2.getStrWidth(pbuf);
    u8g2.drawStr(SCREEN_WIDTH - pw, SCREEN_HEIGHT - 1, pbuf);
  }

  u8g2.sendBuffer();
}
