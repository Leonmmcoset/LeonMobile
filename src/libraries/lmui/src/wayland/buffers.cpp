#include "backend.hpp"
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>
namespace lmui {
Application::Impl::Buffer *Application::Impl::buffer() {
    for (auto i = buffers.begin(); i != buffers.end();) {
        auto &b = **i;
        if (!b.busy && (b.w != w || b.h != h)) {
            wl_buffer_destroy(b.buffer);
            munmap(b.data, b.size);
            i = buffers.erase(i);
        } else
            ++i;
    }
    for (auto &b : buffers)
        if (!b->busy)
            return b.get();
    if (buffers.size() >= 3)
        return nullptr;
    auto b = std::make_unique<Buffer>();
    b->owner = this;
    b->w = w;
    b->h = h;
    b->size = size_t(w) * h * 4;
    int fd = memfd_create("lmui-frame", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, off_t(b->size))) {
        if (fd >= 0)
            close(fd);
        throw std::runtime_error("cannot allocate LMUI frame");
    }
    b->data = mmap(nullptr, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (b->data == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("cannot map LMUI frame");
    }
    auto *pool = wl_shm_create_pool(shm, fd, int(b->size));
    b->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    static const wl_buffer_listener events = {[](void *d, wl_buffer *) {
        auto *b = static_cast<Buffer *>(d);
        b->busy = false;
    }};
    wl_buffer_add_listener(b->buffer, &events, b.get());
    auto *out = b.get();
    buffers.push_back(std::move(b));
    return out;
}
} // namespace lmui
