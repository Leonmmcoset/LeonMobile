#pragma once
#include <leon/mobile/ui.hpp>
#include <sys/types.h>
#include <vterm.h>
namespace leon::apps {
class Terminal {
  public:
    Terminal();
    ~Terminal();
    Terminal(const Terminal &) = delete;
    Terminal &operator=(const Terminal &) = delete;
    void poll();
    void input(const std::string &text);
    void draw(mobile::Canvas &canvas, mobile::Rect bounds);

  private:
    int fd{-1};
    pid_t child{};
    VTerm *vt{};
    VTermScreen *screen{};
    int cols{48}, rows{24};
};
} // namespace leon::apps
