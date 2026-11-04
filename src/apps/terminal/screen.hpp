#pragma once
#include <deque>
#include <string>
#include <string_view>
#include <vector>
#include <vterm.h>

namespace leon::terminal {
std::string utf8(uint32_t codepoint);
class Screen {
  public:
    explicit Screen(int rows = 24, int columns = 80);
    ~Screen();
    Screen(const Screen &) = delete;
    Screen &operator=(const Screen &) = delete;
    void feed(std::string_view bytes);
    void resize(int rows, int columns);
    void reset();
    void key(VTermKey key, unsigned modifiers = 0);
    void character(uint32_t codepoint, unsigned modifiers = 0);
    void text(std::string_view utf8);
    bool paste(std::string_view utf8);
    void focus(bool focused);
    void mouse(int row, int col, int button, bool pressed, unsigned modifiers = 0);
    void scroll(int lines);
    void live() { offset_ = 0; }
    std::string take_output();
    VTermScreenCell cell(int row, int column) const;
    VTermColor rgb(VTermColor color) const;
    VTermPos cursor() const;
    std::string selection(VTermPos start, VTermPos end) const;
    int rows() const { return rows_; }
    int columns() const { return columns_; }
    int offset() const { return offset_; }
    size_t history_size() const { return history_.size(); }
    bool alternate() const { return alternate_; }
    bool cursor_visible() const { return cursor_visible_ && !offset_; }
    bool cursor_blink() const { return cursor_blink_; }
    int cursor_shape() const { return cursor_shape_; }
    int mouse_mode() const { return mouse_mode_; }
    bool reverse() const { return reverse_; }
    unsigned revision() const { return revision_; }
    unsigned bells() const { return bells_; }
    const std::string &title() const { return title_; }

  private:
    VTerm *vt_{};
    VTermScreen *screen_{};
    int rows_, columns_, offset_{};
    bool alternate_{}, cursor_visible_{true}, cursor_blink_{true}, reverse_{};
    int cursor_shape_{VTERM_PROP_CURSORSHAPE_BLOCK}, mouse_mode_{};
    unsigned revision_{}, bells_{};
    std::string output_, title_, pending_title_;
    std::deque<std::vector<VTermScreenCell>> history_;
    size_t history_cells_{};
    int property(VTermProp, VTermValue *);
    void push(int columns, const VTermScreenCell *cells);
    int pop(int columns, VTermScreenCell *cells);
};
} // namespace leon::terminal
