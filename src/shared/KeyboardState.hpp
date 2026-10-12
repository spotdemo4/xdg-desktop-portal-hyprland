#pragma once

#include <xkbcommon/xkbcommon.h>
#include <cstdint>
#include <memory>
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

  private:
    std::unique_ptr<xkb_state, decltype(&xkb_state_unref)> m_state{nullptr, xkb_state_unref};
    std::unordered_set<uint32_t> m_pressed;
};
