#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace AllyPresets
{

struct Preset
{
    juce::String name;
    float diepte;
    float polderPunch;
    float attackMs;
    float releaseMs;
    float havenDirt;
    float waterwijkGlide;
    int stadsdeelIndex;
    bool overdrive036;
    bool monoLock1300;
};

inline const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> presets = {
        // 1. Kasteel Drill Slide
        {
            "01_Kasteel_Drill_Slide",
            0.60f,   // Diepte
            0.00f,   // Polder Punch
            3.5f,    // Attack: 3.5 ms
            650.0f,  // Release: 650 ms
            0.45f,   // Haven Dirt (Centrum grit)
            85.0f,   // Waterwijk Glide (drill slides)
            1,       // Centrum (1315)
            true,    // 036 Overdrive: ON
            true     // 1300 Mono Lock: ON
        },
        // 2. Stedenwijk Boom
        {
            "02_Stedenwijk_Boom",
            0.75f,   // Diepte
            0.35f,   // Polder Punch
            1.5f,    // Attack: 1.5 ms
            160.0f,  // Release: 160 ms (tight)
            0.25f,   // Haven Dirt
            0.0f,    // Waterwijk Glide: 0
            0,       // Haven (1350)
            false,   // 036 Overdrive: OFF
            true     // 1300 Mono Lock: ON
        },
        // 3. Pampus Deep Sub
        {
            "03_Pampus_Deep_Sub",
            0.90f,   // Diepte (-4.5m NAP focus)
            0.00f,   // Polder Punch: 0
            18.0f,   // Attack: 18 ms velvety swell
            1200.0f, // Release: 1200 ms extended tail
            0.05f,   // Haven Dirt
            40.0f,   // Waterwijk Glide
            2,       // Poort (1363)
            false,   // 036 Overdrive: OFF
            true     // 1300 Mono Lock: ON
        },
        // 4. 42 BASS (DaBaby Raw Punchy Trap 808)
        {
            "04_42_BASS_DaBaby_Punch",
            0.70f,   // Diepte
            0.42f,   // Polder Punch: 42% raw acoustic punch
            1.2f,    // Attack: 1.2 ms ultra-fast punch
            380.0f,  // Release: 380 ms bouncy trap tail
            0.55f,   // Haven Dirt: aggressive crunch
            15.0f,   // Waterwijk Glide: quick 15ms bounce
            1,       // Centrum (1315 hard clip)
            true,    // 036 Overdrive: ON (+6dB parallel push)
            true     // 1300 Mono Lock: ON
        },
        // 5. Muziekwijk Pluck (Short percussive 808)
        {
            "05_Muziekwijk_Pluck",
            0.55f,   // Diepte
            0.50f,   // Polder Punch: punchy knock
            0.8f,    // Attack: 0.8 ms
            180.0f,  // Release: 180 ms short pluck
            0.20f,   // Haven Dirt
            0.0f,    // Waterwijk Glide: 0
            0,       // Haven (1350)
            false,   // 036 Overdrive: OFF
            true     // 1300 Mono Lock: ON
        },
        // 6. Striphelden Rage (Blown-out Carti / Rage bass)
        {
            "06_Striphelden_Rage",
            0.65f,   // Diepte
            0.25f,   // Polder Punch
            2.0f,    // Attack: 2.0 ms
            500.0f,  // Release: 500 ms
            0.80f,   // Haven Dirt: extreme foldback
            35.0f,   // Waterwijk Glide
            3,       // Buiten (1330 foldback)
            true,    // 036 Overdrive: ON
            true     // 1300 Mono Lock: ON
        },
        // 7. Noorderplassen Clean (Velvety liquid pure sub)
        {
            "07_Noorderplassen_Clean",
            0.85f,   // Diepte
            0.00f,   // Polder Punch: 0
            12.0f,   // Attack: 12 ms smooth
            850.0f,  // Release: 850 ms
            0.00f,   // Haven Dirt: 0% completely pure
            50.0f,   // Waterwijk Glide
            2,       // Poort (1363)
            false,   // 036 Overdrive: OFF
            true     // 1300 Mono Lock: ON
        },
        // 8. Kruidenwijk Detroit (Heavy punch Detroit bounce)
        {
            "08_Kruidenwijk_Detroit",
            0.72f,   // Diepte
            0.60f,   // Polder Punch: heavy knock
            1.0f,    // Attack: 1.0 ms
            280.0f,  // Release: 280 ms tight Detroit decay
            0.40f,   // Haven Dirt
            10.0f,   // Waterwijk Glide: 10 ms
            1,       // Centrum (1315)
            true,    // 036 Overdrive: ON
            true     // 1300 Mono Lock: ON
        },
        // 9. Waterwijk Liquid Glide (Smooth sliding 808)
        {
            "09_Waterwijk_Liquid_Glide",
            0.65f,   // Diepte
            0.00f,   // Polder Punch: 0
            8.0f,    // Attack: 8 ms
            900.0f,  // Release: 900 ms long slide
            0.30f,   // Haven Dirt
            120.0f,  // Waterwijk Glide: 120 ms long glide
            0,       // Haven (1350)
            false,   // 036 Overdrive: OFF
            true     // 1300 Mono Lock: ON
        },
        // 10. Oosterwold Warm Tube (Analog vintage 808)
        {
            "10_Oosterwold_Warm_Tube",
            0.78f,   // Diepte
            0.15f,   // Polder Punch: soft
            4.0f,    // Attack: 4 ms
            450.0f,  // Release: 450 ms
            0.60f,   // Haven Dirt: warm tape saturation
            25.0f,   // Waterwijk Glide
            0,       // Haven (1350)
            false,   // 036 Overdrive: OFF
            true     // 1300 Mono Lock: ON
        },
        // 11. Filmwijk Swell (Atmospheric slow cinematic sub)
        {
            "11_Filmwijk_Cinematic_Swell",
            0.92f,   // Diepte: ultra deep
            0.00f,   // Polder Punch: 0
            85.0f,   // Attack: 85 ms slow swell
            1800.0f, // Release: 1.8s long cinematic tail
            0.10f,   // Haven Dirt
            60.0f,   // Waterwijk Glide
            2,       // Poort (1363)
            false,   // 036 Overdrive: OFF
            true     // 1300 Mono Lock: ON
        },
        // 12. Almere Centrum Hardcore (Raw Dutch drill banger)
        {
            "12_Almere_Centrum_Hardcore",
            0.68f,   // Diepte
            0.30f,   // Polder Punch
            2.5f,    // Attack: 2.5 ms
            550.0f,  // Release: 550 ms
            0.70f,   // Haven Dirt: hard clipped
            65.0f,   // Waterwijk Glide: drill slides
            1,       // Centrum (1315)
            true,    // 036 Overdrive: ON
            true     // 1300 Mono Lock: ON
        },
        // 13. De Gouw Earthquake (25-45 Hz sub rumble)
        {
            "13_De_Gouw_Earthquake",
            1.00f,   // Diepte: 100% -4.5m sub focus
            0.00f,   // Polder Punch: 0
            15.0f,   // Attack: 15 ms
            1500.0f, // Release: 1.5s
            0.02f,   // Haven Dirt: pure sub
            30.0f,   // Waterwijk Glide
            2,       // Poort (1363)
            true,    // 036 Overdrive: ON (sub boost)
            true     // 1300 Mono Lock: ON
        },
        // 14. 42GRAM Club Banger (Aggressive festival 808)
        {
            "14_42GRAM_Club_Banger",
            0.72f,   // Diepte
            0.45f,   // Polder Punch: 45% thump
            1.8f,    // Attack: 1.8 ms
            420.0f,  // Release: 420 ms
            0.50f,   // Haven Dirt
            20.0f,   // Waterwijk Glide
            1,       // Centrum (1315)
            true,    // 036 Overdrive: ON
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
    if (auto* param = apvts.getParameter("release"))
        param->setValueNotifyingHost(param->convertTo0to1(p.releaseMs));
    if (auto* param = apvts.getParameter("havenDirt"))
        param->setValueNotifyingHost(param->convertTo0to1(p.havenDirt));
    if (auto* param = apvts.getParameter("waterwijkGlide"))
        param->setValueNotifyingHost(param->convertTo0to1(p.waterwijkGlide));
    if (auto* param = apvts.getParameter("stadsdeel"))
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(p.stadsdeelIndex)));
    if (auto* param = apvts.getParameter("overdrive036"))
        param->setValueNotifyingHost(p.overdrive036 ? 1.0f : 0.0f);
    if (auto* param = apvts.getParameter("monoLock1300"))
        param->setValueNotifyingHost(p.monoLock1300 ? 1.0f : 0.0f);
}

} // namespace AllyPresets
