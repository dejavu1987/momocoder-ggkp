#include "Pages.h"
#include "Keypad.h"
#include <BLECombo.h>
#include "Icons.h"
#include "ListPicker.h"
#include "Settings.h"
#include "WifiRemote.h"
#include "Display.h"
#include "WifiPage.h"

// Every page's bindings array is indexed by slot 0..8 in row-major order:
//   slot 0 = A   slot 1 = UP   slot 2 = B
//   slot 3 = LT  slot 4 = OK   slot 5 = RT
//   slot 6 = C   slot 7 = DN   slot 8 = D
// findBinding() exploits this to resolve a button to its binding in O(1).

// Page::Mouse — air mouse with click bindings.
//   A : ESC                UP: nav-prev          B : scroll toggle
//   LT: left click         OK: right click       RT: browser back
//   C : drag toggle        DN: nav-next          D : browser forward
static const Binding mouseBindings[] = {
  {ICON_CIRCLE_X,        {ActionKind::Key,          {.keyPtr = &KEY_ESC}}},
  {ICON_CHEVRON_TOP,     {ActionKind::NavPrev,      {}}},
  {ICON_LOOP,            {ActionKind::ToggleScroll, {}}},
  {ICON_TARGET,          {ActionKind::MouseClick,   {.mouseBtn = MOUSE_LEFT}}},
  {ICON_MENU,            {ActionKind::MouseClick,   {.mouseBtn = MOUSE_RIGHT}}},
  {ICON_ACTION_UNDO,     {ActionKind::MouseClick,   {.mouseBtn = MOUSE_BACK}}},
  {ICON_MOVE,            {ActionKind::ToggleDrag,   {}}},
  {ICON_CHEVRON_BOTTOM,  {ActionKind::NavNext,      {}}},
  {ICON_ACTION_REDO,     {ActionKind::MouseClick,   {.mouseBtn = MOUSE_FORWARD}}},
};

// 'f' is a literal byte — wrap once at file scope so the table can take its
// address. Same trick for any future char-literal bindings.
static const uint8_t KEY_F_LOWER = 'f';

// Page::Media — keyboard / media keys.
//   A : ESC          UP: nav-prev    B : 'f' (fullscreen)
//   LT: left arrow   OK: play/pause  RT: right arrow
//   C : vol down     DN: nav-next    D : vol up
static const Binding mediaBindings[] = {
  {ICON_CIRCLE_X,            {ActionKind::Key,      {.keyPtr   = &KEY_ESC}}},
  {ICON_CHEVRON_TOP,         {ActionKind::NavPrev,  {}}},
  {ICON_FULLSCREEN_ENTER,    {ActionKind::Key,      {.keyPtr   = &KEY_F_LOWER}}},
  {ICON_MEDIA_SKIP_BACKWARD, {ActionKind::Key,      {.keyPtr   = &KEY_LEFT_ARROW}}},
  {ICON_MEDIA_PLAY,          {ActionKind::MediaKey, {.mediaPtr = &KEY_MEDIA_PLAY_PAUSE}}},
  {ICON_MEDIA_SKIP_FORWARD,  {ActionKind::Key,      {.keyPtr   = &KEY_RIGHT_ARROW}}},
  {ICON_VOLUME_LOW,          {ActionKind::MediaKey, {.mediaPtr = &KEY_MEDIA_VOLUME_DOWN}}},
  {ICON_CHEVRON_BOTTOM,      {ActionKind::NavNext,  {}}},
  {ICON_VOLUME_HIGH,         {ActionKind::MediaKey, {.mediaPtr = &KEY_MEDIA_VOLUME_UP}}},
};

// Page::Remote — Wi-Fi HTTP remote (connect-on-press to momoggkp.vercel.app).
// All non-nav buttons fire GET /buttonPress/<name>. UP/DN keep page nav.
//   A : "A"          UP: nav-prev    B : "B"
//   LT: "left"       OK: "ok"        RT: "right"
//   C : "C"          DN: nav-next    D : "D"
static const Binding remoteBindings[] = {
  {ICON_BOOKMARK,        {ActionKind::WifiRequest, {.urlPart = "A"}}},
  {ICON_CHEVRON_TOP,     {ActionKind::NavPrev,     {}}},
  {ICON_BADGE,           {ActionKind::WifiRequest, {.urlPart = "B"}}},
  {ICON_ARROW_LEFT,      {ActionKind::WifiRequest, {.urlPart = "left"}}},
  {ICON_CIRCLE_CHECK,    {ActionKind::WifiRequest, {.urlPart = "ok"}}},
  {ICON_ARROW_RIGHT,     {ActionKind::WifiRequest, {.urlPart = "right"}}},
  {ICON_FLAG,            {ActionKind::WifiRequest, {.urlPart = "C"}}},
  {ICON_CHEVRON_BOTTOM,  {ActionKind::NavNext,     {}}},
  {ICON_TAG,             {ActionKind::WifiRequest, {.urlPart = "D"}}},
};

// Page::Settings — air-mouse tuning + BLE re-pairing. Bottom row icons exist
// but are not drawn: renderPage() replaces the bottom row with a "S:NNN D:NN"
// overlay. Bindings still execute when those buttons are pressed.
//   A : forget bonds      UP: nav-prev          B : cycle brightness
//   LT: sens -10          OK: enter pairing     RT: sens +10
//   C : delay -5          DN: nav-next          D : delay +5
static const Binding settingsBindings[] = {
  {ICON_TRASH,            {ActionKind::ForgetBonds,      {}}},
  {ICON_CHEVRON_TOP,      {ActionKind::NavPrev,          {}}},
  {ICON_SUN,              {ActionKind::CycleBrightness,  {}}},
  {ICON_MINUS,            {ActionKind::AdjustSens,   {.delta = -10}}},
  {ICON_BLUETOOTH,        {ActionKind::EnterPairing, {}}},
  {ICON_PLUS,             {ActionKind::AdjustSens,   {.delta = +10}}},
  {ICON_BOLT,             {ActionKind::AdjustDelay,  {.delta = -5}}},
  {ICON_CHEVRON_BOTTOM,   {ActionKind::NavNext,      {}}},
  {ICON_TIMER,            {ActionKind::AdjustDelay,  {.delta = +5}}},
};

