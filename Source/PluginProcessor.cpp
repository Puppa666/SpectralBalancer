#include "PluginProcessor.h"
#include "PluginEditor.h"

const float SpectralBalancerAudioProcessor::bandFrequencies[SpectralBalancerAudioProcessor::numBands] =
    { 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f, 12800.0f };

SpectralBalancerAudioProcessor::SpectralBalancerAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout()),
      forwardFFT(fftOrder),
      window(fftSize, juce::dsp::WindowingFunction<float>::hann)
{
    rawCorrections.fill(0.0f);
    learnAcc.fill(0.0f);

    for (int i = 0; i < specPoints; ++i)
    {
        displayFreqs[i] = 20.0f * std::pow(1000.0f, static_cast<float>(i) / static_cast<float>(specPoints - 1));
        inputSpectrumPoints[i] = -110.0f;
        outputSpectrumPoints[i] = -110.0f;
    }
}

SpectralBalancerAudioProcessor::~SpectralBalancerAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout SpectralBalancerAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "amount", "Amount", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "slope", "Target Slope", juce::NormalisableRange<float>(-7.5f, -1.5f, 0.1f), -4.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "maxCorr", "Max Correction", juce::NormalisableRange<float>(3.0f, 12.0f, 0.5f), 9.0f));

    juce::StringArray modes { "Continuous", "Hold" };
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode", modes, 0));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "autoHpf", "Auto HPF", true));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "hpfManual", "Manual HPF", juce::NormalisableRange<float>(20.0f, 120.0f, 1.0f), 30.0f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "bypass", "Bypass", false));

    return { params.begin(), params.end() };
}

bool SpectralBalancerAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void SpectralBalancerAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    hpfLeft.reset();
    hpfRight.reset();
    hpfLeft.setHighPass(sampleRate, 30.0f);
    hpfRight.setHighPass(sampleRate, 30.0f);

    for (int b = 0; b < numBands; ++b)
    {
        peqLeft[b].reset();
        peqRight[b].reset();
        peqLeft[b].setPeak(sampleRate, bandFrequencies[b], bandQ, 0.0f);
        peqRight[b].setPeak(sampleRate, bandFrequencies[b], bandQ, 0.0f);

        smoothedGains[b].reset(sampleRate, 0.05);
        smoothedGains[b].setCurrentAndTargetValue(0.0f);
    }

    smoothedHpf.reset(sampleRate, 0.06);
    smoothedHpf.setCurrentAndTargetValue(30.0f);

    smoothedBypass.reset(sampleRate, 0.02);
    smoothedBypass.setCurrentAndTargetValue(0.0f);

    fifoIndex = 0;
    nextFFTBlockReady = false;
}

void SpectralBalancerAudioProcessor::releaseResources() {}

void SpectralBalancerAudioProcessor::pushSamplesIntoFifo(float preSample, float postSample)
{
    if (fifoIndex >= fftSize)
    {
        if (!nextFFTBlockReady)
        {
            std::fill(fftDataPre.begin(), fftDataPre.end(), 0.0f);
            std::fill(fftDataPost.begin(), fftDataPost.end(), 0.0f);
            std::copy(fifoPre.begin(), fifoPre.end(), fftDataPre.begin());
            std::copy(fifoPost.begin(), fifoPost.end(), fftDataPost.begin());
            nextFFTBlockReady = true;
        }
        fifoIndex = 0;
    }

    fifoPre[fifoIndex] = preSample;
    fifoPost[fifoIndex] = postSample;
    ++fifoIndex;
}

