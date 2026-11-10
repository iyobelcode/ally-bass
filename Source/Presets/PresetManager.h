#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace AllyPresets
{

struct Preset
{
    juce::String name;
    float diepte;
    float polderPunch;
    float attackMs;
    float verval;
    float releaseMs;
    float havenDirt;
    float weerwaterGlide;
    int stadsdeelIndex;
    bool overdrive036;
    bool monoLock1300;
};

inline const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets = {
        {
            "01_Kasteel_Drill_Slide",
            0.60f,   // Diepte
            0.00f,   // Polder Punch (zero kick chirp)
            4.0f,    // Attack: 4 ms smooth
            2.50f,   // Verval: 2.5s drill sustain
            180.0f,  // Release: 180 ms smooth release
            0.45f,   // Haven Dirt (smooth Centrum edge)
            85.0f,   // Weerwater Glide (smooth drill slide)
            1,       // Stadsdeel: Centrum (1315)
            true,    // 036 Overdrive: ON
            true     // 1300 Mono Lock: ON
        },
        {
            "02_Stedenwijk_Boom",
            0.75f,   // Diepte
            0.35f,   // Polder Punch (warm acoustic thud)
            1.5f,    // Attack: 1.5 ms punch
            0.55f,   // Verval: 0.55s tight boombap
            90.0f,   // Release: 90 ms
            0.25f,   // Haven Dirt (tape warmth)
            0.0f,    // Weerwater Glide: none
            0,       // Stadsdeel: Haven (1350)
            false,   // 036 Overdrive: OFF
            true     // 1300 Mono Lock: ON
        },
        {
            "03_Pampus_Deep_Sub",
            0.90f,   // Diepte (-4.5m pure sub)
            0.00f,   // Polder Punch: OFF
            18.0f,   // Attack: 18 ms velvety swell (zero click)
            3.80f,   // Verval: 3.8s deep sustain
            260.0f,  // Release: 260 ms extended tail
            0.05f,   // Haven Dirt: ultra clean
            40.0f,   // Weerwater Glide
            2,       // Stadsdeel: Poort (1363)
            false,   // 036 Overdrive: OFF
            true     // 1300 Mono Lock: ON
        }
    };
    return presets;
}

inline void applyPreset(juce::AudioProcessorValueTreeState& apvts, int presetIndex)
{
    const auto& presets = getFactoryPresets();
    if (presetIndex < 0 || presetIndex >= static_cast<int>(presets.size()))
        return;

    const auto& p = presets[static_cast<size_t>(presetIndex)];

    if (auto* param = apvts.getParameter("diepte"))
        param->setValueNotifyingHost(param->convertTo0to1(p.diepte));
    if (auto* param = apvts.getParameter("polderPunch"))
        param->setValueNotifyingHost(param->convertTo0to1(p.polderPunch));
    if (auto* param = apvts.getParameter("attack"))
        param->setValueNotifyingHost(param->convertTo0to1(p.attackMs));
    if (auto* param = apvts.getParameter("verval"))
        param->setValueNotifyingHost(param->convertTo0to1(p.verval));
    if (auto* param = apvts.getParameter("release"))
        param->setValueNotifyingHost(param->convertTo0to1(p.releaseMs));
    if (auto* param = apvts.getParameter("havenDirt"))
        param->setValueNotifyingHost(param->convertTo0to1(p.havenDirt));
    if (auto* param = apvts.getParameter("weerwaterGlide"))
        param->setValueNotifyingHost(param->convertTo0to1(p.weerwaterGlide));
    if (auto* param = apvts.getParameter("stadsdeel"))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(p.stadsdeelIndex)));
    if (auto* param = apvts.getParameter("overdrive036"))
        param->setValueNotifyingHost(p.overdrive036 ? 1.0f : 0.0f);
    if (auto* param = apvts.getParameter("monoLock1300"))
        param->setValueNotifyingHost(p.monoLock1300 ? 1.0f : 0.0f);
}

} // namespace AllyPresets
