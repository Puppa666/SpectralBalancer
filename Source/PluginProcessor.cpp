#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralBalancerAudioProcessor::SpectralBalancerAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()),
      forwardFFT(fftOrder),
      window(fftSize, juce::dsp::WindowingFunction<float>::hann)
{
    for (int i = 0; i < numBands; ++i)
        smoothedCorrections[i].reset(48000, 0.08);
}

SpectralBalancerAudioProcessor::~SpectralBalancerAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout SpectralBalancerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("amount", "Amount", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    return { params.begin(), params.end() };
}

void SpectralBalancerAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;

    hpfLeft.reset(); hpfRight.reset();
    // ASSEGNAZIONE DIRETTA DEL PUNTATORE (senza asterisco iniziale):
    hpfLeft.coefficients = FilterCoefs::makeHighPass(sampleRate, 30.0f);
    hpfRight.coefficients = FilterCoefs::makeHighPass(sampleRate, 30.0f);

    for (int i = 0; i < numBands; ++i)
    {
        peqLeft[i].reset(); peqRight[i].reset();
        auto coefs = FilterCoefs::makePeakFilter(sampleRate, bandFrequencies[i], 1.2f, 1.0f);
        peqLeft[i].coefficients = coefs;
        peqRight[i].coefficients = coefs;
        smoothedCorrections[i].reset(sampleRate, 0.08);
    }
}

void SpectralBalancerAudioProcessor::releaseResources() {}

void SpectralBalancerAudioProcessor::pushNextSampleIntoFifo(float sample)
{
    if (fifoIndex == fftSize)
    {
        if (!nextFFTBlockReady)
        {
            std::fill(fftData.begin(), fftData.end(), 0.0f);
            std::copy(fifo.begin(), fifo.end(), fftData.begin());
            nextFFTBlockReady = true;
        }
        fifoIndex = 0;
    }
    fifo[fifoIndex++] = sample;
}

void SpectralBalancerAudioProcessor::performSpectralAnalysis()
{
    window.multiplyWithWindowingTable(fftData.data(), fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform(fftData.data());

    const float sampleRate = static_cast<float>(getSampleRate());
    const float binHz = sampleRate / static_cast<float>(fftSize);

    float bandLevels[numBands] = { 0 };

    for (int i = 0; i < numBands; ++i)
    {
        int minBin = std::max(1, (int)((bandFrequencies[i] * 0.707f) / binHz));
        int maxBin = std::min((fftSize / 2) - 1, (int)((bandFrequencies[i] * 1.414f) / binHz));
        float sum = 0.0f;
        int count = 0;
        for (int b = minBin; b <= maxBin; ++b) {
            sum += fftData[b];
            count++;
        }
        bandLevels[i] = count > 0 ? (sum / count) : 0.001f;
    }

    // Auto Sub HPF
    int subMaxBin = std::max(1, (int)(60.0f / binHz));
    float subSum = 0.0f;
    for (int s = 1; s <= subMaxBin; ++s) subSum += fftData[s];
    float subAvg = subSum / subMaxBin;

    if (subAvg > 8.0f && bandLevels[0] < 5.0f) currentHpf = 45.0f;
    else if (subAvg > 15.0f) currentHpf = 65.0f;
    else currentHpf = 30.0f;

    // Normalizzazione a 1 kHz e calcolo Tilt Target
    float ref1k = (bandLevels[2] + bandLevels[3]) * 0.5f;
    ref1k = std::max(0.0001f, ref1k);

    for (int i = 0; i < numBands; ++i)
    {
        float actualDb = 20.0f * std::log10(std::max(0.0001f, bandLevels[i]) / ref1k);
        float diff = targetOffsets[i] - actualDb;
        float gain = juce::jlimit(-8.0f, 8.0f, diff * 0.75f);
        smoothedCorrections[i].setTargetValue(gain);
    }
}

void SpectralBalancerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    bool bypassed = *apvts.getRawParameterValue("bypass") > 0.5f;
    if (bypassed) return;

    float amount = *apvts.getRawParameterValue("amount") / 100.0f;

    // Sidechain Feed: push samples in mono into FIFO for analysis
    auto* channelDataLeft = buffer.getReadPointer(0);
    auto* channelDataRight = totalNumInputChannels > 1 ? buffer.getReadPointer(1) : channelDataLeft;

    for (int i = 0; i < numSamples; ++i)
    {
        pushNextSampleIntoFifo((channelDataLeft[i] + channelDataRight[i]) * 0.5f);
    }

    if (nextFFTBlockReady)
    {
        performSpectralAnalysis();
        nextFFTBlockReady = false;

        // Aggiorna filtri IIR
        double sr = getSampleRate();
        hpfLeft.coefficients = FilterCoefs::makeHighPass(sr, currentHpf);
        hpfRight.coefficients = FilterCoefs::makeHighPass(sr, currentHpf);

        for (int b = 0; b < numBands; ++b)
        {
            float targetGainDb = smoothedCorrections[b].getNextValue() * amount;
            float gainFactor = juce::Decibels::decibelsToGain(targetGainDb);
            auto coefs = FilterCoefs::makePeakFilter(sr, bandFrequencies[b], 1.2f, gainFactor);
            peqLeft[b].coefficients = coefs;
            peqRight[b].coefficients = coefs;
        }
    }

    // Processamento Wet
    juce::AudioBuffer<float> dryCopy;
    dryCopy.makeCopyOf(buffer);

    for (int i = 0; i < numSamples; ++i)
    {
        float l = buffer.getSample(0, i);
        float r = totalNumInputChannels > 1 ? buffer.getSample(1, i) : l;

        l = hpfLeft.processSample(l);
        r = hpfRight.processSample(r);

        for (int b = 0; b < numBands; ++b)
        {
            l = peqLeft[b].processSample(l);
            r = peqRight[b].processSample(r);
        }

        // Wet / Dry blend
        buffer.setSample(0, i, dryCopy.getSample(0, i) * (1.0f - amount) + l * amount);
        if (totalNumInputChannels > 1)
            buffer.setSample(1, i, dryCopy.getSample(1, i) * (1.0f - amount) + r * amount);
    }
}

juce::AudioProcessorEditor* SpectralBalancerAudioProcessor::createEditor()
{
    return new SpectralBalancerAudioProcessorEditor(*this);
}

void SpectralBalancerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void SpectralBalancerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralBalancerAudioProcessor();
}
bool SpectralBalancerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Supporta solo Stereo-in -> Stereo-out o Mono-in -> Mono-out
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}
