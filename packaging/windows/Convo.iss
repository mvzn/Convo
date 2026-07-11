; Inno Setup script for Convo (Windows VST3 installer).
; Build:  ISCC.exe //DAppVersion=1.0.0 packaging\windows\Convo.iss   (run from the repo root)
; Output: dist\Convo-<version>-Windows.exe
;
; Defaults to the machine-wide VST3 folder (C:\Program Files\Common Files\VST3), which
; every Windows host scans. The folder page lets the user pick a different VST3 folder,
; and the privileges dialog offers a no-admin "just me" install (which retargets the
; default to the per-user VST3 folder, %LOCALAPPDATA%\Programs\Common\VST3).

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
; {autocf64} = Common Files (admin install) or %LOCALAPPDATA%\Programs\Common (per-user)
DefaultDirName={autocf64}\VST3
AppendDefaultDirName=no
DisableDirPage=no
DirExistsWarning=no
DisableProgramGroupPage=yes
UninstallDisplayName=Convo {#AppVersion}
LicenseFile=..\..\LICENSE
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\..\dist
OutputBaseFilename=Convo-{#AppVersion}-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Files]
; Recurse the whole .vst3 bundle into <chosen VST3 folder>\Convo.vst3
Source: "{#Vst3Path}\*"; DestDir: "{app}\Convo.vst3"; \
    Flags: recursesubdirs createallsubdirs ignoreversion
; ship the AGPLv3 text alongside the binary (inside the bundle folder)
Source: "..\..\LICENSE"; DestDir: "{app}\Convo.vst3"; DestName: "LICENSE.txt"; \
    Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{app}\Convo.vst3"

[Messages]
WelcomeLabel2=This will install the Convo VST3 plugin (version {#AppVersion}).%n%nThe next pages let you review the license and choose the VST3 folder (the system default is right for almost everyone).%n%nClose your DAW before continuing.
SelectDirDesc=Setup will install the Convo.vst3 bundle into this VST3 folder.
SelectDirLabel3=Only change this if your host scans a custom VST3 location.
