# ALLY BASS — VST3 / AU / Standalone Bassline Synthesizer

> *"Minder knoppen, meer identiteit."*  
> Gebouwd op de zeebodem (-4,5 meter onder NAP), geïnspireerd door polderklei, brutalistische architectuur, het Weerwater en de rauwe hiphop/drill cultuur van Almere (1300 / 036).

---

## 🏛️ Project Identiteit & Cultuur
* **Herkomst**: Almere, Nederland (Postcode 1300 / Netnummer 036).
* **Concept**: Sub-bassen die letterlijk dieper gaan dan zeeniveau met een gefocust resonantiepunt op `-4.5m NAP`.
* **Doelgroep**: Producers van modern trap, drill, boombap en electronic club music die zonder urenlang tweaken direct een keiharde, herkenbare 808-sound neerleggen.
* **Credits**: `JUICED UNICORN / IZHIA ARTS / 42GRAM`

---

## 🎛️ DSP & Geluidsarchitectuur

### 1. NAP Sub-Oscillator (Onder Zeeniveau Engine)
- **Hybride Sinus/Driehoek**: Schakelt naadloos tussen een pure, diepe sub-sinus en een verzadigde parabolische driehoeksgolf voor rijke sub-harmonischen tussen 25 Hz en 80 Hz.
- **NAP-Lock Algoritme**: Garandeert 100% fase-coherentie en zero-crossing initialisatie bij elke nootaanslag. Voorkomt fase-uitdoving in clubsystemen, ongeacht pitch-bends of glides.
- **-4.5m NAP Resonantie**: De `DIEPTE` knop injecteert een fase-vergrendelde sub-octaaf component gecentreerd rond ~45 Hz.

### 2. Polder Transient Module (De Attack/Click)
- 808's moeten moeiteloos doorsnijden op zowel telefoonspeakers als festival PA-systemen.
- **Synthetische Pitch Drop Chirp**: Daalt in ~12 ms van 1400 Hz naar het fundament voor de fysieke 'knock'.
- **Polder Clay Snap**: Een band-passed ruisburst (~2.4 kHz) met een ultrasnelle decay van 5 ms voor instant klikdefinitie.
- Gemasterd en geschaald via de `POLDER PUNCH` knop.

### 3. Stadsdeel Saturatie (4 Karakteristieke DSP-Modi)

#### A. Haven (1350) — Warme Tape / Buizen Saturatie
Asymmetrische overdrachtsfunctie met even harmonischen en dynamische DC-filtering:
$$\begin{aligned}
x_{\text{biased}} &= x + 0.18 x^2 - 0.05 x^3 \\
y &= \tanh((1.0 + 3.5 \cdot \text{drive}) \cdot x_{\text{biased}}) \\
y_{\text{clean}}[n] &= y[n] - y[n-1] + 0.995 \cdot y_{\text{clean}}[n-1]
\end{aligned}$$

#### B. Centrum (1315) — Hard-Clipped Waveshaper (Trap & Drill)
Agressieve cubische soft/hard waveshaper met dynamische gain push:
$$\begin{aligned}
u &= x \cdot (1.0 + 7.5 \cdot \text{drive}) \\
y &= \begin{cases}
\operatorname{sgn}(u) & \text{als } |u| \ge 1.0 \\
1.5 u - 0.5 u^3 & \text{als } |u| < 1.0
\end{cases}
\end{aligned}$$

#### C. Poort (1363) — Clean Sub Exciter
Behoudt de sub-fundamenteel 100% kristalhelder en voegt een subtiele 2e Chebyshev harmonische excitatie toe:
$$y = x \cdot (1.0 + 0.4 \cdot \text{drive}) + 0.15 \cdot \text{drive} \cdot (2 x^2 - 1)$$

#### D. Buiten (1330) — Foldback Distortie met Mid-Resonantie
Resonant piekfilter op 550 Hz ($Q=2.0$) gevolgd door een wavefolder:
$$y = \sin\left(x_{\text{boosted}} \cdot (1.0 + 4.5 \cdot \text{drive}) \cdot \frac{\pi}{2}\right)$$

