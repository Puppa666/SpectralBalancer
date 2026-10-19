#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

// ==============================================================================
// Lightweight allocation-free Biquad Filter (Direct Form II Transposed)
// ==============================================================================
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    inline float process(float in) noexcept
    {
        float out = in * b0 + z1;
        z1 = in * b1 - out * a1 + z2;
        z2 = in * b2 - out * a2;
        return out;
    }

    void setPeak(double sampleRate, float freq, float q, float gainDb) noexcept
    {
        if (sampleRate <= 0.0) return;
        freq = juce::jlimit(15.0f, static_cast<float>(sampleRate * 0.48), freq);
        const float w0 = 2.0f * juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
        const float cosw = std::cos(w0);
        const float sinw = std::sin(w0);
        const float alpha = sinw / (2.0f * std::max(0.1f, q));
        const float A = std::pow(10.0f, gainDb / 40.0f);

        const float a0 = 1.0f + alpha / A;
        b0 = (1.0f + alpha * A) / a0;
        b1 = (-2.0f * cosw) / a0;
        b2 = (1.0f - alpha * A) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha / A) / a0;
    }

    void setHighPass(double sampleRate, float cutoff, float q = 0.7071f) noexcept
    {
        if (sampleRate <= 0.0) return;
        cutoff = juce::jlimit(15.0f, static_cast<float>(sampleRate * 0.48), cutoff);
        const float w0 = 2.0f * juce::MathConstants<float>::pi * cutoff / static_cast<float>(sampleRate);
        const float cosw = std::cos(w0);
        const float sinw = std::sin(w0);
        const float alpha = sinw / (2.0f * std::max(0.1f, q));

        const float a0 = 1.0f + alpha;
        b0 = ((1.0f + cosw) * 0.5f) / a0;
        b1 = (-(1.0f + cosw)) / a0;
        b2 = ((1.0f + cosw) * 0.5f) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float getMagnitudeResponse(double sampleRate, float freq) const noexcept
    {
        if (sampleRate <= 0.0) return 1.0f;
        const float w = 2.0f * juce::MathConstants<float>::pi * freq / static_cast<float>(sampleRate);
        const float cosw = std::cos(w);
        const float cos2w = std::cos(2.0f * w);
        const float sinw = std::sin(w);
        const float sin2w = std::sin(2.0f * w);

        const float numReal = b0 + b1 * cosw + b2 * cos2w;
        const float numImag = -b1 * sinw - b2 * sin2w;
        const float denReal = 1.0f + a1 * cosw + a2 * cos2w;
        const float denImag = -a1 * sinw - a2 * sin2w;

        const float numMagSq = numReal * numReal + numImag * numImag;
        const float denMagSq = denReal * denReal + denImag * denImag;
        return (denMagSq > 1e-12f) ? std::sqrt(numMagSq / denMagSq) : 1.0f;
    }
};

// ==============================================================================
// Audio Processor
// ==============================================================================
class SpectralBalancerAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int numBands = 8;
    static constexpr float bandQ = 1.2f;
    static constexpr int specPoints = 120;
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder; // 8192

    static const float bandFrequencies[numBands];

    SpectralBalancerAudioProcessor();
    ~SpectralBalancerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Spectral Balancer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // GUI interface methods
    float getBandCorrection(int index) const;
    float getHpfCutoff() const { return currentHpfCutoff.load(); }
    float getRef1kDb() const { return currentRef1kDb.load(); }
    bool isLearning() const { return isRelearning.load(); }
    float getLearnProgress() const { return learnProgress.load(); }
    void triggerReLearn();

    void getSpectrumData(std::array<float, specPoints>& inData,
                         std::array<float, specPoints>& outData);

    float getFilterResponseDb(float freq) const;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // DSP Filters
    Biquad hpfLeft, hpfRight;
    std::array<Biquad, numBands> peqLeft;
    std::array<Biquad, numBands> peqRight;

    // Smoothed values for click-free audio transitions
    juce::SmoothedValue<float> smoothedHpf;
    std::array<juce::SmoothedValue<float>, numBands> smoothedGains;
    juce::SmoothedValue<float> smoothedBypass;

    // FFT analysis
    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;

    std::array<float, fftSize * 2> fftDataPre;
    std::array<float, fftSize * 2> fftDataPost;
    std::array<float, fftSize> fifoPre;
    std::array<float, fftSize> fifoPost;
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;

    // Analysis and learning state
    std::array<float, numBands> rawCorrections;
    std::array<float, numBands> learnAcc;
    int learnFrameCount = 0;
    static constexpr int learnTargetFrames = 12; // ~2 seconds at 8192 samples (48kHz)

    float smoothedSubExcess = 0.0f;
    std::atomic<float> currentHpfCutoff { 30.0f };
    std::atomic<float> currentRef1kDb { -50.0f };
    std::atomic<bool> isRelearning { false };
    std::atomic<float> learnProgress { 0.0f };

    // Spectrum display curves (log-spaced between 20 Hz and 20 kHz)
    std::array<float, specPoints> displayFreqs;
    std::array<float, specPoints> inputSpectrumPoints;
    std::array<float, specPoints> outputSpectrumPoints;
    juce::CriticalSection spectrumLock;

    void pushSamplesIntoFifo(float preSample, float postSample);
    void performSpectralAnalysis(float slope, float maxCorr, int modeVal);

    static inline float targetRel(float freq, float slope) noexcept
    {
        return slope * std::log2(freq / 1000.0f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralBalancerAudioProcessor)
};
