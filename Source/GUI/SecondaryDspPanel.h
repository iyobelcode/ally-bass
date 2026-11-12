#pragma once

#include <array>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "DepthRingKnob.h"
#include "EngineeredDepthComboBox.h"
#include "EngineeredDepthTheme.h"
#include "EngineeredDepthToggle.h"

namespace AllyGUI
{

/**
 * Chamfered dark-metallic Secondary DSP console bar hosting:
 * STADSDEEL DSP selector | ATTACK & RELEASE Mini Depth Rings | 036 OVERDRIVE | 1300 MONO LOCK
 */
class SecondaryDspPanel : public juce::Component
{
public:
    explicit SecondaryDspPanel(juce::AudioProcessorValueTreeState& apvts);
    ~SecondaryDspPanel() override = default;

    void resized() override;
    void paint(juce::Graphics& g) override;

    EngineeredDepthComboBox* getStadsdeelControl() noexcept { return stadsdeelControl.get(); }
    const EngineeredDepthComboBox* getStadsdeelControl() const noexcept { return stadsdeelControl.get(); }

    DepthRingKnob* getAttackKnob() noexcept { return attackKnob.get(); }
    const DepthRingKnob* getAttackKnob() const noexcept { return attackKnob.get(); }

    DepthRingKnob* getReleaseKnob() noexcept { return releaseKnob.get(); }
    const DepthRingKnob* getReleaseKnob() const noexcept { return releaseKnob.get(); }

    EngineeredDepthToggle* getOverdriveToggle() noexcept { return overdriveToggle.get(); }
    const EngineeredDepthToggle* getOverdriveToggle() const noexcept { return overdriveToggle.get(); }

    EngineeredDepthToggle* getMonoLockToggle() noexcept { return monoLockToggle.get(); }
    const EngineeredDepthToggle* getMonoLockToggle() const noexcept { return monoLockToggle.get(); }

    juce::Rectangle<float> getChassisBounds() const noexcept { return chassisBounds; }
    float getDividerX(int index) const noexcept;
    size_t getEstimatedCacheBytes() const noexcept;

private:
    void renderPanelToCache(juce::Graphics& g) const;

    std::unique_ptr<EngineeredDepthComboBox> stadsdeelControl;
    std::unique_ptr<DepthRingKnob> attackKnob;
    std::unique_ptr<DepthRingKnob> releaseKnob;
    std::unique_ptr<EngineeredDepthToggle> overdriveToggle;
    std::unique_ptr<EngineeredDepthToggle> monoLockToggle;

    juce::Rectangle<float> chassisBounds;
    std::array<float, 3> dividerX {};

    mutable juce::Image cachedPanelImage;
    mutable int cachedW = 0;
    mutable int cachedH = 0;
    mutable float cachedScale = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SecondaryDspPanel)
};

} // namespace AllyGUI
