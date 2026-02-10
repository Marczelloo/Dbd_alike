#include "engine/core/Time.hpp"

#include <algorithm>

namespace engine::core
{
Time::Time(double fixedDeltaSeconds)
    : m_fixedDeltaSeconds(fixedDeltaSeconds)
    , m_deltaSeconds(0.0)
    , m_totalSeconds(0.0)
    , m_lastFrameSeconds(0.0)
    , m_accumulator(0.0)
    , m_frameIndex(0)
    , m_firstFrame(true)
{
}

void Time::SetFixedDeltaSeconds(double fixedDeltaSeconds)
{
    m_fixedDeltaSeconds = std::clamp(fixedDeltaSeconds, 1.0 / 240.0, 1.0 / 15.0);
    m_accumulator = std::min(m_accumulator, m_fixedDeltaSeconds * 2.0);
}

void Time::BeginFrame(double nowSeconds)
{
    if (m_firstFrame)
    {
        m_lastFrameSeconds = nowSeconds;
        m_firstFrame = false;
    }

    const double rawDelta = nowSeconds - m_lastFrameSeconds;
    m_deltaSeconds = std::clamp(rawDelta, 0.0, 0.25);
    m_lastFrameSeconds = nowSeconds;
    m_totalSeconds += m_deltaSeconds;
    m_accumulator += m_deltaSeconds;
    ++m_frameIndex;
}

bool Time::ShouldRunFixedStep() const
{
    return m_accumulator >= m_fixedDeltaSeconds;
}

void Time::ConsumeFixedStep()
{
    m_accumulator -= m_fixedDeltaSeconds;
    if (m_accumulator < 0.0)
    {
        m_accumulator = 0.0;
    }
}

double Time::InterpolationAlpha() const
{
    return m_fixedDeltaSeconds > 0.0 ? (m_accumulator / m_fixedDeltaSeconds) : 0.0;
}
} // namespace engine::core