void SpectralBalancerAudioProcessor::performSpectralAnalysis(float slope, float maxCorr, int modeVal)
{
    window.multiplyWithWindowingTable(fftDataPre.data(), fftSize);
    window.multiplyWithWindowingTable(fftDataPost.data(), fftSize);

    forwardFFT.performFrequencyOnlyForwardTransform(fftDataPre.data());
    forwardFFT.performFrequencyOnlyForwardTransform(fftDataPost.data());

    const float sampleRate = static_cast<float>(getSampleRate());
    const float binHz = sampleRate / static_cast<float>(fftSize);

    auto calcBandPower = [&](const float* fft, float loHz, float hiHz) -> float
    {
        int minBin = juce::jlimit(1, (fftSize / 2) - 1, static_cast<int>(std::floor(loHz / binHz)));
        int maxBin = juce::jlimit(minBin, (fftSize / 2) - 1, static_cast<int>(std::ceil(hiHz / binHz)));
        float sumPower = 0.0f;
        for (int b = minBin; b <= maxBin; ++b)
        {
            float normMag = fft[b] / static_cast<float>(fftSize);
            sumPower += normMag * normMag;
        }
        return sumPower / static_cast<float>(maxBin - minBin + 1);
    };

    // 1 kHz reference band (1 octave centered around 1000 Hz)
    float refPower = calcBandPower(fftDataPre.data(), 1000.0f * 0.707f, 1000.0f * 1.414f);
    float refDb = (refPower > 1e-12f) ? (10.0f * std::log10(refPower)) : -120.0f;

    // Sub-bass band (20 - 60 Hz)
    float subPower = calcBandPower(fftDataPre.data(), 20.0f, 60.0f);
    float subDb = (subPower > 1e-12f) ? (10.0f * std::log10(subPower)) : -120.0f;

    const bool hasSignal = (refDb >= -85.0f);

    if (hasSignal)
    {
        currentRef1kDb.store(refDb);

        float subTarget = targetRel(40.0f, slope);
        float subExcess = (subDb < -95.0f) ? 0.0f : (subDb - refDb - subTarget);
        smoothedSubExcess = smoothedSubExcess * 0.95f + subExcess * 0.05f;

        float autoCutoff = 30.0f;
        if (smoothedSubExcess > 12.0f)
            autoCutoff = 65.0f;
        else if (smoothedSubExcess > 6.0f)
            autoCutoff = 45.0f;

        currentHpfCutoff.store(autoCutoff);

        // Calculate corrections for all 8 bands
        for (int b = 0; b < numBands; ++b)
        {
            float bPower = calcBandPower(fftDataPre.data(), bandFrequencies[b] * 0.707f, bandFrequencies[b] * 1.414f);
            float bDb = (bPower > 1e-12f) ? (10.0f * std::log10(bPower)) : -120.0f;

            float diff = 0.0f;
            if (bDb >= -95.0f)
            {
                float target = targetRel(bandFrequencies[b], slope);
                float measuredDiff = bDb - refDb;
                diff = juce::jlimit(-maxCorr, maxCorr, target - measuredDiff);
            }

            if (isRelearning.load())
            {
                learnAcc[b] += diff;
            }
            else if (modeVal == 0) // Continuous mode
            {
                rawCorrections[b] = rawCorrections[b] * 0.96f + diff * 0.04f;
            }
        }

        if (isRelearning.load())
        {
            learnFrameCount++;
            learnProgress.store(static_cast<float>(learnFrameCount) / static_cast<float>(learnTargetFrames));

            if (learnFrameCount >= learnTargetFrames)
            {
                for (int b = 0; b < numBands; ++b)
                    rawCorrections[b] = learnAcc[b] / static_cast<float>(learnTargetFrames);

                isRelearning.store(false);
                learnProgress.store(0.0f);

                if (auto* modeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("mode")))
                    *modeParam = 1; // Switch to Hold mode automatically
            }
        }
    }

    // Compute log-spaced spectrum curves for GUI visualizer
    {
        juce::ScopedLock sl(spectrumLock);
        for (int i = 0; i < specPoints; ++i)
        {
            int bin = juce::jlimit(1, (fftSize / 2) - 1, static_cast<int>(std::round(displayFreqs[i] / binHz)));
            float preMag = (fftDataPre[bin] / static_cast<float>(fftSize));
            float postMag = (fftDataPost[bin] / static_cast<float>(fftSize));

            float preDbVal = (preMag > 1e-6f) ? (20.0f * std::log10(preMag)) : -110.0f;
            float postDbVal = (postMag > 1e-6f) ? (20.0f * std::log10(postMag)) : -110.0f;

            inputSpectrumPoints[i] = inputSpectrumPoints[i] * 0.75f + preDbVal * 0.25f;
            outputSpectrumPoints[i] = outputSpectrumPoints[i] * 0.75f + postDbVal * 0.25f;
        }
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

    const double sr = getSampleRate();
    if (sr <= 0.0 || numSamples <= 0) return;

    const float amount = *apvts.getRawParameterValue("amount") / 100.0f;
    const float slope = *apvts.getRawParameterValue("slope");
    const float maxCorr = *apvts.getRawParameterValue("maxCorr");
    const int modeVal = static_cast<int>(*apvts.getRawParameterValue("mode"));
    const bool autoHpf = *apvts.getRawParameterValue("autoHpf") > 0.5f;
    const float hpfManual = *apvts.getRawParameterValue("hpfManual");
    const bool bypassParam = *apvts.getRawParameterValue("bypass") > 0.5f;

    const float targetHpf = autoHpf ? currentHpfCutoff.load() : hpfManual;
    smoothedHpf.setTargetValue(targetHpf);
    smoothedBypass.setTargetValue(bypassParam ? 1.0f : 0.0f);

    for (int b = 0; b < numBands; ++b)
        smoothedGains[b].setTargetValue(rawCorrections[b] * amount);

    // Update filter coefficients at block boundary using smoothed values
    const float currentHpf = smoothedHpf.getNextValue();
    hpfLeft.setHighPass(sr, currentHpf);
    hpfRight.setHighPass(sr, currentHpf);

    for (int b = 0; b < numBands; ++b)
    {
        const float g = smoothedGains[b].getNextValue();
        peqLeft[b].setPeak(sr, bandFrequencies[b], bandQ, g);
        peqRight[b].setPeak(sr, bandFrequencies[b], bandQ, g);
    }

    auto* channelDataLeft = buffer.getWritePointer(0);
    auto* channelDataRight = (totalNumInputChannels > 1) ? buffer.getWritePointer(1) : channelDataLeft;

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = channelDataLeft[i];
        const float inR = (totalNumInputChannels > 1) ? channelDataRight[i] : inL;
        const float preMono = (inL + inR) * 0.5f;

        // Process through HPF + PEQ
        float procL = hpfLeft.process(inL);
        float procR = hpfRight.process(inR);

        for (int b = 0; b < numBands; ++b)
        {
            procL = peqLeft[b].process(procL);
            procR = peqRight[b].process(procR);
        }

        const float bypassVal = smoothedBypass.getNextValue();
        const float outL = procL * (1.0f - bypassVal) + inL * bypassVal;
        const float outR = procR * (1.0f - bypassVal) + inR * bypassVal;

        channelDataLeft[i] = outL;
        if (totalNumInputChannels > 1)
            channelDataRight[i] = outR;

        const float postMono = (outL + outR) * 0.5f;
        pushSamplesIntoFifo(preMono, postMono);
    }

    if (nextFFTBlockReady)
    {
        performSpectralAnalysis(slope, maxCorr, modeVal);
        nextFFTBlockReady = false;
    }
}

