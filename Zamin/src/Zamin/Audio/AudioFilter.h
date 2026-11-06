#pragma once

#include <cstdint>

namespace Zamin {

    // One second-order filter section (RBJ "Audio EQ Cookbook", Q = 0.707: Butterworth, no
    // resonance bump), 12 dB per octave past its cutoff.
    struct Biquad
    {
        float B0 = 1.0f, B1 = 0.0f, B2 = 0.0f, A1 = 0.0f, A2 = 0.0f;

        static Biquad LowPass(float cutoff, float sampleRate);
        static Biquad HighPass(float cutoff, float sampleRate);
        // The gain of the filter for a sine of `frequency` Hz (1 = unchanged).
        float GetResponse(float frequency, float sampleRate) const;
    };

    // A low-pass and a high-pass in series for up to MaxChannels interleaved channels: what the
    // filters of the mixer groups and the sounds run on the audio thread. Pure maths (headless).
    // A cutoff that changes glides there in the log domain (time constant ~30 ms) instead of
    // jumping; a filter switched on starts open and closes in, one switched off opens up and then
    // stops working (bypassed: the samples are left as they are).
    class AudioFilterChain
    {
    public:
        static constexpr uint32_t MaxChannels = 8;
        static constexpr float GlideSeconds = 0.03f;

        // Cutoffs in Hz; 0 = that filter off.
        void SetTarget(float lowPass, float highPass);
        // The first Process starts right at the targets (a sound that starts filtered). After
        // StartOpen it glides in from unfiltered instead (a filter added while the sound plays).
        void StartOpen()
        {
            m_Primed = true;
            m_LowPass = m_HighPass = 0.0f;
        }
        // Filters `frames` x `channels` interleaved samples in place.
        void Process(float* samples, uint32_t frames, uint32_t channels, float sampleRate);
        void Reset();

        // The cutoffs now (on their way to the targets); 0 = that filter is bypassed.
        float GetLowPass() const { return m_LowPass; }
        float GetHighPass() const { return m_HighPass; }
        bool IsBypassed() const { return m_LowPass <= 0.0f && m_HighPass <= 0.0f; }

    private:
        struct State
        {
            float Z1 = 0.0f, Z2 = 0.0f;
        };
        static float Glide(float current, float target, float k);
        void UpdateCoefficients(float sampleRate);

        float m_TargetLowPass = 0.0f, m_TargetHighPass = 0.0f;
        float m_LowPass = 0.0f, m_HighPass = 0.0f; // 0: bypassed
        float m_CoefficientsLowPass = -1.0f, m_CoefficientsHighPass = -1.0f, m_CoefficientsRate = 0.0f;
        bool m_Primed = false;
        Biquad m_LowPassBiquad, m_HighPassBiquad;
        State m_LowPassState[MaxChannels], m_HighPassState[MaxChannels];
    };

}
