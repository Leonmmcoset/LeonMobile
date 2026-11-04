#include "../../services/serviced/notify.h"
#include "backend.h"
#include "engine.h"
#include <cctype>
#include <csignal>
#include <iostream>
#include <leon/mobile/ui.hpp>
using namespace mobile;
static volatile sig_atomic_t stopping;
static void stop(int) { stopping = 1; }
int main() {
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    ime_engine engine{};
    const char *dictionary = getenv("LEON_PINYIN_DICTIONARY");
    if (ime_engine_open(&engine, dictionary ? dictionary : "/usr/share/leon/ime/pinyin.tsv")) {
        std::cerr << "imed: dictionary unavailable\n";
        return 1;
    }
    auto *backend = ime_backend_open();
    if (!backend) {
        ime_engine_close(&engine);
        return 1;
    }
    Application app("org.leon.ime", "LeonMobile keyboard");
    std::vector<Hit> hits;
    TouchInput feedback;
    bool chinese = true, symbols = false, ready = false, shift = false;
    unsigned ticks = 0;
    unsigned revision = ime_backend_revision(backend);
    auto send = [&](const std::string &text) {
        ime_engine_clear(&engine);
        ime_backend_send(backend, text.c_str(), "", 0);
        app.redraw();
    };
    auto input = [&](const std::string &text) {
        if (text == "\b") {
            if (*engine.preedit) {
                ime_engine_backspace(&engine);
                ime_backend_send(backend, nullptr, engine.preedit, 0);
            } else
                ime_backend_send(backend, nullptr, "", 1);
        } else if (chinese && !symbols && !ime_backend_private(backend) && text.size() == 1 &&
                   text[0] >= 'a' && text[0] <= 'z') {
            ime_engine_letter(&engine, text[0]);
            ime_backend_send(backend, nullptr, engine.preedit, 0);
        } else if (*engine.preedit) {
            const char *candidates[6];
            size_t count = ime_engine_candidates(&engine, candidates, 6);
            std::string word = count ? candidates[0] : engine.preedit;
            send(word + (text == " " ? "" : text));
        } else
            send(text);
        app.redraw();
    };
    auto dispatch = [&] {
        if (ime_backend_poll(backend)) {
            app.quit();
            return;
        }
        if (revision != ime_backend_revision(backend)) {
            revision = ime_backend_revision(backend);
            ime_engine_clear(&engine);
            feedback.cancel();
            app.redraw();
        }
    };
    app.watch_fd(ime_backend_fd(backend), dispatch);
    app.tick = [&] {
        if (stopping) {
            app.quit();
            return;
        }
        if (++ticks % 5 == 0)
            try {
                auto prefs = rpc({{"op", "settings"}}).at("settings");
                bool dark = prefs.value("dark", false);
                if (dark != app.dark) {
                    app.dark = dark;
                    app.redraw();
                }
                app.reduce_motion(prefs.value("reduce_motion", false));
            } catch (...) {
            }
        leon_service_watchdog();
    };
    app.on_pointer_event([&](const PointerEvent &e) {
        feedback.handle(e, hits);
        app.redraw();
    });
    app.paint = [&](Canvas &c) {
        if (!ready) {
            leon_service_ready();
            ready = true;
        }
        hits.clear();
        auto t = c.theme;
        double w = c.width, h = c.height, top = h - 300;
        c.clear(t.container);
        c.text(*engine.preedit ? engine.preedit : (chinese ? "拼音 · 中文" : "English"),
               {16, top, w - 32, 32}, 17, t.text);
        const char *candidates[6];
        size_t count = ime_engine_candidates(&engine, candidates, 6);
        double cw = (w - 70) / 6;
        for (size_t i = 0; i < count; ++i) {
            std::string word = candidates[i];
            button(c, hits, {6 + i * cw, top + 34, cw - 4, 44}, word, [&, word] { send(word); });
        }
        button(c, hits, {w - 64, top + 34, 28, 44}, "‹", [&] {
            if (engine.page)
                --engine.page;
        });
        button(c, hits, {w - 32, top + 34, 28, 44}, "›", [&] {
            ++engine.page;
            const char *next[6];
            if (!ime_engine_candidates(&engine, next, 6))
                engine.page = 0;
        });
        std::vector<std::string> rows =
            symbols ? std::vector<std::string>{"1234567890", "-/:;()$&@", ".,!?\"'=+"}
                    : std::vector<std::string>{"qwertyuiop", "asdfghjkl", "zxcvbnm"};
        for (size_t r = 0; r < rows.size(); ++r) {
            double inset = r == 2 ? 54 : 8;
            double kw = (w - 2 * inset) / rows[r].size();
            for (size_t i = 0; i < rows[r].size(); ++i) {
                std::string letter(1, rows[r][i]);
                if (shift && !symbols && (!chinese || ime_backend_private(backend)))
                    letter[0] = char(std::toupper(static_cast<unsigned char>(letter[0])));
                button(c, hits, {inset + i * kw, top + 84 + r * 51, kw - 4, 46}, letter,
                       [&, letter] { input(letter); });
            }
        }
        button(c, hits, {8, top + 186, 40, 46}, "⇧", [&] { shift = !shift; });
        button(c, hits, {8, top + 244, 52, 48}, symbols ? "ABC" : "123",
               [&] { symbols = !symbols; });
        button(c, hits, {66, top + 244, 66, 48}, chinese ? "中/英" : "En/中", [&] {
            send("");
            chinese = !chinese;
        });
        button(c, hits, {138, top + 244, w - 302, 48}, "Space", [&] { input(" "); });
        button(c, hits, {w - 158, top + 244, 70, 48}, "⌫", [&] { input("\b"); });
        button(
            c, hits, {w - 82, top + 244, 74, 48}, "↵", [&] { input("\n"); }, true);
        feedback.draw(c);
    };
    int result = app.run();
    app.unwatch_fd(ime_backend_fd(backend));
    ime_backend_close(backend);
    ime_engine_close(&engine);
    return result;
}
