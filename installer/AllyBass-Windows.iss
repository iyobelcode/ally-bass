; ═══════════════════════════════════════════════════════════════════════════
;  ALLY BASS – Windows VST3 Installer
;  Built with Inno Setup 6
;  Developer: JUICED UNICORN / IZHIA ARTS / 42GRAM
;  Theme: Almere, Nederland – 1300 / 036
; ═══════════════════════════════════════════════════════════════════════════

#define AppName    "ALLY BASS"
#define AppVersion "1.0.0"
#define AppPublisher "JUICED UNICORN / IZHIA ARTS / 42GRAM"
#define AppURL     "https://github.com/izhia-arts/ally-bass"
#define VST3Dir    "staging\ALLY BASS.vst3"

[Setup]
AppId={{8F3A1C2D-4B5E-6F7A-8B9C-0D1E2F3A4B5C}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} v{#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={commonpf64}\Common Files\VST3
DefaultGroupName={#AppName}
AllowNoIcons=yes
; Output
OutputDir=Output
OutputBaseFilename=AllyBass_Setup
; Compression
Compression=lzma2/ultra64
SolidCompression=yes
; Appearance
WizardStyle=modern
WizardImageFile=compiler:WizModernImage.bmp
WizardSmallImageFile=compiler:WizModernSmallImage.bmp
SetupIconFile=
; Min Windows 10
MinVersion=10.0
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
; Privileges
PrivilegesRequired=admin
; Uninstaller
Uninstallable=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\ALLY BASS.vst3\Contents\x86_64-win\ALLY BASS.vst3
; Misc
DisableDirPage=no
DisableProgramGroupPage=yes
; License page
; LicenseFile=..\LICENSE.txt

; ── Banner ────────────────────────────────────────────────────────────────
[Messages]
WelcomeLabel1=Welkom bij de {#AppName} installer
WelcomeLabel2=ALLY BASS is een grensverleggende VST3 Bassline Synthesizer gebouwd in Almere, Nederland.%n%nGebaseerd op de zeebodem (-4,5m NAP) · Netnummer 036 · Postcodegebied 1300%n%n JUICED UNICORN / IZHIA ARTS / 42GRAM

; ── Files ─────────────────────────────────────────────────────────────────
[Files]
; The VST3 bundle (a directory on Windows) → installs to Common Files\VST3\
Source: "..\{#VST3Dir}\*"; DestDir: "{commonpf64}\Common Files\VST3\ALLY BASS.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

; ── Registry ──────────────────────────────────────────────────────────────
[Registry]
; Nothing required – VST3 is discovered by path, not registry.

; ── Icons (Start Menu) ───────────────────────────────────────────────────
[Icons]
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

; ── Run after install ─────────────────────────────────────────────────────
[Run]
; Nothing – VST3 auto-scanned by DAW on next launch.

; ── Code (optional info dialog) ───────────────────────────────────────────
[Code]
procedure InitializeWizard;
begin
  WizardForm.Caption := '{#AppName} v{#AppVersion} – Installer';
end;
