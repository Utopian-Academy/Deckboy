; Deckboy Windows installer (Inno Setup 6).
;
; Builds a proper installer from the SAME staged tree the portable zip uses
; (tools\package_windows.ps1 -> dist\staging\Deckboy-<VERSION>-windows-x64\),
; so the installer and the zip always contain byte-identical payloads. All
; dependencies (Deckboy.exe, every DLL, ffmpeg/ffprobe, the MSVC runtime, data\)
; are already in that folder, so the installer bundles everything and needs no
; internet at install time.
;
; Build:
;   iscc /DDeckboyVersion=0.83.0 tools\deckboy.iss
; VERSION is passed in by the packaging script so it stays the single source of
; truth; a fallback keeps a bare `iscc tools\deckboy.iss` working for a quick
; local build.

#ifndef DeckboyVersion
  #define DeckboyVersion "0.0.0"
#endif

#define AppName "Deckboy"
#define AppPublisher "Utopian Academy"
#define AppURL "https://github.com/Utopian-Academy/Deckboy"
#define StageDir "..\dist\staging\Deckboy-" + DeckboyVersion + "-windows-x64"

[Setup]
AppId={{7E2C9F41-6B3D-4E1A-B755-DECB0A0AD001}
AppName={#AppName}
AppVersion={#DeckboyVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
; Per-machine install by default; a user without admin gets a per-user install
; instead of a hard failure.
PrivilegesRequiredOverridesAllowed=dialog commandline
OutputDir=..\dist
OutputBaseFilename=Deckboy-{#DeckboyVersion}-windows-x64-setup
SetupIconFile=..\art\windows\icons\deckboy_app.ico
UninstallDisplayIcon={app}\Deckboy.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; The payload is the portable zip's contents, which do not embed machine state
; (package_windows.ps1 strips last_project.txt / default.deckboy).
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"
Name: "associate"; Description: "Associate &.deckboy show files with Deckboy"; GroupDescription: "File associations:"

[Files]
; The entire staged tree — Deckboy.exe, DLLs, ffmpeg/ffprobe, MSVC runtime,
; data\, LICENSE. recursesubdirs pulls in data\ and its subfolders.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\Deckboy"; Filename: "{app}\Deckboy.exe"
Name: "{group}\Uninstall Deckboy"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Deckboy"; Filename: "{app}\Deckboy.exe"; Tasks: desktopicon

[Registry]
; .deckboy association, only when the operator opted in. HKA = HKLM for a
; per-machine install, HKCU for a per-user one, so it is correct either way.
Root: HKA; Subkey: "Software\Classes\.deckboy"; ValueType: string; ValueData: "Deckboy.Show"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Deckboy.Show"; ValueType: string; ValueData: "Deckboy Show"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Deckboy.Show\DefaultIcon"; ValueType: string; ValueData: "{app}\Deckboy.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Deckboy.Show\shell\open\command"; ValueType: string; ValueData: """{app}\Deckboy.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\Deckboy.exe"; Description: "Launch Deckboy now"; Flags: nowait postinstall skipifsilent
