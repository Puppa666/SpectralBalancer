#include "PluginProcessor.h"
#include "PluginEditor.h"

SpectralBalancerAudioProcessorEditor::SpectralBalancerAudioProcessorEditor(SpectralBalancerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&darkLookAndFeel);
    setSize(860, 580);

    // Setup Sliders
    auto setupSlider = [this](juce::Slider& s, const juce::String& suffix)
    {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 55, 18);
        s.setTextValueSuffix(suffix);
        addAndMakeVisible(s);
    };

    setupSlider(amountSlider, "%");
    setupSlider(slopeSlider, " dB");
    setupSlider(maxCorrSlider, " dB");
    setupSlider(hpfManualSlider, " Hz");

    hpfManualSlider.setColour(juce::Slider::thumbColourId, juce::Colour(0xfff59e0b));
    hpfManualSlider.setColour(juce::Slider::trackColourId, juce::Colour(0xfff59e0b));

    // Preset buttons
    auto setupPresetBtn = [this](juce::TextButton& btn, float targetSlope)
    {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff18181b));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffa1a1aa));
        btn.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff27272a));
        btn.onClick = [this, targetSlope]() { slopeSlider.setValue(targetSlope, juce::sendNotificationAsync); };
        addAndMakeVisible(btn);
    };

    setupPresetBtn(pinkPresetButton, -3.0f);
    setupPresetBtn(mixPresetButton, -4.5f);
    setupPresetBtn(darkPresetButton, -6.0f);

    // Bypass Button
    bypassButton.setButtonText("ACTIVE");
    bypassButton.onClick = [this]()
    {
        if (auto* param = dynamic_cast<juce::AudioParameterBool*>(audioProcessor.apvts.getParameter("bypass")))
            *param = !param->get();
    };
    addAndMakeVisible(bypassButton);

    // Mode Button
    modeButton.setButtonText("Continuous Auto");
    modeButton.onClick = [this]()
    {
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*>(audioProcessor.apvts.getParameter("mode")))
            *param = (param->getIndex() == 0 ? 1 : 0);
    };
    addAndMakeVisible(modeButton);

    // Re-Learn Button
    reLearnButton.setButtonText("Re-Learn");
    reLearnButton.onClick = [this]()
    {
        audioProcessor.triggerReLearn();
    };
    addAndMakeVisible(reLearnButton);

    // Auto HPF Button
    autoHpfButton.setButtonText("Auto");
    addAndMakeVisible(autoHpfButton);

    // Attachments
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "amount", amountSlider);
    slopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "slope", slopeSlider);
    maxCorrAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "maxCorr", maxCorrSlider);
    hpfManualAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.apvts, "hpfManual", hpfManualSlider);
    autoHpfAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.apvts, "autoHpf", autoHpfButton);

    inSpectrum.fill(-110.0f);
    outSpectrum.fill(-110.0f);

    startTimerHz(30);
}

