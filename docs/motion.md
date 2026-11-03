# LMUI motion and touch

LeonMobile 1.0.0 remains a developer graphics prototype. LMUI implements the
Material 3 standard cubic `(0.2, 0, 0, 1)`, emphasized two-segment path,
emphasized deceleration `(0.05, 0.7, 0.1, 1)` and emphasized acceleration
`(0.3, 0, 0.8, 0.15)`. These curves follow the Material motion specification;
no Android code or assets are embedded. Feedback takes 100 ms, state changes
200 ms, entrances 300 ms and larger spatial changes 400 ms. Motion communicates
input and state changes; it never loops decoratively or delays an action until
an animation finishes. Curves are bounded to keep text and controls readable.

## Painting and scheduling

`<lmui/motion.hpp>` exposes `Animation`, `MotionSpec`, `Easing`, `ease`, `mix`
and `MotionContext`. Time arguments use monotonic seconds (`motion_time()`);
specification durations use milliseconds. `Animation::to` retargets from its
current displayed value. The new trajectory is position-continuous; it does
not preserve the old velocity. This avoids jumps during rapid reversals.

Within an application paint callback, use a stable key:

```cpp
double position = canvas.animate("volume/value", volume, lmui::motion::settle);
canvas.circle(24 + position * 400, 200, 10, canvas.theme.primary);
```

The first sample starts at the target unless an explicit initial value is
provided as the fourth argument. Keys must identify the same control across
frames; use separate prefixes for pages and repeated rows. Entries disappear
after a frame in which they are not painted. `Widget::motion_id` optionally
supplies an explicit identity; otherwise the tree path identifies it. Toggle
thumbs and tracks animate state changes. Set a slider's `interacting` flag
while dragging so it follows the contact immediately.

The Wayland backend uses surface frame callbacks, a monotonic clock and at
most three shared-memory buffers. Active motion requests the next frame;
settled motion stops repainting. Hidden clients wait for their compositor
callback. The existing 200 ms `tick` callback remains for application logic,
separate from animation. Custom time-based paint effects can use
`frame_time()` and `request_frame()`; they must stop requesting frames when
settled. A manually constructed Canvas without a MotionContext renders final
values, keeping offscreen rendering deterministic.

`ContentTransition` captures the last painted composition, then crossfades
and translates incoming content by a short distance. Call `start(dx, dy)`
when the page changes, and wrap the relevant paint code in `begin(canvas)` /
`end(canvas)`. Translate incoming hit rectangles by `offset_x()` and
`offset_y()` after painting. Cancel any old touch capture on navigation.
Interrupted transitions retain the last presentation and finish at exact
final coordinates. The object owns its Cairo patterns and must outlive the
page; snapshots are bounded rather than retained per page.

## Input and visual feedback

`Application::pointer_event` receives `PointerEvent {x, y, phase}` with
`PointerPhase::{down, move, up, cancel}`. Mouse input uses the primary button;
touch uses one captured contact and ignores additional contacts. Pointer leave,
seat capability loss and touch cancellation cancel the capture. The older
`pointer(x, y, down)` callback remains available for down/up compatibility.
Applications should use one dispatch path to avoid duplicate actions.

`TouchInput::handle(event, targets)` accepts a list of `TouchTarget` records
(rectangle, action, optional position action, continuous flag, prominent flag,
corner radius). The last matching target wins. It copies the selected target
and callbacks, so rebuilding a tree cannot invalidate a held contact. Buttons
activate on release inside their target; moving more than 12 logical pixels,
leaving the target, or cancellation prevents activation. A continuous position
control receives down/move/up updates and can clamp coordinates to its range.
Its optional action runs after the final position update, including release
outside the track, so expensive changes can be committed once per drag.
`handle(event, widget)` also supports a retained Widget tree and its sliders.

```cpp
lmui::TouchInput touch;
app.pointer_event = [&](const lmui::PointerEvent &event) {
    touch.handle(event, tree);
};
app.paint = [&](lmui::Canvas &canvas) {
    tree.layout({24, 24, 432, 600});
    tree.draw(canvas);
    touch.draw(canvas);
};
```

Call `draw` after the controls to paint a rounded, clipped state layer and
contact-origin ripple. Short taps retain at least 100 ms of feedback before
the 180 ms release fade. A held, fully expanded ripple stops repainting.
The feedback is visual; no physical vibration support is claimed.

## Reduced motion and verification

`Application::reduce_motion` finishes transitions immediately. A process-level
`LMUI_REDUCE_MOTION=1` override forces the same policy even if application
settings change. Held controls still display a static state layer; there is
no expanding ripple, spatial movement or delayed release fade. Debug
`LMUI_CAPTURE_FRAME` / `LMUI_QUIT_AFTER_FRAME` render settled state for stable
screenshots. These environment options do not add manifest permissions.

Built-in applications read the persisted boolean `reduce_motion` preference.
Settings exposes it under Display → Reduce motion. All eight built-ins share
release activation and ripples; keyboard expansion, content resizing, error
messages, settings selections and folder/page navigation use the same motion
policy. Settings and file lists track touch drags immediately, then apply a
short bounded scroll settle. The volume slider previews locally while held
and sends one audio request on release rather than blocking each motion event.

`lmui-motion-and-touch` covers bounded curves, interruption, stale-key removal,
release/cancel/drag behavior and rendered transition continuity. The independent
Weston smoke script additionally checks real intermediate frames, completion,
idle repaint suspension and reduced motion, saving motion PNGs outside Git.
