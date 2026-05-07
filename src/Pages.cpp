#include "Pages.h"
#include "Keypad.h"
#include <BLECombo.h>
#include "Icons.h"
#include "WifiRemote.h"
#include "Display.h"
#include "WifiPage.h"

// Page::Mouse — air mouse with click bindings.
//   A : ESC                UP: nav-prev          B : scroll toggle
//   LT: left click         OK: right click       RT: browser back
//   C : drag toggle        DN: nav-next          D : browser forward
static const Binding mouseBindings[] = {
  {BTN_A,  ICON_CIRCLE_X,        {ActionKind::Key,          {.keyPtr = &KEY_ESC}}},
  {BTN_UP, ICON_CHEVRON_TOP,     {ActionKind::NavPrev,      {}}},
  {BTN_B,  ICON_LOOP,            {ActionKind::ToggleScroll, {}}},
  {BTN_LT, ICON_TARGET,          {ActionKind::MouseClick,   {.mouseBtn = MOUSE_LEFT}}},
  {BTN_OK, ICON_MENU,            {ActionKind::MouseClick,   {.mouseBtn = MOUSE_RIGHT}}},
  {BTN_RT, ICON_ACTION_UNDO,     {ActionKind::MouseClick,   {.mouseBtn = MOUSE_BACK}}},
  {BTN_C,  ICON_MOVE,            {ActionKind::ToggleDrag,   {}}},
  {BTN_DN, ICON_CHEVRON_BOTTOM,  {ActionKind::NavNext,      {}}},
  {BTN_D,  ICON_ACTION_REDO,     {ActionKind::MouseClick,   {.mouseBtn = MOUSE_FORWARD}}},
};

// 'f' is a literal byte — wrap once at file scope so the table can take its
// address. Same trick for any future char-literal bindings.
static const uint8_t KEY_F_LOWER = 'f';

// Page::Media — keyboard / media keys.
//   A : ESC          UP: nav-prev    B : 'f' (fullscreen)
//   LT: left arrow   OK: play/pause  RT: right arrow
//   C : vol down     DN: nav-next    D : vol up
static const Binding mediaBindings[] = {
  {BTN_A,  ICON_CIRCLE_X,            {ActionKind::Key,      {.keyPtr   = &KEY_ESC}}},
  {BTN_UP, ICON_CHEVRON_TOP,         {ActionKind::NavPrev,  {}}},
  {BTN_B,  ICON_FULLSCREEN_ENTER,    {ActionKind::Key,      {.keyPtr   = &KEY_F_LOWER}}},
  {BTN_LT, ICON_MEDIA_SKIP_BACKWARD, {ActionKind::Key,      {.keyPtr   = &KEY_LEFT_ARROW}}},
  {BTN_OK, ICON_MEDIA_PLAY,          {ActionKind::MediaKey, {.mediaPtr = &KEY_MEDIA_PLAY_PAUSE}}},
  {BTN_RT, ICON_MEDIA_SKIP_FORWARD,  {ActionKind::Key,      {.keyPtr   = &KEY_RIGHT_ARROW}}},
  {BTN_C,  ICON_VOLUME_LOW,          {ActionKind::MediaKey, {.mediaPtr = &KEY_MEDIA_VOLUME_DOWN}}},
  {BTN_DN, ICON_CHEVRON_BOTTOM,      {ActionKind::NavNext,  {}}},
  {BTN_D,  ICON_VOLUME_HIGH,         {ActionKind::MediaKey, {.mediaPtr = &KEY_MEDIA_VOLUME_UP}}},
};

// Page::Remote — Wi-Fi HTTP remote (connect-on-press to momoggkp.vercel.app).
// All non-nav buttons fire GET /buttonPress/<name>. UP/DN keep page nav.
//   A : "A"          UP: nav-prev    B : "B"
//   LT: "left"       OK: "ok"        RT: "right"
//   C : "C"          DN: nav-next    D : "D"
static const Binding remoteBindings[] = {
  {BTN_A,  ICON_BOOKMARK,        {ActionKind::WifiRequest, {.urlPart = "A"}}},
  {BTN_UP, ICON_CHEVRON_TOP,     {ActionKind::NavPrev,     {}}},
  {BTN_B,  ICON_BADGE,           {ActionKind::WifiRequest, {.urlPart = "B"}}},
  {BTN_LT, ICON_ARROW_LEFT,      {ActionKind::WifiRequest, {.urlPart = "left"}}},
  {BTN_OK, ICON_CIRCLE_CHECK,    {ActionKind::WifiRequest, {.urlPart = "ok"}}},
  {BTN_RT, ICON_ARROW_RIGHT,     {ActionKind::WifiRequest, {.urlPart = "right"}}},
  {BTN_C,  ICON_FLAG,            {ActionKind::WifiRequest, {.urlPart = "C"}}},
  {BTN_DN, ICON_CHEVRON_BOTTOM,  {ActionKind::NavNext,     {}}},
  {BTN_D,  ICON_TAG,             {ActionKind::WifiRequest, {.urlPart = "D"}}},
};