### 4. Waterwijk Dynamic Portamento / Glide Engine
- Monofone Last-Note Priority legato engine.
- **Velocity-gevoelige slide**: Hoe harder de noot wordt aangeslagen, hoe sneller de glidecurve:
  $$t_{\text{eff}} = t_{\text{glide}} \cdot (1.3 - 0.7 \cdot \text{velocity})$$
- Continue vloeiende pitch glides zonder fase-kliks.

### 5. Master Switches
* **036 OVERDRIVE**: Parallelle compressie (snelle 5ms attack, 80ms release, ratio 6:1) + 6dB extra saturation push voor onmiddellijk club-volume.
* **1300 MONO LOCK**: 2e orde Linkwitz-Riley crossover op 90 Hz die alle subfrequenties dwingt naar hard mono en fase-annulering in clubs elimineert.

---

## 🎚️ De Interface (6 Knoppen & 2 Toggles)

1. **DIEPTE** (`diepte`): Regelt het sub-laag en het sub-resonantiepunt (-4.5m NAP focus).
2. **POLDER PUNCH** (`polderPunch`): Bepaalt de transient knock en snap in de mix.
3. **VERVAL** (`verval`): Vormt de staart van de 808 (van 0.05s boombap punch tot 5.0s doordreunende drill sustain).
4. **HAVEN DIRT** (`havenDirt`): Stuurt de geselecteerde stadsdeel-saturatie aan.
5. **WATERWIJK GLIDE** (`waterwijkGlide`): Bepaalt de glijtijd en legato slide respons (0 tot 500 ms).
   *(Testfase-notitie: directe hernoeming vanaf oude ID `weerwaterGlide` zonder legacy-migratie; oude test-DAW-projecten/automations met `weerwaterGlide` verliezen de oude glide-binding en vallen terug op 40.0 ms.)*
6. **STADSDEEL SELECTOR**: 4-standen selector (*Haven 1350*, *Centrum 1315*, *Poort 1363*, *Buiten 1330*).
* **036 OVERDRIVE**: Parallel NYC compression + 6dB saturation boost (Toggle).
* **1300 MONO LOCK**: Sub < 90Hz hard mono crossover (Toggle).

---

## 🌊 De NAP Wave Visualizer
* **Centrale Weerwater Vector Scoop**: Realtime vectorgolf die de rimpelingen van het Weerwater nabootst met een 60 FPS update-rate.
* **-4.5m NAP Datumlijn**: Een subtiele technische stippellijn markeert de zeebodem.
* **Sub-Dive Glow**: Zodra de sub-bass activeert, duikt de golf fysiek onder deze waterlijn met een neon-cyaan (`#00E5FF`) nagloeiing.
* **Overdrive Aura**: Bij actieve 036 Overdrive licht een neon-oranje/rode (`#FF3B30`) harmonische gloed op.

---

## 💾 Almere Factory Presets
1. `01_Kasteel_Drill_Slide`: Lange decay (2.5s), Centrum clipping, snelle Waterwijk glide (85ms), 036 Overdrive & Mono Lock actief.
2. `02_Stedenwijk_Boom`: Korte decay (0.45s), Haven dirt, zware Polder Punch (90%), instant boombap thump.
3. `03_Pampus_Deep_Sub`: Zachte punch, Poort exciter, diepe -4.5m NAP focus (90%), pure en diepe sub-druk.

---

## 📦 Installatie & Builds
De plugin is gecompileerd voor macOS (Apple Silicon arm64 / universal):

* **VST3**: `~/Library/Audio/Plug-Ins/VST3/ALLY BASS.vst3`
* **AU (Audio Unit)**: `~/Library/Audio/Plug-Ins/Components/ALLY BASS.component`
* **Standalone App**: `build/AllyBass_artefacts/Release/Standalone/ALLY BASS.app`

### Handmatig bouwen met CMake:
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 8
```

---
**Credits**: JUICED UNICORN / IZHIA ARTS / 42GRAM  
*52.3702° N, 5.2144° E — Almere, The Netherlands*
