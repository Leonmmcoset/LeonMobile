#pragma once
#include <cairo.h>
#include <memory>
#include <string>
namespace leon::apps {
struct ImageDeleter {
    void operator()(cairo_surface_t *image) const { cairo_surface_destroy(image); }
};
using Image = std::unique_ptr<cairo_surface_t, ImageDeleter>;
cairo_surface_t *decode_image(const std::string &path);
} // namespace leon::apps
