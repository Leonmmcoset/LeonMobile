#include "input.hpp"
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>
namespace leon::terminal {
bool key_input(Screen &screen, unsigned symbol, unsigned modifiers) {
    VTermKey key = VTERM_KEY_NONE;
    switch (symbol) {
    case XKB_KEY_Return:
        key = VTERM_KEY_ENTER;
        break;
    case XKB_KEY_KP_Enter:
        key = VTERM_KEY_KP_ENTER;
        break;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab:
        key = VTERM_KEY_TAB;
        break;
    case XKB_KEY_BackSpace:
        key = VTERM_KEY_BACKSPACE;
        break;
    case XKB_KEY_Escape:
        key = VTERM_KEY_ESCAPE;
        break;
    case XKB_KEY_Up:
        key = VTERM_KEY_UP;
        break;
    case XKB_KEY_Down:
        key = VTERM_KEY_DOWN;
        break;
    case XKB_KEY_Left:
        key = VTERM_KEY_LEFT;
        break;
    case XKB_KEY_Right:
        key = VTERM_KEY_RIGHT;
        break;
    case XKB_KEY_Insert:
        key = VTERM_KEY_INS;
        break;
    case XKB_KEY_Delete:
        key = VTERM_KEY_DEL;
        break;
    case XKB_KEY_Home:
        key = VTERM_KEY_HOME;
        break;
    case XKB_KEY_End:
        key = VTERM_KEY_END;
        break;
    case XKB_KEY_Page_Up:
        key = VTERM_KEY_PAGEUP;
        break;
    case XKB_KEY_Page_Down:
        key = VTERM_KEY_PAGEDOWN;
        break;
    case XKB_KEY_KP_Add:
        key = VTERM_KEY_KP_PLUS;
        break;
    case XKB_KEY_KP_Subtract:
        key = VTERM_KEY_KP_MINUS;
        break;
    case XKB_KEY_KP_Multiply:
        key = VTERM_KEY_KP_MULT;
        break;
    case XKB_KEY_KP_Divide:
        key = VTERM_KEY_KP_DIVIDE;
        break;
    case XKB_KEY_KP_Decimal:
        key = VTERM_KEY_KP_PERIOD;
        break;
    default:
        if (symbol >= XKB_KEY_F1 && symbol <= XKB_KEY_F35)
            key = VTermKey(VTERM_KEY_FUNCTION(symbol - XKB_KEY_F1 + 1));
        else if (symbol >= XKB_KEY_KP_0 && symbol <= XKB_KEY_KP_9)
            key = VTermKey(VTERM_KEY_KP_0 + symbol - XKB_KEY_KP_0);
        break;
    }
    if (key != VTERM_KEY_NONE) {
        screen.key(key, modifiers);
        return true;
    }
    if (uint32_t codepoint = xkb_keysym_to_utf32(symbol)) {
        screen.character(codepoint, modifiers);
        return true;
    }
    return false;
}
} // namespace leon::terminal