float SpectralBalancerAudioProcessor::getBandCorrection(int index) const
{
    if (index < 0 || index >= numBands) return 0.0f;
    const float amount = *apvts.getRawParameterValue("amount") / 100.0f;
    return rawCorrections[index] * amount;
}

void SpectralBalancerAudioProcessor::triggerReLearn()
{
    learnFrameCount = 0;
    learnAcc.fill(0.0f);
    learnProgress.store(0.0f);
    isRelearning.store(true);
}

void SpectralBalancerAudioProcessor::getSpectrumData(std::array<float, specPoints>& inData,
                                                    std::array<float, specPoints>& outData)
{
    juce::ScopedLock sl(spectrumLock);
    inData = inputSpectrumPoints;
    outData = outputSpectrumPoints;
}

float SpectralBalancerAudioProcessor::getFilterResponseDb(float freq) const
{
    double sr = getSampleRate();
    if (sr <= 0.0) sr = 48000.0;

    float totalMag = hpfLeft.getMagnitudeResponse(sr, freq);
    for (int b = 0; b < numBands; ++b)
        totalMag *= peqLeft[b].getMagnitudeResponse(sr, freq);

    return 20.0f * std::log10(std::max(1e-6f, totalMag));
}

juce::AudioProcessorEditor* SpectralBalancerAudioProcessor::createEditor()
{
    return new SpectralBalancerAudioProcessorEditor(*this);
}

void SpectralBalancerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr)
        copyXmlToBinary(*xml, destData);
}

void SpectralBalancerAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectralBalancerAudioProcessor();
}