SpectralBalancerAudioProcessorEditor::~SpectralBalancerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void SpectralBalancerAudioProcessorEditor::timerCallback()
{
    audioProcessor.getSpectrumData(inSpectrum, outSpectrum);

    // Update bypass button styling
    const bool isBypassed = *audioProcessor.apvts.getRawParameterValue("bypass") > 0.5f;
    if (isBypassed)
    {
        bypassButton.setButtonText("BYPASS ON");
        bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0x33f43f5e));
        bypassButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfff43f5e));
        bypassButton.setColour(juce::ComboBox::outlineColourId, juce::Colour(0x66f43f5e));
    }
    else
    {
        bypassButton.setButtonText("ACTIVE");
        bypassButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff18181b));
        bypassButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfffafafa));
        bypassButton.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3f3f46));
    }

    // Update mode button
    const int modeVal = static_cast<int>(*audioProcessor.apvts.getRawParameterValue("mode"));
    if (modeVal == 0)
    {
        modeButton.setButtonText("Continuous Auto");
        modeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0x2b064e3b));
        modeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff6ee7b7));
        modeButton.setColour(juce::ComboBox::outlineColourId, juce::Colour(0x6610b981));
    }
    else
    {
        modeButton.setButtonText("Hold Snapshot");
        modeButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0x2e451a03));
        modeButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xfffcd34d));
        modeButton.setColour(juce::ComboBox::outlineColourId, juce::Colour(0x66f59e0b));
    }

    // Update Re-Learn button
    if (audioProcessor.isLearning())
    {
        int pct = static_cast<int>(audioProcessor.getLearnProgress() * 100.0f);
        reLearnButton.setButtonText("Analyzing (" + juce::String(pct) + "%)...");
        reLearnButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffd97706));
        reLearnButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff09090b));
        reLearnButton.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xfff59e0b));
    }
    else
    {
        reLearnButton.setButtonText("Re-Learn");
        reLearnButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff18181b));
        reLearnButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe4e4e7));
        reLearnButton.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff27272a));
    }

    // Update Auto HPF button & slider state
    const bool isAutoHpf = *audioProcessor.apvts.getRawParameterValue("autoHpf") > 0.5f;
    hpfManualSlider.setEnabled(!isAutoHpf);
    autoHpfButton.setButtonText(isAutoHpf ? "Auto HPF" : "Manual HPF");
    autoHpfButton.setColour(juce::TextButton::buttonColourId, isAutoHpf ? juce::Colour(0x2b064e3b) : juce::Colour(0xff18181b));
    autoHpfButton.setColour(juce::TextButton::textColourOffId, isAutoHpf ? juce::Colour(0xff6ee7b7) : juce::Colour(0xffa1a1aa));
    autoHpfButton.setColour(juce::ComboBox::outlineColourId, isAutoHpf ? juce::Colour(0x6610b981) : juce::Colour(0xff27272a));

    repaint();
}

void SpectralBalancerAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff09090b)); // zinc-950

    // ==========================================================================
    // 1. Header Bar
    // ==========================================================================
    g.setColour(juce::Colour(0xff10b981));
    g.fillRoundedRectangle(24.0f, 18.0f, 6.0f, 26.0f, 3.0f);

    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.setColour(juce::Colours::white);
    g.drawText("SPECTRAL BALANCER", 38, 14, 230, 22, juce::Justification::left);

    const float curSlope = *audioProcessor.apvts.getRawParameterValue("slope");
    juce::String slopeText = juce::String(curSlope, 1) + " dB / OCT";
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff18181b));
    g.fillRoundedRectangle(275.0f, 16.0f, 100.0f, 18.0f, 4.0f);
    g.setColour(juce::Colour(0x6610b981));
    g.drawRoundedRectangle(275.0f, 16.0f, 100.0f, 18.0f, 4.0f, 1.0f);
    g.setColour(juce::Colour(0xff34d399));
    g.drawText(slopeText, 275, 16, 100, 18, juce::Justification::centred);

    g.setFont(juce::Font(11.0f, juce::Font::plain));
    g.setColour(juce::Colour(0xff71717a));
    g.drawText("Automatic Spectral Tilt & Low-End Correction", 38, 33, 300, 16, juce::Justification::left);

    // ==========================================================================
    // 2. Action row info: HPF status
    // ==========================================================================
    const bool isAutoHpf = *audioProcessor.apvts.getRawParameterValue("autoHpf") > 0.5f;
    const int hpfVal = static_cast<int>(std::round(isAutoHpf ? audioProcessor.getHpfCutoff() : *audioProcessor.apvts.getRawParameterValue("hpfManual")));
    juce::String hpfStatus = "HPF: " + juce::String(hpfVal) + " Hz (" + (isAutoHpf ? "Auto" : "Manual") + ")";

    g.setColour(juce::Colour(0xff18181b));
    g.fillRoundedRectangle(280.0f, 58.0f, 140.0f, 28.0f, 6.0f);
    g.setColour(juce::Colour(0xff27272a));
    g.drawRoundedRectangle(280.0f, 58.0f, 140.0f, 28.0f, 6.0f, 1.0f);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xfff59e0b));
    g.drawText(hpfStatus, 280, 58, 140, 28, juce::Justification::centred);

    // ==========================================================================
    // 3. Spectrum Canvas
    // ==========================================================================
    const float canvasX = 24.0f;
    const float canvasY = 96.0f;
    const float canvasW = 812.0f;
    const float canvasH = 216.0f;

    g.setColour(juce::Colour(0xff060608));
    g.fillRoundedRectangle(canvasX, canvasY, canvasW, canvasH, 8.0f);

    // Grid lines & labels
    g.setFont(juce::Font(9.0f, juce::Font::plain));
    for (float db = -100.0f; db <= -20.0f; db += 20.0f)
    {
        float y = dbToY(db, canvasY, canvasH);
        g.setColour(juce::Colour(0x14ffffff));
        g.drawHorizontalLine(static_cast<int>(y), canvasX, canvasX + canvasW);
        g.setColour(juce::Colour(0x40ffffff));
        g.drawText(juce::String(static_cast<int>(db)) + " dB", static_cast<int>(canvasX + 4), static_cast<int>(y - 10), 40, 10, juce::Justification::left);
    }

    const float freqGrid[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
    for (float f : freqGrid)
    {
        float x = freqToX(f, canvasX, canvasW);
        g.setColour(juce::Colour(0x14ffffff));
        g.drawVerticalLine(static_cast<int>(x), canvasY, canvasY + canvasH);
        g.setColour(juce::Colour(0x40ffffff));
        juce::String lbl = (f >= 1000.0f) ? (juce::String(static_cast<int>(f / 1000.0f)) + "k") : juce::String(static_cast<int>(f));
        g.drawText(lbl, static_cast<int>(x + 3), static_cast<int>(canvasY + canvasH - 14), 30, 12, juce::Justification::left);
    }

    // Clip to canvas area for curves
    {
        juce::Graphics::ScopedSaveState sss(g);
        g.reduceClipRegion(juce::Rectangle<int>(static_cast<int>(canvasX), static_cast<int>(canvasY),
                                                static_cast<int>(canvasW), static_cast<int>(canvasH)));

        // Pre-EQ Input Spectrum
        juce::Path prePath;
        for (int i = 0; i < SpectralBalancerAudioProcessor::specPoints; ++i)
        {
            float f = 20.0f * std::pow(1000.0f, static_cast<float>(i) / static_cast<float>(SpectralBalancerAudioProcessor::specPoints - 1));
            float x = freqToX(f, canvasX, canvasW);
            float y = dbToY(inSpectrum[i], canvasY, canvasH);
            if (i == 0) prePath.startNewSubPath(x, y);
            else prePath.lineTo(x, y);
        }
        g.setColour(juce::Colour(0x55a1a1aa));
        g.strokePath(prePath, juce::PathStrokeType(1.2f));

        // Post-EQ Output Spectrum
        const bool isBypassed = *audioProcessor.apvts.getRawParameterValue("bypass") > 0.5f;
        juce::Path postPath;
        for (int i = 0; i < SpectralBalancerAudioProcessor::specPoints; ++i)
        {
            float f = 20.0f * std::pow(1000.0f, static_cast<float>(i) / static_cast<float>(SpectralBalancerAudioProcessor::specPoints - 1));
            float x = freqToX(f, canvasX, canvasW);
            float y = dbToY(outSpectrum[i], canvasY, canvasH);
            if (i == 0) postPath.startNewSubPath(x, y);
            else postPath.lineTo(x, y);
        }
        g.setColour(isBypassed ? juce::Colour(0xd0f43f5e) : juce::Colour(0xd038bdf8));
        g.strokePath(postPath, juce::PathStrokeType(1.6f));

        // Target Slope Curve (anchored to 1 kHz level)
        const float ref1k = audioProcessor.getRef1kDb();
        juce::Path targetPath;
        for (int i = 0; i < SpectralBalancerAudioProcessor::specPoints; ++i)
        {
            float f = 20.0f * std::pow(1000.0f, static_cast<float>(i) / static_cast<float>(SpectralBalancerAudioProcessor::specPoints - 1));
            float x = freqToX(f, canvasX, canvasW);
            float targetDb = ref1k + curSlope * std::log2(f / 1000.0f);
            float y = dbToY(targetDb, canvasY, canvasH);
            if (i == 0) targetPath.startNewSubPath(x, y);
            else targetPath.lineTo(x, y);
        }
        g.setColour(juce::Colour(0xa010b981));
        const float dashLengths[] = { 5.0f, 4.0f };
        juce::Path dashedTargetPath;
        juce::PathStrokeType targetStroke(1.8f);
        targetStroke.createDashedPath(dashedTargetPath, targetPath, dashLengths, 2);
        g.strokePath(dashedTargetPath, targetStroke);

        // Filter EQ Response Curve (white line centered at baseY)
        const float baseY = canvasY + canvasH * 0.28f;
        const float pxPerDb = canvasH * 0.015f;
        g.setColour(juce::Colour(0x18ffffff));
        g.drawHorizontalLine(static_cast<int>(baseY), canvasX, canvasX + canvasW);

        juce::Path eqPath;
        for (int i = 0; i < SpectralBalancerAudioProcessor::specPoints; ++i)
        {
            float f = 20.0f * std::pow(1000.0f, static_cast<float>(i) / static_cast<float>(SpectralBalancerAudioProcessor::specPoints - 1));
            float x = freqToX(f, canvasX, canvasW);
            float eqDb = audioProcessor.getFilterResponseDb(f);
            float y = baseY - juce::jlimit(-18.0f, 18.0f, eqDb) * pxPerDb;
            if (i == 0) eqPath.startNewSubPath(x, y);
            else eqPath.lineTo(x, y);
        }
        g.setColour(isBypassed ? juce::Colour(0x40fafafa) : juce::Colour(0xf0fafafa));
        g.strokePath(eqPath, juce::PathStrokeType(1.8f));

        // HPF Cutoff Marker Line (amber dashed vertical line)
        float hpfCut = isAutoHpf ? audioProcessor.getHpfCutoff() : *audioProcessor.apvts.getRawParameterValue("hpfManual");
        float hpfX = freqToX(hpfCut, canvasX, canvasW);
        g.setColour(juce::Colour(0xccf59e0b));
        const float hpfDashes[] = { 3.0f, 3.0f };
        g.drawDashedLine(juce::Line<float>(hpfX, canvasY, hpfX, canvasY + canvasH), hpfDashes, 2, 1.5f);
    }

    g.setColour(juce::Colour(0xff27272a));
    g.drawRoundedRectangle(canvasX, canvasY, canvasW, canvasH, 8.0f, 1.0f);

    // Canvas legend
    g.setColour(juce::Colour(0xcc18181b));
    g.fillRoundedRectangle(canvasX + 8.0f, canvasY + 8.0f, 330.0f, 20.0f, 4.0f);
    g.setColour(juce::Colour(0xff27272a));
    g.drawRoundedRectangle(canvasX + 8.0f, canvasY + 8.0f, 330.0f, 20.0f, 4.0f, 1.0f);

    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff34d399));
    g.drawText("- - Target", canvasX + 14, canvasY + 8, 55, 20, juce::Justification::left);
    g.setColour(juce::Colour(0xffa1a1aa));
    g.drawText("— In", canvasX + 75, canvasY + 8, 35, 20, juce::Justification::left);
    g.setColour(juce::Colour(0xff38bdf8));
    g.drawText("— Out", canvasX + 115, canvasY + 8, 40, 20, juce::Justification::left);
    g.setColour(juce::Colour(0xfffafafa));
    g.drawText("— EQ", canvasX + 160, canvasY + 8, 40, 20, juce::Justification::left);
    g.setColour(juce::Colour(0xfff59e0b));
    g.drawText("¦ HPF " + juce::String(hpfVal) + "Hz", canvasX + 205, canvasY + 8, 90, 20, juce::Justification::left);

    // ==========================================================================
    // 4. 8 Band Correction Meter Cards
    // ==========================================================================
    const char* bandLabels[8] = { "100", "200", "400", "800", "1.6k", "3.2k", "6.4k", "12.8k" };
    const float cardY = 324.0f;
    const float cardH = 92.0f;
    const float cardSpacing = 8.0f;
    const float cardW = (canvasW - 7.0f * cardSpacing) / 8.0f;

    for (int b = 0; b < 8; ++b)
    {
        float x = canvasX + b * (cardW + cardSpacing);
        g.setColour(juce::Colour(0xff141417));
        g.fillRoundedRectangle(x, cardY, cardW, cardH, 6.0f);
        g.setColour(juce::Colour(0xff27272a));
        g.drawRoundedRectangle(x, cardY, cardW, cardH, 6.0f, 1.0f);

        // Band label
        g.setFont(juce::Font(10.5f, juce::Font::bold));
        g.setColour(juce::Colour(0xffa1a1aa));
        g.drawText(bandLabels[b], static_cast<int>(x), static_cast<int>(cardY + 4), static_cast<int>(cardW), 14, juce::Justification::centred);

        // Meter groove
        const float meterBoxX = x + 16.0f;
        const float meterBoxY = cardY + 22.0f;
        const float meterBoxW = cardW - 32.0f;
        const float meterBoxH = 46.0f;

        g.setColour(juce::Colour(0xff09090b));
        g.fillRoundedRectangle(meterBoxX, meterBoxY, meterBoxW, meterBoxH, 3.0f);

        // Zero line
        const float midY = meterBoxY + meterBoxH * 0.5f;
        g.setColour(juce::Colour(0xff3f3f46));
        g.drawHorizontalLine(static_cast<int>(midY), meterBoxX, meterBoxX + meterBoxW);

        // Band gain meter
        float corr = audioProcessor.getBandCorrection(b);
        float barH = juce::jlimit(0.0f, meterBoxH * 0.5f, std::abs(corr) * (meterBoxH * 0.5f / 10.0f));

        if (corr >= 0.05f)
        {
            g.setColour(juce::Colour(0xff10b981));
            g.fillRect(meterBoxX + 4.0f, midY - barH, meterBoxW - 8.0f, barH);
        }
        else if (corr <= -0.05f)
        {
            g.setColour(juce::Colour(0xfff43f5e));
            g.fillRect(meterBoxX + 4.0f, midY, meterBoxW - 8.0f, barH);
        }

        // Value text
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        if (std::abs(corr) < 0.2f)
            g.setColour(juce::Colour(0xff52525b));
        else if (corr > 0)
            g.setColour(juce::Colour(0xff34d399));
        else
            g.setColour(juce::Colour(0xfffb7185));

        juce::String valStr = (corr >= 0 ? "+" : "") + juce::String(corr, 1) + " dB";
        g.drawText(valStr, static_cast<int>(x), static_cast<int>(cardY + cardH - 20), static_cast<int>(cardW), 16, juce::Justification::centred);
    }

    // ==========================================================================
    // 5. Bottom Controls Cards
    // ==========================================================================
    const float botY = 426.0f;
    const float botH = 138.0f;
    const float colW = (canvasW - 2.0f * 12.0f) / 3.0f;

    // Card 1: Target Slope
    g.setColour(juce::Colour(0xff121215));
    g.fillRoundedRectangle(canvasX, botY, colW, botH, 8.0f);
    g.setColour(juce::Colour(0xff27272a));
    g.drawRoundedRectangle(canvasX, botY, colW, botH, 8.0f, 1.0f);

    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xfffafafa));
    g.drawText("Target Slope", static_cast<int>(canvasX + 14), static_cast<int>(botY + 10), 120, 16, juce::Justification::left);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xff34d399));
    g.drawText(juce::String(curSlope, 1) + " dB/oct", static_cast<int>(canvasX + colW - 100), static_cast<int>(botY + 10), 86, 16, juce::Justification::right);

    // Card 2: Correction Dynamics
    float col2X = canvasX + colW + 12.0f;
    g.setColour(juce::Colour(0xff121215));
    g.fillRoundedRectangle(col2X, botY, colW, botH, 8.0f);
    g.setColour(juce::Colour(0xff27272a));
    g.drawRoundedRectangle(col2X, botY, colW, botH, 8.0f, 1.0f);

    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xfffafafa));
    g.drawText("Correction", static_cast<int>(col2X + 14), static_cast<int>(botY + 10), 120, 16, juce::Justification::left);

    g.setFont(juce::Font(10.5f, juce::Font::plain));
    g.setColour(juce::Colour(0xffa1a1aa));
    g.drawText("Amount", static_cast<int>(col2X + 14), static_cast<int>(botY + 36), 100, 14, juce::Justification::left);
    g.drawText("Max per band", static_cast<int>(col2X + 14), static_cast<int>(botY + 84), 100, 14, juce::Justification::left);

    // Card 3: High-pass Filter
    float col3X = canvasX + 2.0f * (colW + 12.0f);
    g.setColour(juce::Colour(0xff121215));
    g.fillRoundedRectangle(col3X, botY, colW, botH, 8.0f);
    g.setColour(juce::Colour(0xff27272a));
    g.drawRoundedRectangle(col3X, botY, colW, botH, 8.0f, 1.0f);

    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xfffafafa));
    g.drawText("High-pass Filter", static_cast<int>(col3X + 14), static_cast<int>(botY + 10), 120, 16, juce::Justification::left);

    g.setFont(juce::Font(10.5f, juce::Font::plain));
    g.setColour(juce::Colour(0xffa1a1aa));
    g.drawText("Manual Cutoff (20-120 Hz)", static_cast<int>(col3X + 14), static_cast<int>(botY + 44), 160, 14, juce::Justification::left);
}

