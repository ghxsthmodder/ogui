#include "ogui/tween.hpp"
#include <algorithm>

namespace ogui {

namespace {
float clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }
} // namespace

float tweenEaseValue(TweenEase ease, float t) {
    t = clamp01(t);
    switch (ease) {
        case TweenEase::Linear: return t;
        case TweenEase::EaseOutCubic: {   // fast start, smooth landing
            const float u = 1.0f - t;
            return 1.0f - u * u * u;
        }
        case TweenEase::EaseInOutCubic: { // symmetric S-curve
            const float u = t * 2.0f;
            if (u < 1.0f) {
                return 0.5f * u * u * u;
            }
            const float v = 2.0f - u;
            return 0.5f + 0.5f * (1.0f - v * v * v);
        }
        case TweenEase::EaseOutBack: {    // overshoots, settles back
            constexpr float c1 = 1.70158f;
            constexpr float c3 = c1 + 1.0f;
            const float u = t - 1.0f;
            return 1.0f + c3 * u * u * u + c1 * u * u;
        }
    }
    return t;
}

void Tween::start(float from, float to, float duration, TweenEase ease,
                  float delay) {
    m_from = from;
    m_to = to;
    m_duration = duration;
    m_ease = ease;
    m_delay = delay > 0.0f ? delay : 0.0f;
    m_elapsed = 0.0f;
    m_running = duration > 0.0f;
}

void Tween::finish() {
    m_elapsed = m_duration + m_delay + 1.0f;
    m_running = false;
}

void Tween::update(float dt) {
    if (!m_running) return;
    m_elapsed += dt > 0.0f ? dt : 0.0f;
    if (m_elapsed >= m_delay + m_duration) {
        m_elapsed = m_delay + m_duration;
        m_running = false;
    }
}

float Tween::value() const {
    if (m_elapsed <= m_delay) return m_from;
    if (m_elapsed >= m_delay + m_duration) return m_to;
    const float t = (m_elapsed - m_delay) / m_duration;
    return m_from + (m_to - m_from) * tweenEaseValue(m_ease, t);
}

} // namespace ogui