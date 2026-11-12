#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <algorithm>

namespace AllyPresets
{

enum class PresetKind
{
    Init = 0,
    Factory = 1,
    User = 2
};

struct PresetStateInfo
{
    PresetKind kind = PresetKind::Init;
    int factoryIndex = -1;
    juce::String displayName { "Init" };
    juce::String filePath;
    bool modified = false;

    bool operator==(const PresetStateInfo& other) const noexcept
    {
        return kind == other.kind
            && factoryIndex == other.factoryIndex
            && displayName == other.displayName
            && filePath == other.filePath
            && modified == other.modified;
    }

    bool operator!=(const PresetStateInfo& other) const noexcept { return !(*this == other); }
};

struct UserPresetEntry
{
    juce::String name;
    juce::File file;
};

inline constexpr int kPresetSchemaVersion = 1;
inline constexpr const char* kPresetFileExtension = ".allypreset";
inline constexpr const char* kPresetFileWildcard = "*.allypreset";
inline constexpr const char* kPresetXmlRootTag = "ALLY1300Preset";

inline const juce::Identifier propPresetKind     { "allyPresetKind" };
inline const juce::Identifier propPresetIndex    { "allyPresetIndex" };
inline const juce::Identifier propPresetName     { "allyPresetName" };
inline const juce::Identifier propPresetPath     { "allyPresetPath" };
inline const juce::Identifier propPresetModified { "allyPresetModified" };
inline const juce::Identifier propPresetApplying { "allyPresetApplying" };
inline const juce::Identifier propPresetRevision { "allyPresetRevision" };

inline const juce::StringArray& getAllParameterIds()
{
    static const juce::StringArray ids {
        "diepte",
        "polderPunch",
        "havenDirt",
        "waterwijkGlide",
        "masterVolume",
        "attack",
        "release",
        "stadsdeel",
        "overdrive036",
        "monoLock1300"
    };
    return ids;
}

inline int wrapPresetIndex(int index, int count) noexcept
{
    if (count <= 0)
        return 0;
    return ((index % count) + count) % count;
}

inline juce::File getDefaultUserPresetsDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("JUICED UNICORN")
        .getChildFile("ALLY 1300 SUB SYNTH")
        .getChildFile("Presets");
}

inline juce::String sanitizePresetDisplayName(const juce::String& raw)
{
    juce::String cleaned = raw.replaceCharacters("\r\n\t", "   ").trim();
    if (cleaned.length() > 64)
        cleaned = cleaned.substring(0, 64).trim();
    return cleaned;
}

inline juce::String sanitizePresetFilename(const juce::String& raw)
{
    auto name = sanitizePresetDisplayName(raw);
    name = name.replaceCharacters("/\\:*?\"<>|", "_________")
               .replace("..", "_");
    while (name.contains("__"))
        name = name.replace("__", "_");
    name = name.trimCharactersAtStart(". _").trimCharactersAtEnd(". _");
    return name.isNotEmpty() ? name : juce::String("User_Preset");
}

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
            1.0f,    // Attack: 1.0 ms (matches 1-300 ms APVTS range)
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

inline bool isFactoryPresetName(const juce::String& candidate)
{
    const auto trimmed = sanitizePresetDisplayName(candidate);
    if (trimmed.isEmpty())
        return false;

    const auto& presets = getFactoryPresets();
    for (const auto& p : presets)
    {
        if (p.name.equalsIgnoreCase(trimmed))
            return true;
    }
    return false;
}

inline void writePresetMetadata(juce::ValueTree& stateTree,
                                PresetKind kind,
                                int factoryIndex,
                                const juce::String& name,
                                const juce::String& filePath,
                                bool modified)
{
    stateTree.setProperty(propPresetKind, static_cast<int>(kind), nullptr);
    stateTree.setProperty(propPresetIndex, factoryIndex, nullptr);
    stateTree.setProperty(propPresetName, name, nullptr);
    stateTree.setProperty(propPresetPath, filePath, nullptr);
    stateTree.setProperty(propPresetModified, modified, nullptr);
}

