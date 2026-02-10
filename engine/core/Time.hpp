#pragma once

namespace engine::core
{
class Time
{
public:
    explicit Time(double fixedDeltaSeconds = 1.0 / 60.0);

    void SetFixedDeltaSeconds(double fixedDeltaSeconds);

    void BeginFrame(double nowSeconds);
    bool ShouldRunFixedStep() const;
    void ConsumeFixedStep();

    [[nodiscard]] double DeltaSeconds() const { return m_deltaSeconds; }
    [[nodiscard]] double FixedDeltaSeconds() const { return m_fixedDeltaSeconds; }
    [[nodiscard]] double TotalSeconds() const { return m_totalSeconds; }
    [[nodiscard]] double InterpolationAlpha() const;
    [[nodiscard]] unsigned long long FrameIndex() const { return m_frameIndex; }

private:
    double m_fixedDeltaSeconds;
    double m_deltaSeconds;
    double m_totalSeconds;
    double m_lastFrameSeconds;
    double m_accumulator;
    unsigned long long m_frameIndex;
    bool m_firstFrame;
};
} // namespace engine::core
