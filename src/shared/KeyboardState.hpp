#pragma once

#include <xkbcommon/xkbcommon.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

// Track modifiers using the compositor's keymap, rather than fixed masks.
class KeyboardState {
  public:
    bool setKeymap(const char* text) {
        std::unique_ptr<xkb_context, decltype(&xkb_context_unref)> context(
            xkb_context_new(XKB_CONTEXT_NO_FLAGS), xkb_context_unref);
        if (!context)
            return false;
        std::unique_ptr<xkb_keymap, decltype(&xkb_keymap_unref)> map(
            xkb_keymap_new_from_string(context.get(), text, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS), xkb_keymap_unref);
        if (!map)
            return false;
        std::unique_ptr<xkb_state, decltype(&xkb_state_unref)> state(xkb_state_new(map.get()), xkb_state_unref);
        if (!state)
            return false;
        m_state = std::move(state);
        m_pressed.clear();
        return true;
    }

    void key(uint32_t code, bool pressed) {
        if (!m_state)
            return;
        // Repeated presses must not increment XKB's modifier reference count.
        const bool changed = pressed ? m_pressed.insert(code).second : m_pressed.erase(code) != 0;
        if (changed)
            xkb_state_update_key(m_state.get(), code + 8, pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
    }

    std::vector<uint32_t> releaseAll() {
        std::vector<uint32_t> keys(m_pressed.begin(), m_pressed.end());
        for (auto code : keys)
            key(code, false);
        return keys;
    }

    uint32_t modifiers(xkb_state_component component) const {
        return m_state ? xkb_state_serialize_mods(m_state.get(), component) : 0;
    }

    uint32_t group() const {
        return m_state ? xkb_state_serialize_layout(m_state.get(), XKB_STATE_LAYOUT_EFFECTIVE) : 0;
    }

    uint32_t character(uint32_t code) const {
        return m_state ? xkb_state_key_get_utf32(m_state.get(), code + 8) : 0;
    }

    struct SKeyForSym {
        uint32_t       code = 0;
        xkb_mod_mask_t mods = 0;
    };

    // The evdev key that types `sym` in the current layout, and the modifiers its level
    // takes. Keys typed with fewer levels win, so 'a' comes without Shift.
    std::optional<SKeyForSym> keyFor(xkb_keysym_t sym) const {
        if (!m_state)
            return std::nullopt;
        const auto  KEYMAP = xkb_state_get_keymap(m_state.get());
        const auto  LAYOUT = group();
        const auto  MIN = xkb_keymap_min_keycode(KEYMAP), MAX = xkb_keymap_max_keycode(KEYMAP);
        xkb_level_index_t levels = 1;
        for (xkb_keycode_t code = MIN; code <= MAX; code++)
            levels = std::max(levels, xkb_keymap_num_levels_for_key(KEYMAP, code, LAYOUT));
        for (xkb_level_index_t level = 0; level < levels; level++) {
            for (xkb_keycode_t code = std::max<xkb_keycode_t>(MIN, 8); code <= MAX; code++) {
                const xkb_keysym_t* syms  = nullptr;
                const int           count = xkb_keymap_key_get_syms_by_level(KEYMAP, code, LAYOUT, level, &syms);
                for (int i = 0; i < count; i++) {
                    if (syms[i] != sym)
                        continue;
                    xkb_mod_mask_t masks[4] = {};
                    const auto     MASKS    = xkb_keymap_key_get_mods_for_level(KEYMAP, code, LAYOUT, level, masks, 4);
                    return SKeyForSym{.code = code - 8, .mods = MASKS > 0 ? masks[0] : 0};
                }
            }
        }
        return std::nullopt;
    }

  private:
    std::unique_ptr<xkb_state, decltype(&xkb_state_unref)> m_state{nullptr, xkb_state_unref};
    std::unordered_set<uint32_t> m_pressed;
};