void SpectralBalancerAudioProcessorEditor::resized()
{
    // Bypass button in header
    bypassButton.setBounds(730, 16, 106, 28);

    // Mode and Re-Learn buttons
    modeButton.setBounds(24, 58, 126, 28);
    reLearnButton.setBounds(158, 58, 112, 28);

    const float canvasX = 24.0f;
    const float canvasW = 812.0f;
    const float botY = 426.0f;
    const float colW = (canvasW - 2.0f * 12.0f) / 3.0f;

    // Col 1: Slope presets & slider
    const int presetW = static_cast<int>((colW - 28.0f - 12.0f) / 3.0f);
    pinkPresetButton.setBounds(static_cast<int>(canvasX + 14), static_cast<int>(botY + 36), presetW, 24);
    mixPresetButton.setBounds(static_cast<int>(canvasX + 14 + presetW + 6), static_cast<int>(botY + 36), presetW, 24);
    darkPresetButton.setBounds(static_cast<int>(canvasX + 14 + (presetW + 6) * 2), static_cast<int>(botY + 36), presetW, 24);
    slopeSlider.setBounds(static_cast<int>(canvasX + 10), static_cast<int>(botY + 76), static_cast<int>(colW - 20), 34);

    // Col 2: Amount & MaxCorr sliders
    const float col2X = canvasX + colW + 12.0f;
    amountSlider.setBounds(static_cast<int>(col2X + 10), static_cast<int>(botY + 50), static_cast<int>(colW - 20), 28);
    maxCorrSlider.setBounds(static_cast<int>(col2X + 10), static_cast<int>(botY + 98), static_cast<int>(colW - 20), 28);

    // Col 3: HPF auto button & manual slider
    const float col3X = canvasX + 2.0f * (colW + 12.0f);
    autoHpfButton.setBounds(static_cast<int>(col3X + colW - 100), static_cast<int>(botY + 8), 86, 24);
    hpfManualSlider.setBounds(static_cast<int>(col3X + 10), static_cast<int>(botY + 66), static_cast<int>(colW - 20), 32);
}