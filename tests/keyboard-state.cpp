#include "../src/shared/KeyboardState.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <linux/input-event-codes.h>

int main() {
    auto* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    assert(context);
    xkb_rule_names names{};
    names.layout = "fr";
    names.options = "compose:caps,shift:both_capslock_cancel";
    auto* map = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    assert(map);
    char* text = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1);
    assert(text);
    KeyboardState state;
    assert(state.setKeymap(text));
    free(text);
    assert(state.character(KEY_Q) == 'a');
    assert(state.character(KEY_A) == 'q');
    assert(state.character(KEY_W) == 'z');
    assert(state.character(KEY_2) == U'é');
    assert(state.group() == 0);
    state.key(KEY_RIGHTALT, true);
    auto mod5 = xkb_keymap_mod_get_index(map, "Mod5");
    auto mod1 = xkb_keymap_mod_get_index(map, "Mod1");
    assert(state.modifiers(XKB_STATE_MODS_DEPRESSED) & (1u << mod5));
    assert(!(state.modifiers(XKB_STATE_MODS_DEPRESSED) & (1u << mod1)));
    assert(state.character(KEY_0) == '@');
    state.key(KEY_RIGHTALT, false);
    state.key(KEY_LEFTSHIFT, true);
    state.key(KEY_LEFTSHIFT, true); // Duplicate press must not leave Shift stuck.
    assert(state.character(KEY_Q) == 'A');
    state.key(KEY_LEFTSHIFT, false);
    assert(state.character(KEY_Q) == 'a');
    assert(state.modifiers(XKB_STATE_MODS_DEPRESSED) == 0);
    state.key(KEY_LEFTCTRL, true);
    state.key(KEY_RIGHTCTRL, true);
    state.key(KEY_LEFTCTRL, false);
    assert(state.modifiers(XKB_STATE_MODS_DEPRESSED) != 0);
    state.key(KEY_RIGHTCTRL, false);
    assert(state.modifiers(XKB_STATE_MODS_DEPRESSED) == 0);
    for (int cycle = 0; cycle < 100; ++cycle) {
        state.key(KEY_LEFTCTRL, true);
        state.key(KEY_RIGHTALT, true);
        state.key(KEY_Q, true);
        assert(state.releaseAll().size() == 3);
        assert(state.releaseAll().empty());
        assert(state.modifiers(XKB_STATE_MODS_DEPRESSED) == 0);
        assert(state.character(KEY_Q) == 'a');
        assert(state.group() == 0);
    }
    xkb_keymap_unref(map);
    xkb_context_unref(context);
    std::cout << "PASS: French AZERTY, accents, AltGr, both Ctrl keys, duplicate presses, 100 stop/resume cycles\n";
}