inline PresetStateInfo readPresetMetadata(const juce::ValueTree& stateTree,
                                          bool modifiedOverride = false,
                                          bool useModifiedOverride = false)
{
    PresetStateInfo info;

    if (!stateTree.hasProperty(propPresetKind))
    {
        info.kind = PresetKind::Init;
        info.factoryIndex = -1;
        info.displayName = "Init";
        info.filePath = {};
        info.modified = useModifiedOverride ? modifiedOverride : false;
        return info;
    }

    const int rawKind = static_cast<int>(stateTree.getProperty(propPresetKind, 0));
    if (rawKind == static_cast<int>(PresetKind::Factory))
        info.kind = PresetKind::Factory;
    else if (rawKind == static_cast<int>(PresetKind::User))
        info.kind = PresetKind::User;
    else
        info.kind = PresetKind::Init;

    info.factoryIndex = static_cast<int>(stateTree.getProperty(propPresetIndex, -1));
    info.displayName = stateTree.getProperty(propPresetName, juce::String("Init")).toString();
    info.filePath = stateTree.getProperty(propPresetPath, juce::String()).toString();
    info.modified = useModifiedOverride
        ? modifiedOverride
        : static_cast<bool>(stateTree.getProperty(propPresetModified, false));

    if (info.kind == PresetKind::Factory)
    {
        const auto& presets = getFactoryPresets();
        if (info.factoryIndex >= 0 && info.factoryIndex < static_cast<int>(presets.size()))
            info.displayName = presets[static_cast<size_t>(info.factoryIndex)].name;
        else
            info.kind = PresetKind::Init;
    }
    else if (info.displayName.isEmpty())
    {
        info.displayName = (info.kind == PresetKind::User) ? "User Preset" : "Init";
    }

    return info;
}

inline void bumpPresetRevision(juce::ValueTree& stateTree)
{
    const int currentRev = static_cast<int>(stateTree.getProperty(propPresetRevision, 0));
    stateTree.setProperty(propPresetRevision, currentRev + 1, nullptr);
}

inline void applyPreset(juce::AudioProcessorValueTreeState& apvts, int presetIndex)
{
    const auto& presets = getFactoryPresets();
    if (presetIndex < 0 || presetIndex >= static_cast<int>(presets.size()))
        return;

    const auto& p = presets[static_cast<size_t>(presetIndex)];

    apvts.state.setProperty(propPresetApplying, true, nullptr);

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

    writePresetMetadata(apvts.state, PresetKind::Factory, presetIndex, p.name, {}, false);
    bumpPresetRevision(apvts.state);
    apvts.state.setProperty(propPresetApplying, false, nullptr);
}

inline std::unique_ptr<juce::XmlElement> buildUserPresetXml(juce::AudioProcessorValueTreeState& apvts,
                                                            const juce::String& presetName,
                                                            const juce::File& targetFile = {},
                                                            const juce::String& pluginVersion = "1.0.0")
{
    auto cleanName = sanitizePresetDisplayName(presetName);
    if (cleanName.isEmpty())
        cleanName = "User_Preset";

    auto root = std::make_unique<juce::XmlElement>(kPresetXmlRootTag);
    root->setAttribute("schemaVersion", kPresetSchemaVersion);
    root->setAttribute("pluginVersion", pluginVersion);

    auto* meta = root->createNewChildElement("Meta");
    meta->setAttribute("name", cleanName);
    meta->setAttribute("createdUtc", juce::Time::getCurrentTime().toISO8601(true));

    auto stateCopy = apvts.copyState();
    stateCopy.removeProperty(propPresetApplying, nullptr);
    stateCopy.removeProperty(propPresetRevision, nullptr);
    writePresetMetadata(stateCopy,
                        PresetKind::User,
                        -1,
                        cleanName,
                        targetFile.getFullPathName(),
                        false);

    if (auto paramXml = stateCopy.createXml())
        root->addChildElement(paramXml.release());

    return root;
}

inline bool savePresetToFile(juce::AudioProcessorValueTreeState& apvts,
                             const juce::File& targetFileIn,
                             const juce::String& presetNameIn,
                             bool allowOverwrite,
                             juce::String& outError,
                             const juce::String& pluginVersion = "1.0.0")
{
    const auto cleanName = sanitizePresetDisplayName(presetNameIn);
    if (cleanName.isEmpty())
    {
        outError = "Voer een geldige presetnaam in.";
        return false;
    }

    if (isFactoryPresetName(cleanName))
    {
        outError = "Factory presets kunnen niet worden overschreven. Kies een eigen naam.";
        return false;
    }

    juce::File targetFile = targetFileIn;
    if (!targetFile.hasFileExtension(kPresetFileExtension))
        targetFile = targetFile.withFileExtension("allypreset");

    if (!allowOverwrite && targetFile.existsAsFile())
    {
        outError = "Presetbestand bestaat al. Bevestig overschrijven of kies een nieuwe naam.";
        return false;
    }

    const auto parentDir = targetFile.getParentDirectory();
    if (!parentDir.isDirectory() && !parentDir.createDirectory())
    {
        outError = "Kon de presetmap niet aanmaken: " + parentDir.getFullPathName();
        return false;
    }

    const auto xml = buildUserPresetXml(apvts, cleanName, targetFile, pluginVersion);
    if (xml == nullptr)
    {
        outError = "Kon preset-XML niet genereren.";
        return false;
    }

    if (!targetFile.replaceWithText(xml->toString()))
    {
        outError = "Kon presetbestand niet opslaan naar: " + targetFile.getFullPathName();
        return false;
    }

    apvts.state.setProperty(propPresetApplying, true, nullptr);
    writePresetMetadata(apvts.state,
                        PresetKind::User,
                        -1,
                        cleanName,
                        targetFile.getFullPathName(),
                        false);
    bumpPresetRevision(apvts.state);
    apvts.state.setProperty(propPresetApplying, false, nullptr);

    outError.clear();
    return true;
}

