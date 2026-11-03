#include "shade.hpp"
#include <cassert>
#include <chrono>

using leon::shell::Shade;
using namespace std::chrono_literals;
int main() {
    Shade shade;
    auto time = Shade::Clock::now();
    assert(!shade.visible());
    // A short pull returns to the application; a deliberate pull opens stage 1.
    shade.begin(20, 960);
    shade.move(50, 960);
    assert(shade.extent() > 0 && shade.extent() < 1);
    assert(shade.release(50, 960, time));
    shade.advance(time + 200ms);
    assert(!shade.visible());
    shade.begin(20, 960);
    shade.move(360, 960);
    assert(shade.height(960) > 350 && shade.height(960) < 400);
    shade.release(360, 960, time);
    shade.advance(time + 200ms);
    assert(shade.extent() == 1 && shade.height(960) == 700);
    shade.begin(100, 960);
    shade.release(400, 960, time);
    shade.advance(time + 200ms);
    assert(shade.extent() == 2 && shade.height(960) == 896);
    // Reversing an in-flight close starts from its current rendered extent.
    shade.show(Shade::Level::closed, time + 200ms);
    shade.advance(time + 260ms);
    double intermediate = shade.extent();
    shade.show(Shade::Level::expanded, time + 260ms);
    assert(shade.extent() == intermediate);
    shade.advance(time + 500ms);
    assert(shade.extent() == 2);
    shade.begin(500, 960);
    shade.release(180, 960, time);
    shade.advance(time + 200ms);
    assert(shade.extent() == 1);
    shade.begin(400, 960);
    shade.release(0, 960, time);
    shade.advance(time + 200ms);
    assert(!shade.visible());
    shade.reset();
    shade.begin(20, 960);
    assert(!shade.release(20, 960, time));
    assert(!shade.visible());
}
