#ifndef _FCITX5_PLOVER_KEYSYMS_H_
#define _FCITX5_PLOVER_KEYSYMS_H_

// Single source of truth for which keys this plugin recognizes, shared
// between the fcitx5 addon (ploverengine.cpp, which only needs to know
// whether a keysym is handled at all) and the Plover-side native extension
// (bridge.cpp, which also needs the Plover key name for key_down()/
// key_up(), and the reverse direction for suppress()/send_key_combination()).
// Previously this table was hand-duplicated between C++ (as a bare keysym
// set) and Python (as a keysym-name-string -> Plover-key-name dict); now
// there is exactly one copy of it.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/keysymgen.h>

namespace fcitx5plover {

struct HandledKey {
    // The name Plover's Capture.key_down()/key_up()/suppress() know this
    // key by -- Plover's own steno keymap vocabulary (see
    // Keyboard.KEYS_LAYOUT), *not* an X11 keysym name: they agree for
    // letters/digits/function keys/space/BackSpace/etc, but not punctuation
    // ("`" here vs. fcitx::Key::keySymToString(FcitxKey_grave) == "grave").
    // send_key_combination() needs the X11 keysym name instead, for
    // portability with dictionaries already written for the X11/uinput
    // backends -- see native.cpp's keyNameLookup(), which derives it from
    // `keysym` below via keySymToString rather than using ploverName.
    const char *ploverName;
    uint32_t keysym;
};

// clang-format off
inline constexpr HandledKey kHandledKeys[] = {
    {"F1", FcitxKey_F1},     {"F2", FcitxKey_F2},     {"F3", FcitxKey_F3},
    {"F4", FcitxKey_F4},     {"F5", FcitxKey_F5},     {"F6", FcitxKey_F6},
    {"F7", FcitxKey_F7},     {"F8", FcitxKey_F8},     {"F9", FcitxKey_F9},
    {"F10", FcitxKey_F10},   {"F11", FcitxKey_F11},   {"F12", FcitxKey_F12},
    {"`", FcitxKey_grave},   {"0", FcitxKey_0},       {"1", FcitxKey_1},
    {"2", FcitxKey_2},       {"3", FcitxKey_3},       {"4", FcitxKey_4},
    {"5", FcitxKey_5},       {"6", FcitxKey_6},       {"7", FcitxKey_7},
    {"8", FcitxKey_8},       {"9", FcitxKey_9},       {"-", FcitxKey_minus},
    {"=", FcitxKey_equal},   {"q", FcitxKey_q},       {"w", FcitxKey_w},
    {"e", FcitxKey_e},       {"r", FcitxKey_r},       {"t", FcitxKey_t},
    {"y", FcitxKey_y},       {"u", FcitxKey_u},       {"i", FcitxKey_i},
    {"o", FcitxKey_o},       {"p", FcitxKey_p},       {"[", FcitxKey_bracketleft},
    {"]", FcitxKey_bracketright},                     {"\\", FcitxKey_backslash},
    {"a", FcitxKey_a},       {"s", FcitxKey_s},       {"d", FcitxKey_d},
    {"f", FcitxKey_f},       {"g", FcitxKey_g},       {"h", FcitxKey_h},
    {"j", FcitxKey_j},       {"k", FcitxKey_k},       {"l", FcitxKey_l},
    {";", FcitxKey_semicolon},                        {"'", FcitxKey_apostrophe},
    {"z", FcitxKey_z},       {"x", FcitxKey_x},       {"c", FcitxKey_c},
    {"v", FcitxKey_v},       {"b", FcitxKey_b},       {"n", FcitxKey_n},
    {"m", FcitxKey_m},       {",", FcitxKey_comma},   {".", FcitxKey_period},
    {"/", FcitxKey_slash},   {"space", FcitxKey_space},
    {"BackSpace", FcitxKey_BackSpace},                {"Delete", FcitxKey_Delete},
    {"Down", FcitxKey_Down}, {"End", FcitxKey_End},   {"Escape", FcitxKey_Escape},
    {"Home", FcitxKey_Home}, {"Left", FcitxKey_Left},
    {"Page_Down", FcitxKey_Page_Down},                {"Page_Up", FcitxKey_Page_Up},
    {"Return", FcitxKey_Return},                      {"Right", FcitxKey_Right},
    {"Tab", FcitxKey_Tab},   {"Up", FcitxKey_Up},
};

inline constexpr uint32_t kModifierKeysyms[] = {
    FcitxKey_Control_L, FcitxKey_Control_R, FcitxKey_Shift_L,
    FcitxKey_Shift_R,   FcitxKey_Alt_L,     FcitxKey_Alt_R,
    FcitxKey_Super_L,   FcitxKey_Super_R,
};
// clang-format on

// fcitx::KeyState bit values for the modifiers above -- send_key_combination()
// needs these to build ForwardKey's `state` bitmask as each modifier in the
// combo is pressed/released. Real values from fcitx-utils/keysym.h, not
// hand-copied magic numbers.
inline uint32_t modifierMaskFor(uint32_t keysym) {
    switch (keysym) {
    case FcitxKey_Control_L:
    case FcitxKey_Control_R:
        return static_cast<uint32_t>(fcitx::KeyState::Ctrl);
    case FcitxKey_Shift_L:
    case FcitxKey_Shift_R:
        return static_cast<uint32_t>(fcitx::KeyState::Shift);
    case FcitxKey_Alt_L:
    case FcitxKey_Alt_R:
        return static_cast<uint32_t>(fcitx::KeyState::Alt);
    case FcitxKey_Super_L:
    case FcitxKey_Super_R:
        return static_cast<uint32_t>(fcitx::KeyState::Super);
    default:
        return 0;
    }
}

inline const std::unordered_set<uint32_t> &handledKeysymSet() {
    static const std::unordered_set<uint32_t> keysyms = [] {
        std::unordered_set<uint32_t> result;
        for (const auto &key : kHandledKeys) {
            result.insert(key.keysym);
        }
        return result;
    }();
    return keysyms;
}

inline const std::unordered_set<uint32_t> &modifierKeysymSet() {
    static const std::unordered_set<uint32_t> keysyms(
        std::begin(kModifierKeysyms), std::end(kModifierKeysyms));
    return keysyms;
}

// keysym -> Plover key name, for translating incoming KeyDown/KeyUp
// notifications (bridge.cpp's counterpart to the addon's kHandledKeysyms).
inline const std::unordered_map<uint32_t, std::string> &keysymToPloverName() {
    static const std::unordered_map<uint32_t, std::string> table = [] {
        std::unordered_map<uint32_t, std::string> result;
        for (const auto &key : kHandledKeys) {
            result.emplace(key.keysym, key.ploverName);
        }
        return result;
    }();
    return table;
}

inline std::optional<std::string> ploverNameForKeysym(uint32_t keysym) {
    const auto &table = keysymToPloverName();
    auto it = table.find(keysym);
    if (it == table.end()) {
        return std::nullopt;
    }
    return it->second;
}

// Plover key name (exactly HandledKey::ploverName, e.g. "`"/"BackSpace",
// *not* an X11 keysym name -- Plover's Keyboard machine binds its steno
// keymap to this same vocabulary, via whatever key_down()/key_up() already
// report) -> keysym, for suppress(): Capture.suppress()'s suppressed_keys
// come straight from Keyboard._bindings.keys(), i.e. this exact vocabulary.
//
// Deliberately *not* used for send_key_combination(): that one needs X11
// keysym names instead ("comma", not ","), to match the vocabulary the
// X11/uinput backends already give combo strings in existing dictionaries
// -- see native.cpp's keyNameLookup().
inline const std::unordered_map<std::string, uint32_t> &ploverNameToKeysym() {
    static const std::unordered_map<std::string, uint32_t> table = [] {
        std::unordered_map<std::string, uint32_t> result;
        for (const auto &key : kHandledKeys) {
            result.emplace(key.ploverName, key.keysym);
        }
        return result;
    }();
    return table;
}

inline std::optional<uint32_t> keysymForPloverName(const std::string &name) {
    const auto &table = ploverNameToKeysym();
    auto it = table.find(name);
    if (it == table.end()) {
        return std::nullopt;
    }
    return it->second;
}

} // namespace fcitx5plover

#endif // _FCITX5_PLOVER_KEYSYMS_H_
