#include "screen.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace leon::terminal {
std::string utf8(uint32_t codepoint) {
    if (codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
        codepoint = 0xfffd;
    std::string out;
    if (codepoint < 0x80)
        out += char(codepoint);
    else {
        int length = codepoint < 0x800 ? 2 : codepoint < 0x10000 ? 3 : 4;
        out += char((length == 2   ? 0xc0
                     : length == 3 ? 0xe0
                                   : 0xf0) |
                    (codepoint >> (6 * (length - 1))));
        for (int shift = 6 * (length - 2); shift >= 0; shift -= 6)
            out += char(0x80 | ((codepoint >> shift) & 63));
    }
    return out;
}
Screen::Screen(int rows, int columns)
    : rows_(std::clamp(rows, 2, 256)), columns_(std::clamp(columns, 8, 512)) {
    vt_ = vterm_new(rows_, columns_);
    if (!vt_)
        throw std::runtime_error("cannot allocate terminal screen");
    vterm_set_utf8(vt_, 1);
    vterm_output_set_callback(
        vt_,
        [](const char *text, size_t size, void *data) {
            auto &s = *static_cast<Screen *>(data);
            if (s.output_.size() + size <= 1024 * 1024)
                s.output_.append(text, size);
        },
        this);
    screen_ = vterm_obtain_screen(vt_);
    VTermScreenCallbacks callbacks{};
    callbacks.damage = [](VTermRect, void *data) {
        ++static_cast<Screen *>(data)->revision_;
        return 1;
    };
    callbacks.movecursor = [](VTermPos, VTermPos, int, void *data) {
        ++static_cast<Screen *>(data)->revision_;
        return 1;
    };
    callbacks.settermprop = [](VTermProp property, VTermValue *value, void *data) {
        return static_cast<Screen *>(data)->property(property, value);
    };
    callbacks.bell = [](void *data) {
        ++static_cast<Screen *>(data)->bells_;
        return 1;
    };
    callbacks.sb_pushline = [](int columns, const VTermScreenCell *cells, void *data) {
        static_cast<Screen *>(data)->push(columns, cells);
        return 1;
    };
    callbacks.sb_popline = [](int columns, VTermScreenCell *cells, void *data) {
        return static_cast<Screen *>(data)->pop(columns, cells);
    };
#if VTERM_VERSION_MAJOR > 0 || VTERM_VERSION_MINOR >= 3
    callbacks.sb_clear = [](void *data) {
        auto &s = *static_cast<Screen *>(data);
        s.history_.clear();
        s.history_cells_ = s.offset_ = 0;
        return 1;
    };
#endif
    // libvterm retains the callbacks pointer for the screen's lifetime.
    static const auto screen_callbacks = callbacks;
    vterm_screen_set_callbacks(screen_, &screen_callbacks, this);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_ROW);
    VTermColor foreground, background;
    vterm_color_rgb(&foreground, 238, 230, 244);
    vterm_color_rgb(&background, 25, 21, 31);
    vterm_state_set_default_colors(vterm_obtain_state(vt_), &foreground, &background);
    reset();
}
Screen::~Screen() { vterm_free(vt_); }
void Screen::feed(std::string_view bytes) {
    vterm_input_write(vt_, bytes.data(), bytes.size());
    vterm_screen_flush_damage(screen_);
}
void Screen::resize(int rows, int columns) {
    rows = std::clamp(rows, 2, 256);
    columns = std::clamp(columns, 8, 512);
    if (rows == rows_ && columns == columns_)
        return;
    rows_ = rows;
    columns_ = columns;
    vterm_set_size(vt_, rows_, columns_);
    offset_ = std::min<int>(offset_, history_.size());
    vterm_screen_flush_damage(screen_);
    ++revision_;
}
void Screen::reset() {
    history_.clear();
    history_cells_ = offset_ = 0;
    title_.clear();
    pending_title_.clear();
    output_.clear();
    vterm_screen_reset(screen_, 1);
    vterm_screen_flush_damage(screen_);
    ++revision_;
}
int Screen::property(VTermProp property, VTermValue *value) {
    switch (property) {
    case VTERM_PROP_ALTSCREEN:
        alternate_ = value->boolean;
        offset_ = 0;
        break;
    case VTERM_PROP_CURSORVISIBLE:
        cursor_visible_ = value->boolean;
        break;
    case VTERM_PROP_CURSORBLINK:
        cursor_blink_ = value->boolean;
        break;
    case VTERM_PROP_CURSORSHAPE:
        cursor_shape_ = value->number;
        break;
    case VTERM_PROP_MOUSE:
        mouse_mode_ = value->number;
        break;
    case VTERM_PROP_REVERSE:
        reverse_ = value->boolean;
        break;
    case VTERM_PROP_TITLE:
#if VTERM_VERSION_MAJOR > 0 || VTERM_VERSION_MINOR >= 3
        if (value->string.initial)
            pending_title_.clear();
        pending_title_.append(value->string.str,
                              std::min<size_t>(value->string.len, 256 - pending_title_.size()));
        if (value->string.final)
            title_ = pending_title_;
#else
        title_ = std::string(value->string, strnlen(value->string, 256));
#endif
        break;
    default:
        break;
    }
    ++revision_;
    return 1;
}
void Screen::push(int columns, const VTermScreenCell *cells) {
    if (alternate_)
        return;
    history_.emplace_back(cells, cells + columns);
    history_cells_ += columns;
    if (offset_)
        ++offset_;
    while (history_.size() > 2000 || history_cells_ > 262144) {
        history_cells_ -= history_.front().size();
        history_.pop_front();
    }
    offset_ = std::min<int>(offset_, history_.size());
}
int Screen::pop(int columns, VTermScreenCell *cells) {
    if (history_.empty() || alternate_)
        return 0;
    std::fill(cells, cells + columns, VTermScreenCell{});
    VTermColor foreground, background;
    vterm_state_get_default_colors(vterm_obtain_state(vt_), &foreground, &background);
    for (int column = 0; column < columns; ++column) {
        cells[column].width = 1;
        cells[column].fg = foreground;
        cells[column].bg = background;
    }
    const auto &line = history_.back();
    std::copy_n(line.begin(), std::min<size_t>(columns, line.size()), cells);
    if (cells[columns - 1].width == 2) {
        cells[columns - 1].chars[0] = 0;
        cells[columns - 1].width = 1;
    }
    history_cells_ -= line.size();
    history_.pop_back();
    offset_ = std::min<int>(offset_, history_.size());
    return 1;
}
VTermScreenCell Screen::cell(int row, int column) const {
    VTermScreenCell result{};
    vterm_state_get_default_colors(vterm_obtain_state(vt_), &result.fg, &result.bg);
    result.width = 1;
    if (row < 0 || row >= rows_ || column < 0 || column >= columns_)
        return result;
    int index = int(history_.size()) - offset_ + row;
    if (!alternate_ && index < int(history_.size())) {
        const auto &line = history_[index];
        if (column < int(line.size()))
            result = line[column];
    } else
        vterm_screen_get_cell(screen_, {row - (alternate_ ? 0 : offset_), column}, &result);
    return result;
}
VTermColor Screen::rgb(VTermColor color) const {
    vterm_screen_convert_color_to_rgb(screen_, &color);
    return color;
}
VTermPos Screen::cursor() const {
    VTermPos position{};
    vterm_state_get_cursorpos(vterm_obtain_state(vt_), &position);
    return position;
}
void Screen::key(VTermKey key, unsigned modifiers) {
    live();
    vterm_keyboard_key(vt_, key, VTermModifier(modifiers & VTERM_ALL_MODS_MASK));
}
void Screen::character(uint32_t codepoint, unsigned modifiers) {
    live();
    vterm_keyboard_unichar(vt_, codepoint, VTermModifier(modifiers & VTERM_ALL_MODS_MASK));
}
void Screen::text(std::string_view bytes) {
    // Wayland IME commits are validated UTF-8 by the application before entry.
    live();
    for (size_t i = 0; i < bytes.size();) {
        unsigned char lead = bytes[i++];
        uint32_t codepoint = lead;
        int extra = 0;
        if (lead >= 0xf0) {
            codepoint &= 7;
            extra = 3;
        } else if (lead >= 0xe0) {
            codepoint &= 15;
            extra = 2;
        } else if (lead >= 0xc0) {
            codepoint &= 31;
            extra = 1;
        }
        while (extra-- && i < bytes.size())
            codepoint = (codepoint << 6) | (static_cast<unsigned char>(bytes[i++]) & 63);
        if (codepoint == '\n')
            key(VTERM_KEY_ENTER);
        else
            character(codepoint);
    }
}
bool Screen::paste(std::string_view bytes) {
    if (output_.size() + bytes.size() + 32 > 1024 * 1024)
        return false;
    live();
    vterm_keyboard_start_paste(vt_);
    // Strip control bytes (including ESC) so pasted text cannot terminate
    // bracketed paste early. Keep tabs and normalize newlines to carriage return.
    bool carriage = false;
    for (unsigned char byte : bytes) {
        if (byte == '\n') {
            if (!carriage)
                output_ += '\r';
        } else if (byte == '\t' || byte == '\r' || (byte >= 32 && byte != 127))
            output_ += char(byte);
        carriage = byte == '\r';
    }
    vterm_keyboard_end_paste(vt_);
    return true;
}
void Screen::focus(bool focused) {
    if (focused)
        vterm_state_focus_in(vterm_obtain_state(vt_));
    else
        vterm_state_focus_out(vterm_obtain_state(vt_));
}
void Screen::mouse(int row, int column, int button, bool pressed, unsigned modifiers) {
    auto mod = VTermModifier(modifiers & VTERM_ALL_MODS_MASK);
    vterm_mouse_move(vt_, std::clamp(row, 0, rows_ - 1), std::clamp(column, 0, columns_ - 1), mod);
    if (button)
        vterm_mouse_button(vt_, button, pressed, mod);
}
void Screen::scroll(int lines) {
    if (!alternate_)
        offset_ = std::clamp(offset_ + lines, 0, int(history_.size()));
}
std::string Screen::take_output() { return std::exchange(output_, {}); }
std::string Screen::selection(VTermPos start, VTermPos end) const {
    if (vterm_pos_cmp(start, end) > 0)
        std::swap(start, end);
    start.row = std::clamp(start.row, 0, rows_ - 1);
    end.row = std::clamp(end.row, 0, rows_ - 1);
    std::string result;
    for (int row = start.row; row <= end.row; ++row) {
        std::string line;
        int first = row == start.row ? std::max(0, start.col) : 0;
        int last = row == end.row ? std::min(columns_ - 1, end.col) : columns_ - 1;
        for (int col = first; col <= last; ++col) {
            auto value = cell(row, col);
            if (value.chars[0] == UINT32_MAX)
                continue;
            if (!value.chars[0])
                line += ' ';
            else
                for (auto codepoint : value.chars) {
                    if (!codepoint)
                        break;
                    line += utf8(codepoint);
                }
        }
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        result += line;
        if (row < end.row)
            result += '\n';
    }
    return result;
}
} // namespace leon::terminal
