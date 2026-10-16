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
; Native raster variants for Inno 6 font/DPI combinations, including 150%.
WizardImageFile=..\art\windows\installer\wizard-164.bmp,..\art\windows\installer\wizard-202.bmp,..\art\windows\installer\wizard-240.bmp,..\art\windows\installer\wizard-269.bmp,..\art\windows\installer\wizard-290.bmp,..\art\windows\installer\wizard-315.bmp,..\art\windows\installer\wizard-336.bmp,..\art\windows\installer\wizard-366.bmp,..\art\windows\installer\wizard-403.bmp,..\art\windows\installer\wizard-416.bmp,..\art\windows\installer\wizard-430.bmp,..\art\windows\installer\wizard-498.bmp,..\art\windows\installer\wizard-534.bmp
WizardSmallImageFile=..\art\windows\installer\small-58.bmp,..\art\windows\installer\small-71.bmp,..\art\windows\installer\small-77.bmp,..\art\windows\installer\small-85.bmp,..\art\windows\installer\small-97.bmp,..\art\windows\installer\small-103.bmp,..\art\windows\installer\small-112.bmp,..\art\windows\installer\small-116.bmp,..\art\windows\installer\small-124.bmp,..\art\windows\installer\small-129.bmp,..\art\windows\installer\small-143.bmp,..\art\windows\installer\small-147.bmp,..\art\windows\installer\small-159.bmp
ChangesAssociations=yes
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
; LIVE CAPTIONS run on a speech model made on this computer, downloaded here
; rather than carried in every download. Anything not ticked can still be
; fetched later: turning captions on offers it. "Any language" is one model
; that hears about 99 languages; .en models are English only and better at it.
Name: "captions"; Description: "Speech models for live captions (downloaded now; any can also be fetched later)"; GroupDescription: "Live captions:"
Name: "captions\en"; Description: "English (142 MB)"; GroupDescription: "Live captions:"
Name: "captions\any"; Description: "Any language, about 99 of them (142 MB)"; GroupDescription: "Live captions:"; Flags: unchecked
Name: "captions\enbest"; Description: "English, more accurate (466 MB)"; GroupDescription: "Live captions:"; Flags: unchecked
Name: "captions\anybest"; Description: "Any language, more accurate (466 MB)"; GroupDescription: "Live captions:"; Flags: unchecked

[InstallDelete]
; An update installs OVER the old folder, so a file a release dropped stays
; behind and is still used: the two round pictures taken out of the splash
; rotation on 2026-10-08 kept appearing on updated machines. The bundled
; splash art is cleared before the new copy goes in. Only bundled art: the
; data folder can also hold the operator's own shows and state.
Type: filesandordirs; Name: "{app}\data\ui\deckboy_ui_pack_v3\splash"

[UninstallDelete]
; The speech models the installer downloaded are not in [Files], so name them.
Type: filesandordirs; Name: "{app}\data\models"

[Files]
; The entire staged tree — Deckboy.exe, DLLs, ffmpeg/ffprobe, MSVC runtime,
; data\, LICENSE. recursesubdirs pulls in data\ and its subfolders.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion
Source: "..\art\windows\icons\deckboy_show.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Deckboy"; Filename: "{app}\Deckboy.exe"
Name: "{group}\Uninstall Deckboy"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Deckboy"; Filename: "{app}\Deckboy.exe"; Tasks: desktopicon

