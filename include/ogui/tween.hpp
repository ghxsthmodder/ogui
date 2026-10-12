#pragma once
#include <cstdint>

namespace ogui {

// ------------------------------------------------------------------
// Tweens: small, header-light animation building blocks.
//
// A Tween tweens one scalar from `from` to `to` over `duration`
// seconds with an easing curve, after an optional delay. Widgets keep
// a Tween member, call update(dt) once a frame (Kernel::frame does it),
// and read value() to draw an interpolated state. start() resets the
// clock; finish() snaps to the end.
// ------------------------------------------------------------------
enum class TweenEase : uint8_t {
    Linear,
    EaseOutCubic,
    EaseInOutCubic,
    EaseOutBack,
};

// T in [0,1] mapped through an easing curve; t is clamped.
float tweenEaseValue(TweenEase ease, float t);

class Tween {
public:
    Tween() = default;

    void start(float from, float to, float duration, TweenEase ease,
               float delay = 0.0f);
    void stop() { m_running = false; }
    void finish();                 // snap to the end state, stop
    void update(float dt);         // advance by seconds

    bool running() const { return m_running; }
    float value() const;
    float startValue() const { return m_from; }
    float endValue() const { return m_to; }

private:
    TweenEase m_ease = TweenEase::Linear;
    float m_from = 0.0f;
    float m_to = 0.0f;
    float m_delay = 0.0f;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
    bool m_running = false;
};

} // namespace ogui