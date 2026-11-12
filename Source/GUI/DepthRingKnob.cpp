#include "DepthRingKnob.h"

namespace AllyGUI
{

DepthRingKnob::DepthRingKnob(const DepthRingConfig& cfg, juce::AudioProcessorValueTreeState& apvts)
    : config(cfg),
      slider(*this)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                               juce::MathConstants<float>::pi * 2.75f,
                               true);
    slider.setMouseDragSensitivity(200);
    slider.setName(config.paramId);
    slider.setTitle(config.title);
    slider.setDescription(config.title + " Depth Ring");
    slider.setWantsKeyboardFocus(true);

    DepthRingLookAndFeel::configureSlider(slider, config.theme, config.icon);
    slider.setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, config.paramId, slider);

    if (auto* param = apvts.getParameter(config.paramId))
    {
        const float defaultPlain = param->convertFrom0to1(param->getDefaultValue());
        slider.setDoubleClickReturnValue(true, static_cast<double>(defaultPlain));
    }

    slider.onValueChange = [this]()
    {
        updateFormattedValue();
        repaint();
    };

    updateFormattedValue();
}

DepthRingKnob::~DepthRingKnob()
{
    slider.setLookAndFeel(nullptr);
}

void DepthRingKnob::enablementChanged()
{
    repaint();
}

float DepthRingKnob::getNormalizedSliderValue() const
{
    const double minV = slider.getMinimum();
    const double maxV = slider.getMaximum();
    if (maxV <= minV)
        return 0.0f;
    return static_cast<float>(juce::jlimit(0.0, 1.0, slider.getNormalisableRange().convertTo0to1(slider.getValue())));
}

EngineeredDepthTheme::ControlState DepthRingKnob::getCurrentVisualState() const
{
    auto state = DepthRingLookAndFeel::inspectSliderState(slider, getNormalizedSliderValue());
    if (!isEnabled())
        state.enabled = false;
    return state;
}

void DepthRingKnob::updateFormattedValue()
{
    const double val = slider.getValue();

    if (config.valueFormatter)
    {
        formattedValue = config.valueFormatter(val);
    }
    else if (config.unitSuffix == "%")
    {
        formattedValue = juce::String(juce::roundToInt(val * 100.0)) + " %";
    }
    else if (config.unitSuffix.isNotEmpty())
    {
        formattedValue = juce::String(val, config.valueDecimals) + " " + config.unitSuffix;
    }
    else
    {
        formattedValue = juce::String(val, config.valueDecimals);
    }
}

void DepthRingKnob::resized()
{
    const auto inner = getLocalBounds().toFloat().reduced(2.0f);
    if (inner.getWidth() < 40.0f || inner.getHeight() < 56.0f)
    {
        titleBounds = {};
        pillBounds = {};
        dialBounds = {};
        readoutBounds = {};
        slider.setBounds(0, 0, 0, 0);
        return;
    }

    // Proportional layout budget around the dial outer diameter D:
    // Vertical: title (0.145 D) + pill/gap (0.095 D) + dial (1.00 D) + gap (0.045 D) + readout (0.195 D) = 1.48 D
    // Horizontal: dial (1.00 D) + 4% side clearance = 1.04 D
    const float dialDiameter = juce::jmax(32.0f, juce::jmin(inner.getWidth() / 1.04f, inner.getHeight() / 1.48f));

    const float titleH    = juce::jmax(12.0f, dialDiameter * 0.145f);
    const float pillAreaH = juce::jmax(8.0f,  dialDiameter * 0.092f);
    const float bottomGap = juce::jmax(3.0f,  dialDiameter * 0.055f);
    const float readoutH  = juce::jmax(16.0f, dialDiameter * 0.168f);
    const float totalH    = titleH + pillAreaH + dialDiameter + bottomGap + readoutH;

    const float startY = inner.getY() + juce::jmax(0.0f, (inner.getHeight() - totalH) * 0.5f);
    const float cx = inner.getCentreX();

    titleBounds = { inner.getX(), startY, inner.getWidth(), titleH };

    const float pillW = juce::jlimit(18.0f, inner.getWidth() * 0.42f, dialDiameter * 0.24f);
    const float pillH = juce::jlimit(2.4f, 5.0f, dialDiameter * 0.023f);
    pillBounds = juce::Rectangle<float>(pillW, pillH)
                     .withCentre({ cx, startY + titleH + pillAreaH * 0.36f });

    dialBounds = juce::Rectangle<float>(dialDiameter, dialDiameter)
                     .withCentre({ cx, startY + titleH + pillAreaH + dialDiameter * 0.5f });

    const float readoutW = juce::jmin(inner.getWidth(), juce::jmax(72.0f, dialDiameter * 0.76f));
    readoutBounds = juce::Rectangle<float>(readoutW, readoutH)
                        .withCentre({ cx, dialBounds.getBottom() + bottomGap + readoutH * 0.5f });

    slider.setBounds(dialBounds.toNearestInt());
}

void DepthRingKnob::paint(juce::Graphics& g)
{
    if (dialBounds.isEmpty())
        return;

    const auto state = getCurrentVisualState();
    const auto palette = EngineeredDepthTheme::resolveAccent(config.theme, state);
    const float dialSize = dialBounds.getWidth();

    // 1. Focus outline without geometry shift
    if (state.focused)
    {
        EngineeredDepthTheme::drawFocusOutline(g,
                                               getLocalBounds().toFloat().reduced(1.0f),
                                               EngineeredDepthTheme::Metrics::radiusButton,
                                               palette.focus);
    }

    // 2. Centered uppercase parameter label
    const float titleFontPx = juce::jlimit(9.5f, 19.0f, dialSize * 0.098f);
    const auto titleColour = state.enabled
        ? (state.hovered || state.dragging ? EngineeredDepthTheme::Colors::textPrimary
                                           : EngineeredDepthTheme::Colors::textPrimary.withAlpha(0.95f))
        : EngineeredDepthTheme::Colors::disabled;

    EngineeredDepthTheme::drawTrackedText(g,
                                          config.title,
                                          titleBounds,
                                          EngineeredDepthTheme::makeUiFont(titleFontPx, false),
                                          titleColour,
                                          0.12f,
                                          juce::Justification::centred);

    // 3. Glowing accent pill indicator beneath title
    const float pillGlow = state.enabled ? (state.dragging ? 0.34f : (state.hovered ? 0.28f : 0.20f)) : 0.0f;
    EngineeredDepthTheme::drawAccentPill(g, pillBounds, palette.primary, pillGlow, state.enabled);

    // 4. Bottom bracketed readout ([ 0.50 NAP ])
    const float valueFontPx = juce::jlimit(8.5f, 16.0f, dialSize * 0.088f);
    const float bracketStroke = juce::jlimit(1.0f, 1.8f, dialSize * 0.0085f);

    EngineeredDepthTheme::drawBracketReadout(g,
                                             readoutBounds,
                                             formattedValue,
                                             EngineeredDepthTheme::makeDataFont(valueFontPx, false),
                                             palette.bracket,
                                             palette.textValue,
                                             bracketStroke,
                                             state.hovered || state.dragging);
}

} // namespace AllyGUI
