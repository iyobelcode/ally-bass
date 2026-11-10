# ALLY BASS – Windows Build & Installer Guide

> **JUICED UNICORN / IZHIA ARTS / 42GRAM**  
> Almere, Nederland · 1300 / 036 · -4.5m NAP

---

## Automated Build via GitHub Actions (Aanbevolen)

### Stap 1 – Push naar GitHub

```bash
cd /Users/izhia/Desktop/ALLY-Bassline
git remote add origin https://github.com/<jouw-username>/ally-bass.git
git push -u origin main
```

### Stap 2 – GitHub Actions draait automatisch

Na elke push naar `main` start de workflow **"Build ALLY BASS – Windows VST3 Installer"** die:

1. Controleert de code uit op een Windows Server 2022 runner
2. Bouwt met CMake + Visual Studio 2022 (MSVC) in Release mode
3. Pakt de `.vst3`-bundle in met **Inno Setup 6**
4. Upload `AllyBass_Setup.exe` als downloadbare artifact

### Stap 3 – Download de installer

- Ga naar **GitHub → jouw repo → Actions → meest recente run → Artifacts**
- Download **ALLY-BASS-Windows-Installer**
- Pak uit en voer `AllyBass_Setup.exe` uit op Windows

### Stap 4 – Installatie op Windows

- Voer `AllyBass_Setup.exe` uit als Administrator
- De VST3 wordt geïnstalleerd naar:
  ```
  C:\Program Files\Common Files\VST3\ALLY BASS.vst3\
  ```
- Open je DAW (FL Studio, Ableton Live, Reaper, etc.) en voer een **plugin rescan** uit
- ALLY BASS verschijnt in je instrument browser

---

## Handmatige Build op een Windows Machine

Als je liever lokaal bouwt op een Windows-pc:

### Vereisten

| Tool | Versie | Download |
|------|--------|----------|
| Git | Laatste | https://git-scm.com |
| CMake | ≥ 3.22 | https://cmake.org |
| Visual Studio 2022 | Community of hoger | https://visualstudio.microsoft.com |
| (optioneel) Inno Setup 6 | voor installer | https://jrsoftware.org/isinfo.php |

Zorg bij Visual Studio voor de workload: **"Desktop development with C++"**

### Bouwen

```bat
git clone https://github.com/<jouw-username>/ally-bass.git
cd ally-bass
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

De VST3 bundle staat dan op:
```
build\AllyBass_artefacts\Release\VST3\ALLY BASS.vst3
```

Kopieer die map handmatig naar:
```
C:\Program Files\Common Files\VST3\
```

### Installer bouwen (optioneel)

1. Installeer [Inno Setup 6](https://jrsoftware.org/isinfo.php)
2. Kopieer de VST3 bundle naar `installer\staging\VST3\ALLY BASS.vst3\`
3. Open `installer\AllyBass-Windows.iss` in Inno Setup
4. Druk op **Build → Compile** (of `Ctrl+F9`)
5. De installer staat in `installer\Output\AllyBass_Setup.exe`

---

## Release Taggen (automatische GitHub Release)

```bash
git tag v1.0.0
git push origin v1.0.0
```

GitHub Actions maakt dan automatisch een **GitHub Release** aan met `AllyBass_Setup.exe` als bijlage.

---

## Compatibiliteit

| DAW | VST3 Support | Opmerkingen |
|-----|-------------|-------------|
| FL Studio 21+ | ✅ | VST3 standaard ingeschakeld |
| Ableton Live 11+ | ✅ | Settings → Plugins → rescan |
| Reaper | ✅ | Auto-detected |
| Cubase/Nuendo | ✅ | Steinberg's eigen formaat |
| Studio One | ✅ | |
| Bitwig Studio | ✅ | |
| Logic Pro | ❌ | macOS/AU only |

---

## Mapstructuur

```
ALLY-Bassline/
├── .github/
│   └── workflows/
│       └── build-windows.yml   ← GitHub Actions CI/CD
├── installer/
│   ├── AllyBass-Windows.iss    ← Inno Setup script
│   └── staging/                ← wordt automatisch gevuld door CI
│       └── VST3/
│           └── ALLY BASS.vst3/
├── Source/                     ← C++ broncode
├── JUCE/                       ← JUCE 8.0.4
├── CMakeLists.txt
└── WINDOWS-BUILD.md            ← dit bestand
```