// Page::Settings — air-mouse tuning + BLE re-pairing. Bottom row icons exist
// but are not drawn: renderPage() replaces the bottom row with a "S:NNN D:NN"
// overlay. Bindings still execute when those buttons are pressed.
//   A : forget bonds      UP: nav-prev          B : (none)
//   LT: sens -10          OK: enter pairing     RT: sens +10
//   C : delay -5          DN: nav-next          D : delay +5
static const Binding settingsBindings[] = {
  {BTN_A,  ICON_TRASH,            {ActionKind::ForgetBonds,      {}}},
  {BTN_UP, ICON_CHEVRON_TOP,      {ActionKind::NavPrev,          {}}},
  {BTN_B,  ICON_SUN,              {ActionKind::CycleBrightness,  {}}},
  {BTN_LT, ICON_MINUS,            {ActionKind::AdjustSens,   {.delta = -10}}},
  {BTN_OK, ICON_BLUETOOTH,        {ActionKind::EnterPairing, {}}},
  {BTN_RT, ICON_PLUS,             {ActionKind::AdjustSens,   {.delta = +10}}},
  {BTN_C,  ICON_BOLT,             {ActionKind::AdjustDelay,  {.delta = -5}}},
  {BTN_DN, ICON_CHEVRON_BOTTOM,   {ActionKind::NavNext,      {}}},
  {BTN_D,  ICON_TIMER,            {ActionKind::AdjustDelay,  {.delta = +5}}},
};

// Page::Wifi — saved Wi-Fi configs as a list-picker. The page itself
// renders the list (see renderPage in main.cpp); these bindings only
// route button presses through the picker's three-action vocabulary
// (Slot, Left/Right paginate, Confirm) plus the universal UP/DN nav.
//   A : row 0 highlight   UP: nav-prev   B : row 1 highlight
//   LT: page back         OK: confirm    RT: page forward
//   C : row 2 highlight   DN: nav-next   D : row 3 highlight
static const Binding wifiBindings[] = {
  {BTN_A,  0, {ActionKind::ListPickerSlot,    {.slot = 0}}},
  {BTN_UP, 0, {ActionKind::NavPrev,           {}}},
  {BTN_B,  0, {ActionKind::ListPickerSlot,    {.slot = 1}}},
  {BTN_LT, 0, {ActionKind::ListPickerLeft,    {}}},
  {BTN_OK, 0, {ActionKind::ListPickerConfirm, {}}},
  {BTN_RT, 0, {ActionKind::ListPickerRight,   {}}},
  {BTN_C,  0, {ActionKind::ListPickerSlot,    {.slot = 2}}},
  {BTN_DN, 0, {ActionKind::NavNext,           {}}},
  {BTN_D,  0, {ActionKind::ListPickerSlot,    {.slot = 3}}},
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
  const PageDef& def = pageDefs[static_cast<int>(page)];
  for (uint8_t i = 0; i < def.count; ++i) {
    if (def.bindings[i].button == button) return &def.bindings[i];
  }
  return nullptr;
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
    if (a.p.delta < 0) {
      if (mouseSensitivity > 10) mouseSensitivity += a.p.delta;
    } else {
      mouseSensitivity += a.p.delta;
    }
    break;
  case ActionKind::AdjustDelay:
    if (a.p.delta < 0) {
      if (mouseMoveDelay > 5) mouseMoveDelay += a.p.delta;
    } else {
      mouseMoveDelay += a.p.delta;
    }
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
    switch (currentPage) {
      case Page::Wifi: wifiPageOnSlot(a.p.slot); break;
      default: break;
    }
    break;
  case ActionKind::ListPickerLeft:
    switch (currentPage) {
      case Page::Wifi: wifiPageOnLeft(); break;
      default: break;
    }
    break;
  case ActionKind::ListPickerRight:
    switch (currentPage) {
      case Page::Wifi: wifiPageOnRight(); break;
      default: break;
    }
    break;
  case ActionKind::ListPickerConfirm:
    switch (currentPage) {
      case Page::Wifi: wifiPageOnConfirm(); break;
      default: break;
    }
    break;
  }
}