// Page::Wifi — saved Wi-Fi configs as a list-picker. The page itself
// renders the list (see renderPage in main.cpp); these bindings only
// route button presses through the picker's three-action vocabulary
// (Slot, Left/Right paginate, Confirm) plus the universal UP/DN nav.
//   A : row 0 highlight   UP: nav-prev   B : row 1 highlight
//   LT: page back         OK: confirm    RT: page forward
//   C : row 2 highlight   DN: nav-next   D : row 3 highlight
static const Binding wifiBindings[] = {
  {0, {ActionKind::ListPickerSlot,    {.slot = 0}}},
  {0, {ActionKind::NavPrev,           {}}},
  {0, {ActionKind::ListPickerSlot,    {.slot = 1}}},
  {0, {ActionKind::ListPickerLeft,    {}}},
  {0, {ActionKind::ListPickerConfirm, {}}},
  {0, {ActionKind::ListPickerRight,   {}}},
  {0, {ActionKind::ListPickerSlot,    {.slot = 2}}},
  {0, {ActionKind::NavNext,           {}}},
  {0, {ActionKind::ListPickerSlot,    {.slot = 3}}},
};

const PageDef pageDefs[NUM_PAGES] = {
  {Page::Mouse,    mouseBindings,    9},
  {Page::Media,    mediaBindings,    9},
  {Page::Remote,   remoteBindings,   9},
  {Page::Wifi,     wifiBindings,     9},
  {Page::Settings, settingsBindings, 9},
};

int slotForButton(int button) {
  switch (button) {
    case BTN_A:  return 0;
    case BTN_UP: return 1;
    case BTN_B:  return 2;
    case BTN_LT: return 3;
    case BTN_OK: return 4;
    case BTN_RT: return 5;
    case BTN_C:  return 6;
    case BTN_DN: return 7;
    case BTN_D:  return 8;
  }
  return -1;
}

const Binding* findBinding(Page page, int button) {
  int slot = slotForButton(button);
  if (slot < 0) return nullptr;
  const PageDef& def = pageDefs[static_cast<int>(page)];
  if ((uint8_t)slot >= def.count) return nullptr;
  return &def.bindings[slot];
}

// The current page's list-picker view, or nullptr if the page doesn't
// host one. Used by executeAction()'s ListPicker* arms to drive the
// generic primitives without per-page wrapper functions.
static ListPickerView* currentListPickerView() {
  switch (currentPage) {
    case Page::Wifi: return wifiPageGetView();
    default:         return nullptr;
  }
}

// Apply a signed delta to `value`, refusing decrements when already at/below
// `floor`. Saves to NVS only if the value actually changed. Note the floor is
// a gate, not a hard clamp: e.g. with floor=10, value=15, delta=-10 → value=5.
static void adjustClamped(int& value, int delta, int floor) {
  int prev = value;
  if (delta < 0) {
    if (value > floor) value += delta;
  } else {
    value += delta;
  }
  if (value != prev) settingsSave();
}

void executeAction(const Action& a) {
  switch (a.kind) {
  case ActionKind::None:
    break;
  case ActionKind::Key:
    bleCombo.write(*a.p.keyPtr);
    break;
  case ActionKind::MediaKey:
    bleCombo.write(*a.p.mediaPtr);
    break;
  case ActionKind::MouseClick:
    bleCombo.mouseClick(a.p.mouseBtn);
    break;
  case ActionKind::ToggleScroll:
    scrollEnabled = !scrollEnabled;
    break;
  case ActionKind::ToggleDrag:
    dragEnabled = !dragEnabled;
    if (dragEnabled) {
      bleCombo.mousePress(MOUSE_LEFT);
    } else {
      bleCombo.mouseRelease(MOUSE_LEFT);
    }
    break;
  case ActionKind::NavPrev:
    --currentPage;
    break;
  case ActionKind::NavNext:
    ++currentPage;
    break;
  case ActionKind::AdjustSens:
    adjustClamped(mouseSensitivity, a.p.delta, 10);
    break;
  case ActionKind::AdjustDelay:
    adjustClamped(mouseMoveDelay, a.p.delta, 5);
    break;
  case ActionKind::EnterPairing:
    enterPairingMode();
    break;
  case ActionKind::ForgetBonds:
    forgetAllBonds();
    break;
  case ActionKind::WifiRequest:
    wifiRemoteFire(a.p.urlPart);
    break;
  case ActionKind::CycleBrightness:
    displayCycleBrightness();
    break;
  case ActionKind::ListPickerSlot:
    if (auto* v = currentListPickerView()) listPickerOnSlot(*v, a.p.slot);
    break;
  case ActionKind::ListPickerLeft:
    if (auto* v = currentListPickerView()) listPickerOnLeft(*v);
    break;
  case ActionKind::ListPickerRight:
    if (auto* v = currentListPickerView()) listPickerOnRight(*v);
    break;
  case ActionKind::ListPickerConfirm:
    // Confirm dispatches by item kind, so it stays page-specific.
    switch (currentPage) {
      case Page::Wifi: wifiPageOnConfirm(); break;
      default: break;
    }
    break;
  }
}