[Registry]
; .deckboy association, only when the operator opted in. HKA = HKLM for a
; per-machine install, HKCU for a per-user one, so it is correct either way.
Root: HKA; Subkey: "Software\Classes\.deckboy"; ValueType: string; ValueData: "Deckboy.Show"; Flags: uninsdeletevalue; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Deckboy.Show"; ValueType: string; ValueData: "Deckboy Show"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Deckboy.Show\DefaultIcon"; ValueType: string; ValueData: """{app}\deckboy_show.ico"",0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Deckboy.Show\shell\open\command"; ValueType: string; ValueData: """{app}\Deckboy.exe"" ""%1"""; Tasks: associate

[Run]
; INSTALL & RESTART MEANS RESTART.
; "postinstall" made this a CHECKBOX on the finish page, so the app only came
; back if the operator noticed it and left it ticked. Deckboy's updater quits
; the app and hands over to this installer, so from the desk it looked like
; INSTALL & RESTART simply never restarted.
;
; Without postinstall it runs automatically when the install finishes, which
; is what the button promised. skipifsilent stays: a silent install is
; somebody scripting a deployment, and they do not want a window.
;
; AS THE ORIGINAL USER. Only postinstall entries default to that; without the
; flag an all-users install relaunched Deckboy as Administrator, and Windows
; then refuses every file dragged in from Explorer (a lower-privilege window).
Filename: "{app}\Deckboy.exe"; Description: "Launch Deckboy now"; Flags: nowait skipifsilent runasoriginaluser

; CLEAN UP THE INSTALLER WE CAME FROM.
;
; An update leaves a ~94MB setup.exe behind, and the app cannot tidy it up
; afterwards: once the update completes you are running the NEWLY INSTALLED
; Deckboy, whose state directory is a different folder from the one the
; download landed in. So the old copy's data/updates keeps the installer
; forever, and every update adds another.
;
; Only when Deckboy's own updater started us. Someone who downloaded the
; installer deliberately and ran it themselves should still have it afterwards
; -- silently deleting a file out of somebody's Downloads folder is not ours to
; do. Deckboy passes /fromupdater=1; nothing else does.
;
; cmd rather than Inno: a running installer cannot delete itself, so a detached
; shell waits a few seconds for us to exit and then removes it.
Filename: "{cmd}"; Parameters: "/c ping -n 5 127.0.0.1 >nul & del /f /q ""{srcexe}"""; Flags: nowait runhidden; Check: LaunchedByUpdater

[Code]
// True only when Deckboy's updater launched this installer (/fromupdater=1).
function LaunchedByUpdater: Boolean;
begin
  Result := ExpandConstant('{param:fromupdater|0}') = '1';
end;

// ── LIVE CAPTIONS: the speech models ticked under Tasks ─────────────────────
// Downloaded on the Ready page's Next, before anything is installed, to {tmp};
// copied into {app}\data\models once the files are in. A failed download never
// fails the install: captions offer the model again when they are turned on.
// A silent install (the updater's) shows no pages and downloads nothing, and
// a model already in place is not fetched again.
var
  CaptionsPage: TDownloadWizardPage;
  CaptionModels: TArrayOfString;

procedure InitializeWizard;
begin
  CaptionsPage := CreateDownloadPage('Live captions',
    'Downloading the speech models you ticked. Cancel to skip: turning captions on fetches them later.', nil);
end;

procedure AddCaptionModel(const Task, Model: String);
var
  n: Integer;
begin
  if WizardIsTaskSelected(Task) and
     not FileExists(ExpandConstant('{app}\data\models\ggml-' + Model + '.bin')) then
  begin
    CaptionsPage.Add('https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-' + Model + '.bin',
                     'ggml-' + Model + '.bin', '');
    n := GetArrayLength(CaptionModels);
    SetArrayLength(CaptionModels, n + 1);
    CaptionModels[n] := Model;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpReady then
  begin
    SetArrayLength(CaptionModels, 0);
    CaptionsPage.Clear;
    AddCaptionModel('captions\en', 'base.en');
    AddCaptionModel('captions\any', 'base');
    AddCaptionModel('captions\enbest', 'small.en');
    AddCaptionModel('captions\anybest', 'small');
    if GetArrayLength(CaptionModels) > 0 then
    begin
      CaptionsPage.Show;
      try
        try
          CaptionsPage.Download;
        except
          SuppressibleMsgBox('The speech models were not downloaded (' + GetExceptionMessage +
            '). Deckboy installs anyway; turning live captions on fetches them later.',
            mbInformation, MB_OK, IDOK);
          SetArrayLength(CaptionModels, 0);
        end;
      finally
        CaptionsPage.Hide;
      end;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  i: Integer;
  dir: String;
begin
  if (CurStep = ssPostInstall) and (GetArrayLength(CaptionModels) > 0) then
  begin
    dir := ExpandConstant('{app}\data\models');
    ForceDirectories(dir);
    for i := 0 to GetArrayLength(CaptionModels) - 1 do
      FileCopy(ExpandConstant('{tmp}\ggml-' + CaptionModels[i] + '.bin'),
               dir + '\ggml-' + CaptionModels[i] + '.bin', False);
  end;
end;
