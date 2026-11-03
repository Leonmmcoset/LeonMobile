#include <cassert>
#include <lmui/lmui.hpp>
int main() {
    bool activated = false;
    auto tree = lmui::Column({lmui::Label("LeonMobile 中文"),
                              lmui::Button("Launch", [&] { activated = true; }),
                              lmui::Slider(.5, [](double) {})});
    tree.layout({0, 0, 480, 300});
    assert(tree.children[1].bounds.h == 52);
    auto hit = tree.hit(40, 60);
    assert(hit && hit->activate);
    hit->activate();
    assert(activated);
    assert(!tree.hit(500, 60));
    auto *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 480, 300);
    auto *cr = cairo_create(s);
    lmui::Canvas c(cr, 480, 300, lmui::Theme::material());
    c.clear(c.theme.background);
    tree.draw(c);
    assert(!cairo_status(cr));
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}
