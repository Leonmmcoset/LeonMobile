#include "decoder.hpp"
#include <cstdio>
#include <filesystem>
#include <jpeglib.h>
#include <setjmp.h>
namespace leon::apps {
namespace fs = std::filesystem;
struct JpegError {
    jpeg_error_mgr base;
    jmp_buf jump;
};
cairo_surface_t *decode_image(const std::string &path) {
    if (fs::path(path).extension() == ".png")
        return cairo_image_surface_create_from_png(path.c_str());
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return nullptr;
    jpeg_decompress_struct d{};
    JpegError err{};
    d.err = jpeg_std_error(&err.base);
    cairo_surface_t *volatile image = nullptr;
    err.base.error_exit = [](j_common_ptr ptr) {
        longjmp(reinterpret_cast<JpegError *>(ptr->err)->jump, 1);
    };
    if (setjmp(err.jump)) {
        if (image)
            cairo_surface_destroy(image);
        jpeg_destroy_decompress(&d);
        fclose(f);
        return nullptr;
    }
    jpeg_create_decompress(&d);
    jpeg_stdio_src(&d, f);
    jpeg_read_header(&d, TRUE);
    d.out_color_space = JCS_RGB;
    if (d.image_width > 8192 || d.image_height > 8192) {
        jpeg_destroy_decompress(&d);
        fclose(f);
        return nullptr;
    }
    jpeg_start_decompress(&d);
    auto *s =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, int(d.output_width), int(d.output_height));
    image = s;
    if (cairo_surface_status(s)) {
        cairo_surface_destroy(s);
        jpeg_destroy_decompress(&d);
        fclose(f);
        return nullptr;
    }
    // libjpeg owns the row allocation, so its longjmp error path cannot skip
    // a C++ destructor or leak the partially decoded surface.
    JSAMPARRAY rows = (*d.mem->alloc_sarray)(reinterpret_cast<j_common_ptr>(&d), JPOOL_IMAGE,
                                             d.output_width * 3, 1);
    auto *data = cairo_image_surface_get_data(s);
    int stride = cairo_image_surface_get_stride(s);
    while (d.output_scanline < d.output_height) {
        auto y = d.output_scanline;
        jpeg_read_scanlines(&d, rows, 1);
        auto *row = rows[0];
        auto *dst = reinterpret_cast<uint32_t *>(data + y * stride);
        for (unsigned x = 0; x < d.output_width; ++x)
            dst[x] =
                (unsigned(row[x * 3]) << 16) | (unsigned(row[x * 3 + 1]) << 8) | row[x * 3 + 2];
    }
    jpeg_finish_decompress(&d);
    jpeg_destroy_decompress(&d);
    fclose(f);
    cairo_surface_mark_dirty(s);
    return s;
}
} // namespace leon::apps
