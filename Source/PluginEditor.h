#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class SpectralBalancerAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    SpectralBalancerAudioProcessorEditor(SpectralBalancerAudioProcessor&);
    ~SpectralBalancerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    SpectralBalancerAudioProcessor& audioProcessor;
    juce::Slider amountSlider;
    juce::ToggleButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralBalancerAudioProcessorEditor)
};