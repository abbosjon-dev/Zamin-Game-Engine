#include "Zamin/Audio/AudioFilter.h"
#include "Zamin/Audio/AudioMixer.h"

#include <algorithm>
#include <cmath>

namespace Zamin {

    namespace {
        constexpr float s_Pi = 3.14159265358979f;
        constexpr float s_Q = 0.70710678f;
        constexpr uint32_t s_Block = 32; // frames between coefficient updates while gliding

        float ClampCutoff(float cutoff, float sampleRate)
        {
            // Below Nyquist (the formulas break down at it).
            return std::clamp(cutoff, AudioFilterSettings::MinCutoff, std::min(AudioFilterSettings::MaxCutoff, 0.45f * sampleRate));
        }

        void Run(const Biquad& b, float* samples, uint32_t frames, uint32_t channels, uint32_t channel, float& z1, float& z2)
        {
            // Transposed direct form II.
            for (uint32_t f = 0; f < frames; f++)
            {
                float& x = samples[f * channels + channel];
                float y = b.B0 * x + z1;
                z1 = b.B1 * x - b.A1 * y + z2;
                z2 = b.B2 * x - b.A2 * y;
                x = y;
            }
            // No denormals in the tail of a silence.
            if (std::abs(z1) < 1e-20f) z1 = 0.0f;
            if (std::abs(z2) < 1e-20f) z2 = 0.0f;
        }
    }

    Biquad Biquad::LowPass(float cutoff, float sampleRate)
    {
        const float w0 = 2.0f * s_Pi * ClampCutoff(cutoff, sampleRate) / sampleRate;
        const float cosw = std::cos(w0), alpha = std::sin(w0) / (2.0f * s_Q);
        const float a0 = 1.0f + alpha;
        Biquad b;
        b.B0 = (1.0f - cosw) * 0.5f / a0;
        b.B1 = (1.0f - cosw) / a0;
        b.B2 = b.B0;
        b.A1 = -2.0f * cosw / a0;
        b.A2 = (1.0f - alpha) / a0;
        return b;
    }

    Biquad Biquad::HighPass(float cutoff, float sampleRate)
    {
        const float w0 = 2.0f * s_Pi * ClampCutoff(cutoff, sampleRate) / sampleRate;
        const float cosw = std::cos(w0), alpha = std::sin(w0) / (2.0f * s_Q);
        const float a0 = 1.0f + alpha;
        Biquad b;
        b.B0 = (1.0f + cosw) * 0.5f / a0;
        b.B1 = -(1.0f + cosw) / a0;
        b.B2 = b.B0;
        b.A1 = -2.0f * cosw / a0;
        b.A2 = (1.0f - alpha) / a0;
        return b;
    }

