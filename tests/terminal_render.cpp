#include "renderer.hpp"
#include <cassert>
#include <cstdlib>
int main() {
    using namespace leon::terminal;
    Screen screen(4, 12);
    screen.feed("\033[?25l\033[41m   \033[0m\r\niiWW 中é\r\n\033[1;3;4mstyled");
    Renderer renderer;
    auto *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 480, 200);
    auto *cr = cairo_create(surface);
    mobile::Canvas canvas(cr, 480, 200, mobile::Theme::material());
    canvas.clear(canvas.theme.background);
    renderer.draw(canvas, {0, 0, 480, 200}, screen, {}, true, true);
    cairo_surface_flush(surface);
    auto *pixels = reinterpret_cast<uint32_t *>(cairo_image_surface_get_data(surface));
    auto first = pixels[12 * 480 + 11] & 0xffffff;
    assert(first != 0x19151f);
    for (int column = 0; column < 3; ++column)
        assert((pixels[12 * 480 + 11 + column * renderer.cell_width()] & 0xffffff) == first);
    for (int col = 0; col < 4; ++col) {
        bool ink = false;
        for (int y = 10 + renderer.cell_height(); y < 10 + renderer.cell_height() * 2; ++y)
            for (int x = 10 + col * renderer.cell_width();
                 x < 10 + (col + 1) * renderer.cell_width(); ++x)
                ink |= (pixels[y * 480 + x] & 0xffffff) != 0x19151f;
        assert(ink);
    }
    assert(renderer
               .position({0, 0, 480, 200}, 10 + renderer.cell_width() * 3 + 1,
                         11 + renderer.cell_height(), screen)
               .col == 3);
    if (const char *path = getenv("LEON_TERMINAL_EVIDENCE"))
        assert(cairo_surface_write_to_png(surface, path) == CAIRO_STATUS_SUCCESS);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
