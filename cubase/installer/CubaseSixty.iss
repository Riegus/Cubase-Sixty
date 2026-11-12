; Cubase-Sixty installer (Inno Setup 7 or 6). Built by build-installer.ps1, which passes the version and the folders:
;   ISCC /DAppVersion=0.1.0 /DBuildDir=..\bridge\build\Release /DOutDir=..\dist CubaseSixty.iss
;
; Per user, no administrator rights: the program goes into %LOCALAPPDATA%\Programs\Cubase-Sixty (its settings, favourites
; and logs live beside it, as in the repo), the MIDI Remote scripts into the user's Documents (also when Documents is
; redirected, e.g. to OneDrive), Start with Windows into the user's Run key (the same entry the tray app's menu sets).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\bridge\build\Release"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif
#define ScriptsDir "{userdocs}\Steinberg\Cubase\MIDI Remote\Driver Scripts\Local\CubaseSixty"

[Setup]
AppId={{6F0C1B7E-5A0B-4D0E-9C55-3B1B5C6D60A1}
AppName=Cubase-Sixty
AppVersion={#AppVersion}
AppPublisher=Cubase-Sixty
AppPublisherURL=https://github.com/Riegus/Cubase-Sixty
DefaultDirName={localappdata}\Programs\Cubase-Sixty
DefaultGroupName=Cubase-Sixty
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutDir}
OutputBaseFilename=CubaseSixty-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Cubase-Sixty
UninstallDisplayIcon={app}\CubaseSixty.exe
; Cubase-Sixty's own licence is shown; Rea-Sixty's (whose code the bridge uses) is installed beside it, as MIT asks.
LicenseFile=..\LICENSE

[Tasks]
Name: "autostart"; Description: "Start Cubase-Sixty with Windows"; GroupDescription: "Start:"
; Ticked by the code below when a connected controller is not on the driver yet, or none is connected.
Name: "usbdriver"; Description: "Install the USB driver for the SSL controllers (asks for administrator permission; SSL 360 will no longer see them)"; GroupDescription: "USB driver (have the controllers plugged in and switched on):"; Flags: unchecked

[Files]
Source: "{#BuildDir}\CubaseSixty.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\cubase_sixty_bridge.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\libusb-1.0.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE-Rea-Sixty.txt"; Flags: ignoreversion
Source: "..\docs\user-guide.md"; DestDir: "{app}"; Flags: ignoreversion
; The WinUSB driver binding for the controllers (Rea-Sixty's scripts): run by the task below and by the tray app's menu.
Source: "driver\install-driver.ps1"; DestDir: "{app}\driver"; Flags: ignoreversion
Source: "driver\uninstall-driver.ps1"; DestDir: "{app}\driver"; Flags: ignoreversion
Source: "driver\rea_sixty_winusb.inf"; DestDir: "{app}\driver"; Flags: ignoreversion
; The MIDI Remote scripts, where Cubase looks for them.
Source: "..\midi-remote\UF8\CubaseSixty_UF8.js"; DestDir: "{#ScriptsDir}\UF8"; Flags: ignoreversion
Source: "..\midi-remote\UF1\CubaseSixty_UF1.js"; DestDir: "{#ScriptsDir}\UF1"; Flags: ignoreversion
Source: "..\midi-remote\UC1\CubaseSixty_UC1.js"; DestDir: "{#ScriptsDir}\UC1"; Flags: ignoreversion
Source: "..\midi-remote\Helper\CubaseSixty_Helper.js"; DestDir: "{#ScriptsDir}\Helper"; Flags: ignoreversion

[Icons]
Name: "{group}\Cubase-Sixty"; Filename: "{app}\CubaseSixty.exe"
Name: "{group}\Uninstall Cubase-Sixty"; Filename: "{uninstallexe}"

[Registry]
; The same value the tray app's "Start with Windows" writes (it shows as ticked there).
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Cubase-Sixty"; \
  ValueData: """{app}\CubaseSixty.exe"""; Tasks: autostart; Flags: uninsdeletevalue

[Run]
; Elevated (one administrator prompt). The tray app is started after it, so the bridge finds the controllers on the driver.
Filename: "powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -File ""{app}\driver\install-driver.ps1"""; \
  Verb: runas; Flags: shellexec waituntilterminated runhidden; StatusMsg: "Installing the USB driver for the controllers..."; Tasks: usbdriver
Filename: "{app}\CubaseSixty.exe"; Description: "Start Cubase-Sixty now"; Flags: nowait postinstall skipifsilent

[UninstallRun]
; Stops the tray app and the bridge (which blanks the surfaces) before the files go.
Filename: "{app}\CubaseSixty.exe"; Parameters: "--quit"; Flags: runhidden; RunOnceId: "QuitTray"

[UninstallDelete]
; What the program wrote beside itself. (settings.txt and favourites.txt are the user's: kept unless they say so, see below.)
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\level-curve.txt"

[Code]
// The virtual MIDI driver (teVirtualMIDI) makes the bridge's ports. It comes with loopMIDI, and with SSL 360.
function VirtualMidiInstalled(): Boolean;
begin
  Result := FileExists(ExpandConstant('{sys}\teVirtualMIDI64.dll')) or FileExists(ExpandConstant('{sys}\teVirtualMIDI.dll'))
    or FileExists(ExpandConstant('{win}\System32\teVirtualMIDI64.dll'));
end;

function VcRuntimeInstalled(): Boolean;
var installed: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', installed) and (installed = 1);
end;

// The USB driver task is ticked when it looks needed: a controller Windows knows that is not on WinUSB, or no
// controller known at all (a first install). All known ones on WinUSB already: left unticked.
function ControllerDriver(pid: String; var known: Boolean): Boolean;   // True = on WinUSB
var names: TArrayOfString; i: Integer; service, key: String;
begin
  Result := False;
  key := 'SYSTEM\CurrentControlSet\Enum\USB\VID_31E9&PID_' + pid;
  if not RegGetSubkeyNames(HKLM64, key, names) then exit;
  for i := 0 to GetArrayLength(names) - 1 do begin
    known := True;
    if RegQueryStringValue(HKLM64, key + '\' + names[i], 'Service', service) and (CompareText(service, 'WinUSB') = 0) then Result := True;
  end;
end;

function UsbDriverNeeded(): Boolean;
var known, k: Boolean; pids: array[0..2] of String; i: Integer;
begin
  pids[0] := '0021'; pids[1] := '0023'; pids[2] := '0025';
  known := False;
  Result := False;
  for i := 0 to 2 do begin
    k := False;
    if (not ControllerDriver(pids[i], k)) and k then Result := True;
    if k then known := True;
  end;
  if not known then Result := True;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpSelectTasks) and UsbDriverNeeded() then WizardSelectTasks('usbdriver');
end;

// A running copy is asked to quit (it stops the bridge, which blanks the surfaces) before files are replaced.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var code: Integer; exe: String;
begin
  Result := '';
  exe := ExpandConstant('{app}\CubaseSixty.exe');
  if FileExists(exe) then Exec(exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, code);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var text: String;
begin
  if CurStep <> ssPostInstall then exit;
  text := '';
  if not VirtualMidiInstalled() then
    text := text + 'The virtual MIDI driver was not found. Cubase-Sixty needs it for its MIDI ports. Install loopMIDI (free, by Tobias Erichsen), which brings the driver:' + #13#10 +
      'https://www.tobias-erichsen.de/software/loopmidi.html' + #13#10 + #13#10;
  if not VcRuntimeInstalled() then
    text := text + 'The Microsoft Visual C++ runtime was not found. If Cubase-Sixty does not start, install it:' + #13#10 +
      'https://aka.ms/vs/17/release/vc_redist.x64.exe' + #13#10 + #13#10;
  if text <> '' then MsgBox(text + 'See the user guide for the remaining setup steps.', mbInformation, MB_OK);
end;

// Uninstall: the user's settings and favourites are kept unless they say otherwise; the scripts are removed.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep <> usPostUninstall then exit;
  if FileExists(ExpandConstant('{app}\settings.txt')) or FileExists(ExpandConstant('{app}\favourites.txt')) then
    if MsgBox('Remove your Cubase-Sixty settings and favourites too?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then begin
      DeleteFile(ExpandConstant('{app}\settings.txt'));
      DeleteFile(ExpandConstant('{app}\favourites.txt'));
      RemoveDir(ExpandConstant('{app}'));
    end;
end;
