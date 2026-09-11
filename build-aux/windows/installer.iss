; Kaltura Live Control - Windows installer (Inno Setup 6)
;
; Installs into %PROGRAMDATA%\obs-studio\plugins\, which is where OBS 31 actually
; looks for third-party plugins on Windows. Note this is NOT %APPDATA% - a plugin
; placed there is silently ignored, with no error in the OBS log.
;
; Build with:
;   ISCC.exe /DSourceDir=<path-to-RelWithDebInfo> build-aux\windows\installer.iss

#define MyAppName "Kaltura Live Control"
#define MyAppVersion "0.1.0"
#define MyPublisher "Kaltura"
#define MyAppURL "https://corp.kaltura.com"
#define PluginId "kaltura-live-control"

#ifndef SourceDir
  #define SourceDir "..\..\build_x64\RelWithDebInfo"
#endif
#ifndef DataDir
  #define DataDir "..\..\data"
#endif

[Setup]
AppId={{7C3B9E14-5E2A-4C7D-9F41-2A6D8B0E5C33}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyPublisher}
AppPublisherURL={#MyAppURL}
VersionInfoVersion={#MyAppVersion}

; OBS only scans this exact location, so the user does not get to choose.
DefaultDirName={commonappdata}\obs-studio\plugins\{#PluginId}
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no

; %PROGRAMDATA% needs elevation to write.
PrivilegesRequired=admin

OutputDir=..\..\dist
OutputBaseFilename=kaltura-live-control-{#MyAppVersion}-windows-x64-installer
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceDir}\{#PluginId}.dll"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion
; The .pdb is optional - it makes any crash report actually readable.
Source: "{#SourceDir}\{#PluginId}.pdb"; DestDir: "{app}\bin\64bit"; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#DataDir}\locale\*"; DestDir: "{app}\data\locale"; Flags: ignoreversion recursesubdirs

[Messages]
WelcomeLabel2=This will install [name/ver] into OBS Studio.%n%nThe plugin adds a "Kaltura Live Control" dock for finding live entries, wiring their ingest endpoints straight into OBS, and running multi-audio language streams.%n%nPlease close OBS Studio before continuing.

[Code]
{ Replacing a DLL that OBS currently has loaded fails, and the failure is confusing
  ("file in use" on a path the user never chose). Check up front instead. }
function ObsIsRunning(): Boolean;
var
  ResultCode: Integer;
begin
  Result := False;
  if Exec(ExpandConstant('{cmd}'),
          '/C tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe"',
          '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    Result := (ResultCode = 0);
end;

function ObsIsInstalled(): Boolean;
begin
  Result := FileExists(ExpandConstant('{commonpf64}\obs-studio\bin\64bit\obs64.exe'))
         or DirExists(ExpandConstant('{commonappdata}\obs-studio'));
end;

function InitializeSetup(): Boolean;
begin
  Result := True;

  if ObsIsRunning() then
  begin
    if MsgBox('OBS Studio is currently running.' + #13#10#13#10 +
              'The plugin file cannot be replaced while OBS has it loaded. ' +
              'Please close OBS Studio, then click Retry.' + #13#10#13#10 +
              'Continue anyway?', mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;

  if Result and (not ObsIsInstalled()) then
  begin
    if MsgBox('OBS Studio was not detected on this machine.' + #13#10#13#10 +
              'The plugin will be installed to the standard OBS plugin folder ' +
              'and will start working once OBS is installed.' + #13#10#13#10 +
              'Continue?', mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
