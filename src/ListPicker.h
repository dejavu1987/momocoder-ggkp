#ifndef LISTPICKER_H
#define LISTPICKER_H

#include <stdint.h>

// Reusable 4-row list selector for the 64x48 OLED. The wifi page is the
// first consumer; future selectors (themes, keymaps, etc.) can use the
// same primitive by handing it an items array + on-confirm callback.
//
// Two-step interaction model (matches the user's spec):
//   1) Press A/B/C/D -> highlights that row (inverted) but does not commit.
//   2) Press OK     -> commits the highlighted row; caller dispatches by
//                      the item's `kind` field.
// LT/RT paginate (4 items per page); UP/DN are NOT consumed (caller still
// runs page-nav). Pressing on an empty slot is a silent no-op.

constexpr int LIST_PICKER_ROWS = 4;
constexpr uint16_t LIST_PICKER_NO_ACTIVE = 0xFFFF;

struct ListPickerItem {
  const char* label;   // shown after "X. "  e.g. "Home Wifi", "+ Add new..."
  uint8_t     kind;    // domain-specific tag inspected by caller's onConfirm
  uint16_t    userId;  // domain-specific id (e.g. WifiConfigs slot index)
};

struct ListPickerView {
  const ListPickerItem* items;
  uint16_t count;
  uint16_t pageIdx;        // 0..ceil(count/4)-1
  int8_t   highlightSlot;  // -1 = nothing pressed yet, 0..3 = inverted row
  uint16_t activeIdx;      // global item idx, drawn with marker dot;
                           // LIST_PICKER_NO_ACTIVE if none.
};

void listPickerInit(ListPickerView& v, const ListPickerItem* items,
                    uint16_t count, uint16_t activeIdx);

uint16_t listPickerPageCount(const ListPickerView& v);

// Mutators — call on the corresponding button press.
void listPickerOnSlot(ListPickerView& v, uint8_t slot);  // 0..3
void listPickerOnLeft(ListPickerView& v);
void listPickerOnRight(ListPickerView& v);

// Commit the current highlight. Returns global item index, or -1 if no
// highlight or empty slot. Does NOT update activeIdx — caller decides
// (some kinds, like "Add", don't change the persisted active selection).
int32_t listPickerOnOk(const ListPickerView& v);

// Render the picker into the U8g2 buffer and send. Owns the whole 64x48
// screen — caller should NOT clear/send around this.
void listPickerRender(const ListPickerView& v);

#endif // LISTPICKER_H
