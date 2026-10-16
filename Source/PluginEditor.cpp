#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralBalancerAudioProcessorEditor::SpectralBalancerAudioProcessorEditor(SpectralBalancerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(620, 360);

    amountSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    amountSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    addAndMakeVisible(amountSlider);

    bypassButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(bypassButton);

    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "amount", amountSlider);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.apvts, "bypass", bypassButton);

    startTimerHz(24);
}

SpectralBalancerAudioProcessorEditor::~SpectralBalancerAudioProcessorEditor()
{
    stopTimer();
}

void SpectralBalancerAudioProcessorEditor::timerCallback()
{
    repaint();
}

void SpectralBalancerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff121214));

    // Titolo
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.drawText("SPECTRAL BALANCER (-4.5 dB/OCT)", 24, 18, 400, 24, juce::Justification::left);

    g.setFont(juce::Font(12.0f, juce::Font::plain));
    g.setColour(juce::Colours::grey);
    g.drawText("Sub HPF Auto: " + juce::String((int)audioProcessor.getHpfCutoff()) + " Hz", 24, 42, 200, 20, juce::Justification::left);

    // Visualizzazione 6 barre di correzione
    const char* labels[] = { "200", "400", "800", "1.6k", "3.2k", "6.4k" };
    int startX = 40;
    int startY = 80;
    int meterW = 40;
    int meterH = 120;
    int spacing = 55;

    for (int i = 0; i < 6; ++i)
    {
        int x = startX + i * spacing;
        g.setColour(juce::Colour(0xff1e1e24));
        g.fillRoundedRectangle(x, startY, meterW, meterH, 4.0f);

        // Zero line
        g.setColour(juce::Colours::darkgrey);
        g.drawHorizontalLine(startY + meterH / 2, x, x + meterW);

        float corr = audioProcessor.getBandCorrection(i);
        float barH = juce::jlimit(0.0f, (float)meterH / 2.0f, std::abs(corr) * 7.5f);

        if (corr >= 0)
        {
            g.setColour(juce::Colour(0xff10b981));
            g.fillRect((float)x + 12, (float)(startY + meterH / 2) - barH, (float)meterW - 24, barH);
        }
        else
        {
            g.setColour(juce::Colour(0xfff43f5e));
            g.fillRect((float)x + 12, (float)(startY + meterH / 2), (float)meterW - 24, barH);
        }

        // Testo frequenza e dB
        g.setColour(juce::Colours::lightgrey);
        g.setFont(10.0f);
        g.drawText(labels[i], x, startY + meterH + 4, meterW, 16, juce::Justification::centred);
        g.drawText(juce::String(corr, 1) + "dB", x - 5, startY + meterH + 20, meterW + 10, 16, juce::Justification::centred);
    }

    g.setColour(juce::Colours::white);
    g.setFont(13.0f);
    g.drawText("Amount", 420, 185, 120, 20, juce::Justification::centred);
}

void SpectralBalancerAudioProcessorEditor::resized()
{
    amountSlider.setBounds(420, 80, 120, 110);
    bypassButton.setBounds(435, 230, 90, 30);
}