inline bool loadPresetFromFile(juce::AudioProcessorValueTreeState& apvts,
                               const juce::File& sourceFile,
                               juce::String& outPresetName,
                               juce::String& outError)
{
    if (!sourceFile.existsAsFile())
    {
        outError = "Presetbestand bestaat niet.";
        return false;
    }

    const auto xml = juce::XmlDocument::parse(sourceFile);
    if (xml == nullptr || !xml->hasTagName(kPresetXmlRootTag))
    {
        outError = "Ongeldig presetformaat (verwacht ALLY1300Preset XML).";
        return false;
    }

    const int schemaVersion = xml->getIntAttribute("schemaVersion", 0);
    if (schemaVersion < 1 || schemaVersion > kPresetSchemaVersion)
    {
        outError = "Niet-ondersteunde presetversie: " + juce::String(schemaVersion);
        return false;
    }

    const auto* paramsXml = xml->getChildByName(apvts.state.getType());
    if (paramsXml == nullptr)
    {
        outError = "Preset mist parameterdata (" + apvts.state.getType().toString() + ").";
        return false;
    }

    auto parsedTree = juce::ValueTree::fromXml(*paramsXml);
    if (!parsedTree.isValid() || parsedTree.getType() != apvts.state.getType())
    {
        outError = "Preset-parameterstructuur is beschadigd.";
        return false;
    }

    // Verify at least one genuine PARAM entry belongs to this plugin before mutating live state
    const auto& knownIds = getAllParameterIds();
    int matchedParams = 0;
    for (int i = 0; i < parsedTree.getNumChildren(); ++i)
    {
        const auto child = parsedTree.getChild(i);
        if (child.hasType("PARAM") && knownIds.contains(child.getProperty("id").toString()))
            ++matchedParams;
    }
    if (matchedParams == 0)
    {
        outError = "Preset bevat geen bekende ALLY 1300-parameters.";
        return false;
    }

    juce::String metaName;
    if (const auto* metaXml = xml->getChildByName("Meta"))
        metaName = sanitizePresetDisplayName(metaXml->getStringAttribute("name"));
    if (metaName.isEmpty())
        metaName = sanitizePresetDisplayName(sourceFile.getFileNameWithoutExtension());
    if (metaName.isEmpty())
        metaName = "User_Preset";

    parsedTree.removeProperty(propPresetApplying, nullptr);
    writePresetMetadata(parsedTree,
                        PresetKind::User,
                        -1,
                        metaName,
                        sourceFile.getFullPathName(),
                        false);

    apvts.state.setProperty(propPresetApplying, true, nullptr);
    apvts.replaceState(parsedTree);
    writePresetMetadata(apvts.state,
                        PresetKind::User,
                        -1,
                        metaName,
                        sourceFile.getFullPathName(),
                        false);
    bumpPresetRevision(apvts.state);
    apvts.state.setProperty(propPresetApplying, false, nullptr);

    outPresetName = metaName;
    outError.clear();
    return true;
}

inline std::vector<UserPresetEntry> scanUserPresetsDirectory(const juce::File& dir = getDefaultUserPresetsDirectory())
{
    std::vector<UserPresetEntry> entries;
    if (!dir.isDirectory())
        return entries;

    const auto files = dir.findChildFiles(juce::File::findFiles, false, kPresetFileWildcard);
    for (const auto& file : files)
    {
        juce::String displayName = file.getFileNameWithoutExtension();
        if (const auto xml = juce::XmlDocument::parse(file))
        {
            if (xml->hasTagName(kPresetXmlRootTag))
            {
                if (const auto* meta = xml->getChildByName("Meta"))
                {
                    const auto parsedName = sanitizePresetDisplayName(meta->getStringAttribute("name"));
                    if (parsedName.isNotEmpty())
                        displayName = parsedName;
                }
            }
        }
        entries.push_back({ displayName, file });
    }

    std::sort(entries.begin(), entries.end(), [](const UserPresetEntry& a, const UserPresetEntry& b) {
        return a.name.compareIgnoreCase(b.name) < 0;
    });
    return entries;
}

} // namespace AllyPresets
