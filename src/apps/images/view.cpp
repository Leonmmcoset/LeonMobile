#include "application.hpp"
#include <leon/platform.hpp>
using namespace mobile;
namespace leon::apps {
ImagesApplication::ImagesApplication() : ApplicationWindow("org.leon.images", "图片", "Images") {}
void ImagesApplication::text_input(const std::string &text) { edit(image_path, text); }
void ImagesApplication::paint_content(Canvas &c, double bottom) {
    auto t = c.theme;
    double w = c.width;
    button(c, hits, {20, 84, w - 132, 48}, image_path, [&] { keyboard = true; });
    button(c, hits, {w - 104, 84, 84, 48}, tr(en, "打开", "Open"), [&] {
        image.reset(decode_image(image_path));
        if (!image || cairo_surface_status(image.get()))
            error = "Cannot decode PNG/JPEG";
        keyboard = false;
    });
    button(c, hits, {20, 144, w - 40, 46}, tr(en, "选择图片", "Choose an image"), [this] {
        perform([this] {document_token=leon::platform::Client().choose_document();keyboard=false;});
    });
    if (image)
        c.image(image.get(), {20, 202, w - 40, bottom - 228});
    else
        c.text(tr(en, "在应用目录放入 PNG 或 JPEG 图片",
                  "Place a PNG or JPEG in the application directory"),
               {32, 220, w - 64, 120}, 18, t.muted, false, 1);
}
} // namespace leon::apps
