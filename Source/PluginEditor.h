#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ==============================================================================
// Custom Dark Look & Feel (Tailwind Zinc & Emerald styling)
// ==============================================================================
class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff09090b));
        setColour(juce::Slider::thumbColourId, juce::Colour(0xff10b981));
        setColour(juce::Slider::trackColourId, juce::Colour(0xff10b981));
        setColour(juce::Slider::backgroundColourId, juce::Colour(0xff27272a));
        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xfffafafa));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff18181b));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff3f3f46));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                          const juce::Slider::SliderStyle /*style*/, juce::Slider& slider) override
    {
        const float trackHeight = 4.0f;
        const float trackY = static_cast<float>(y) + (static_cast<float>(height) - trackHeight) * 0.5f;
        const float trackX = static_cast<float>(x);
        const float trackW = static_cast<float>(width);

        // Background track
        g.setColour(juce::Colour(0xff27272a));
        g.fillRoundedRectangle(trackX, trackY, trackW, trackHeight, 2.0f);

        // Value fill
        const float fillW = juce::jlimit(0.0f, trackW, sliderPos - trackX);
        juce::Colour accent = slider.findColour(juce::Slider::trackColourId);
        g.setColour(accent);
        g.fillRoundedRectangle(trackX, trackY, fillW, trackHeight, 2.0f);

        // Thumb
        const float thumbRadius = 6.5f;
        g.setColour(juce::Colours::white);
        g.fillEllipse(sliderPos - thumbRadius, trackY + trackHeight * 0.5f - thumbRadius, thumbRadius * 2.0f, thumbRadius * 2.0f);
        g.setColour(accent);
        g.drawEllipse(sliderPos - thumbRadius, trackY + trackHeight * 0.5f - thumbRadius, thumbRadius * 2.0f, thumbRadius * 2.0f, 1.5f);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& /*backgroundColour*/,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        juce::Colour bg = button.findColour(juce::TextButton::buttonColourId);

        if (shouldDrawButtonAsDown)
            bg = bg.brighter(0.2f);
        else if (shouldDrawButtonAsHighlighted)
            bg = bg.brighter(0.1f);

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 6.0f);

        g.setColour(button.findColour(juce::ComboBox::outlineColourId, true));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    }
};

// ==============================================================================
// Audio Processor Editor
// ==============================================================================
class SpectralBalancerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    SpectralBalancerAudioProcessorEditor(SpectralBalancerAudioProcessor&);
    ~SpectralBalancerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    SpectralBalancerAudioProcessor& audioProcessor;
    DarkLookAndFeel darkLookAndFeel;

    // Sliders
    juce::Slider amountSlider;
    juce::Slider slopeSlider;
    juce::Slider maxCorrSlider;
    juce::Slider hpfManualSlider;

    // Buttons
    juce::TextButton bypassButton;
    juce::TextButton modeButton;
    juce::TextButton reLearnButton;
    juce::TextButton autoHpfButton;

    // Preset buttons for Slope
    juce::TextButton pinkPresetButton { "Pink -3" };
    juce::TextButton mixPresetButton  { "Mix -4.5" };
    juce::TextButton darkPresetButton { "Dark -6" };

    // Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slopeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> maxCorrAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> hpfManualAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoHpfAttachment;

    // Spectrum display cache
    std::array<float, SpectralBalancerAudioProcessor::specPoints> inSpectrum;
    std::array<float, SpectralBalancerAudioProcessor::specPoints> outSpectrum;

    // Coordinate helpers for spectrum canvas
    static float freqToX(float f, float x0, float w) noexcept
    {
        return x0 + (std::log10(juce::jlimit(20.0f, 20000.0f, f) / 20.0f) / 3.0f) * w;
    }

    static float dbToY(float db, float y0, float h) noexcept
    {
        // Maps -105 dB to h (bottom) and -15 dB to 0 (top)
        const float norm = juce::jlimit(0.0f, 1.0f, (db - (-105.0f)) / ((-15.0f) - (-105.0f)));
        return y0 + h * (1.0f - norm);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralBalancerAudioProcessorEditor)
};