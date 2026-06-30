; Inno Setup script for Convo (Windows VST3 installer).
; Build:  ISCC.exe //DAppVersion=1.0.0 packaging\windows\Convo.iss   (run from the repo root)
; Output: dist\Convo-<version>-Windows.exe
;
; Installs the Convo.vst3 bundle into the machine-wide VST3 folder
; (C:\Program Files\Common Files\VST3), which every Windows host scans.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

; The built bundle, resolved relative to THIS script (packaging\windows\ -> repo root\build\...).
#define Vst3Path "..\..\build\Convo_artefacts\Release\VST3\Convo.vst3"

[Setup]
AppId={{8B5F1C9A-1D2E-4C3B-9A77-436F6E766F31}
AppName=Convo
AppVersion={#AppVersion}
AppPublisher=mvzn
AppPublisherURL=https://github.com/mvzn/Convo
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
UninstallDisplayName=Convo {#AppVersion}
LicenseFile=..\..\LICENSE
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\..\dist
OutputBaseFilename=Convo-{#AppVersion}-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
; Recurse the whole .vst3 bundle into Common Files\VST3\Convo.vst3
Source: "{#Vst3Path}\*"; DestDir: "{commoncf64}\VST3\Convo.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Convo.vst3"

[Messages]
WelcomeLabel2=This will install the Convo VST3 plugin (version {#AppVersion}) into your system VST3 folder.%n%nClose your DAW before continuing.
