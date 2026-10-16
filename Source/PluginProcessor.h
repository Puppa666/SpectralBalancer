#pragma once
#include <JuceHeader.h>

class SpectralBalancerAudioProcessor : public juce::AudioProcessor
{
public:
    SpectralBalancerAudioProcessor();
    ~SpectralBalancerAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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

    juce::AudioProcessorValueTreeState apvts;
    float getBandCorrection(int index) const { return smoothedCorrections[index].getCurrentValue(); }
    float getHpfCutoff() const { return currentHpf; }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // DSP DSP Filters
    static constexpr int numBands = 6;
    const float bandFrequencies[numBands] = { 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f };
    const float targetOffsets[numBands] = {
        4.5f * std::log2(1000.0f / 200.0f),
        4.5f * std::log2(1000.0f / 400.0f),
        4.5f * std::log2(1000.0f / 800.0f),
        -4.5f * std::log2(1600.0f / 1000.0f),
        -4.5f * std::log2(3200.0f / 1000.0f),
        -4.5f * std::log2(6400.0f / 1000.0f)
    };

    using Filter = juce::dsp::IIR::Filter<float>;
    using FilterCoefs = juce::dsp::IIR::Coefficients<float>;

    Filter hpfLeft, hpfRight;
    std::array<Filter, numBands> peqLeft;
    std::array<Filter, numBands> peqRight;

    // FFT Sidechain Engine (4096 bins)
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, fftSize * 2> fftData;
    std::array<float, fftSize> fifo;
    int fifoIndex = 0;
    bool nextFFTBlockReady = false;

    // Smoothing & Logic
    juce::SmoothedValue<float> smoothedCorrections[numBands];
    float currentHpf = 30.0f;

    void pushNextSampleIntoFifo(float sample);
    void performSpectralAnalysis();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralBalancerAudioProcessor)
};