    float Biquad::GetResponse(float frequency, float sampleRate) const
    {
        // |H(e^jw)| = |b0 + b1 z^-1 + b2 z^-2| / |1 + a1 z^-1 + a2 z^-2|
        const float w = 2.0f * s_Pi * frequency / sampleRate;
        const float c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2.0f * w), s2 = std::sin(2.0f * w);
        const float nr = B0 + B1 * c1 + B2 * c2, ni = -(B1 * s1 + B2 * s2);
        const float dr = 1.0f + A1 * c1 + A2 * c2, di = -(A1 * s1 + A2 * s2);
        return std::sqrt((nr * nr + ni * ni) / std::max(1e-20f, dr * dr + di * di));
    }

    void AudioFilterChain::SetTarget(float lowPass, float highPass)
    {
        m_TargetLowPass = std::isfinite(lowPass) && lowPass > 0.0f ? std::clamp(lowPass, AudioFilterSettings::MinCutoff, AudioFilterSettings::MaxCutoff) : 0.0f;
        m_TargetHighPass = std::isfinite(highPass) && highPass > 0.0f ? std::clamp(highPass, AudioFilterSettings::MinCutoff, AudioFilterSettings::MaxCutoff) : 0.0f;
    }

    void AudioFilterChain::Reset()
    {
        for (uint32_t c = 0; c < MaxChannels; c++)
            m_LowPassState[c] = m_HighPassState[c] = State();
        m_LowPass = m_HighPass = 0.0f;
        m_Primed = false;
    }

    float AudioFilterChain::Glide(float current, float target, float k)
    {
        // current -> target by a fraction of the way each block, in octaves (log domain).
        float next = target * std::pow(current / target, k);
        if (std::abs(std::log(next / target)) < 0.002f)
            next = target;
        return next;
    }

    void AudioFilterChain::UpdateCoefficients(float sampleRate)
    {
        if (m_LowPass > 0.0f && (m_LowPass != m_CoefficientsLowPass || sampleRate != m_CoefficientsRate))
        {
            m_LowPassBiquad = Biquad::LowPass(m_LowPass, sampleRate);
            m_CoefficientsLowPass = m_LowPass;
        }
        if (m_HighPass > 0.0f && (m_HighPass != m_CoefficientsHighPass || sampleRate != m_CoefficientsRate))
        {
            m_HighPassBiquad = Biquad::HighPass(m_HighPass, sampleRate);
            m_CoefficientsHighPass = m_HighPass;
        }
        m_CoefficientsRate = sampleRate;
    }

    void AudioFilterChain::Process(float* samples, uint32_t frames, uint32_t channels, float sampleRate)
    {
        if (!samples || frames == 0 || channels == 0 || !(sampleRate > 0.0f))
            return;
        const uint32_t filtered = std::min(channels, MaxChannels);
        if (!m_Primed)
        {
            // Straight to the targets (a new filter, or one asked to start where it is set).
            m_Primed = true;
            m_LowPass = m_TargetLowPass;
            m_HighPass = m_TargetHighPass;
        }
        const float open = AudioFilterSettings::MaxCutoff, closed = AudioFilterSettings::MinCutoff;
        for (uint32_t start = 0; start < frames; start += s_Block)
        {
            const uint32_t count = std::min(s_Block, frames - start);
            const float k = std::exp(-(float)count / sampleRate / GlideSeconds);
            // Low-pass: switched on it starts fully open; switched off it opens up, then is bypassed.
            if (m_LowPass != m_TargetLowPass)
            {
                if (m_LowPass <= 0.0f)
                {
                    m_LowPass = open;
                    for (State& s : m_LowPassState) s = State();
                }
                float target = m_TargetLowPass > 0.0f ? m_TargetLowPass : open;
                m_LowPass = Glide(m_LowPass, target, k);
                if (m_TargetLowPass <= 0.0f && m_LowPass >= open)
                    m_LowPass = 0.0f;
            }
            // High-pass: the same from the bottom.
            if (m_HighPass != m_TargetHighPass)
            {
                if (m_HighPass <= 0.0f)
                {
                    m_HighPass = closed;
                    for (State& s : m_HighPassState) s = State();
                }
                float target = m_TargetHighPass > 0.0f ? m_TargetHighPass : closed;
                m_HighPass = Glide(m_HighPass, target, k);
                if (m_TargetHighPass <= 0.0f && m_HighPass <= closed)
                    m_HighPass = 0.0f;
            }
            if (IsBypassed())
                continue;
            UpdateCoefficients(sampleRate);
            float* block = samples + (size_t)start * channels;
            for (uint32_t c = 0; c < filtered; c++)
            {
                if (m_LowPass > 0.0f)
                    Run(m_LowPassBiquad, block, count, channels, c, m_LowPassState[c].Z1, m_LowPassState[c].Z2);
                if (m_HighPass > 0.0f)
                    Run(m_HighPassBiquad, block, count, channels, c, m_HighPassState[c].Z1, m_HighPassState[c].Z2);
            }
        }
    }

}
