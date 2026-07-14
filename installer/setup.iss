; Inno Setup script for Conky Windows Port

#define MyAppName "Conky"
#define MyAppVersion "1.24.3-wp.3"
#define MyAppPublisher "Conky project"
#define MyAppURL "https://github.com/brndnhrbrt/conky"
#define MyAppExeName "conky.exe"

#ifndef SOURCE_DIR
  #define SOURCE_DIR "..\build\src"
#endif

[Setup]
AppId={{A2E3F4A5-B6C7-8901-D234-E5F6A7B8C9D0}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf64}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile={#SOURCE_DIR}\..\..\LICENSE.md
OutputDir=.
OutputBaseFilename=Conky-{#MyAppVersion}-Setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
DisableProgramGroupPage=yes
CloseApplications=yes
UninstallDisplayIcon={app}\conky.ico
SetupIconFile=conky.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"; Flags: checkedonce
Name: "startup"; Description: "Start Conky &automatically at login"; GroupDescription: "Startup options:"; Flags: checkedonce
Name: "install_lhm"; Description: "Install LibreHardwareMonitor (enables CPU/GPU temperature, fan speeds, voltages — runs as tray app)"; GroupDescription: "Hardware monitoring:"; Flags: unchecked

[Dirs]
Name: "{commonappdata}\Conky"

[Files]
Source: "{#SOURCE_DIR}\conky.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SOURCE_DIR}\*.dll"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist
Source: "btop.conkyrc"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SOURCE_DIR}\..\..\installer\LibreHardwareMonitor\*"; DestDir: "{app}\LibreHardwareMonitor"; Flags: ignoreversion skipifsourcedoesntexist recursesubdirs createallsubdirs; Tasks: install_lhm
Source: "conky.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Conky"; Filename: "{app}\{#MyAppExeName}"; Parameters: "-c ""{userdocs}\Conky\btop.conkyrc"""; WorkingDir: "{app}"; IconFilename: "{app}\conky.ico"
Name: "{group}\Conky (edit config)"; Filename: "notepad.exe"; Parameters: """{userdocs}\Conky\btop.conkyrc"""; WorkingDir: "{app}"
Name: "{group}\LibreHardwareMonitor"; Filename: "{app}\LibreHardwareMonitor\LibreHardwareMonitor.exe"; WorkingDir: "{app}\LibreHardwareMonitor"; Tasks: install_lhm
Name: "{group}\Conky Temp Helper"; Filename: "{app}\LibreHardwareMonitor\lhm-temp.exe"; WorkingDir: "{app}\LibreHardwareMonitor"; Tasks: install_lhm; Comment: "Manual launch — normally runs automatically via scheduled task"
Name: "{group}\Uninstall Conky"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Conky"; Filename: "{app}\{#MyAppExeName}"; Parameters: "-c ""{userdocs}\Conky\btop.conkyrc"""; WorkingDir: "{app}"; Tasks: desktopicon; IconFilename: "{app}\conky.ico"
Name: "{userstartup}\Conky"; Filename: "{app}\{#MyAppExeName}"; Parameters: "-c ""{userdocs}\Conky\btop.conkyrc"""; WorkingDir: "{app}"; Tasks: startup; IconFilename: "{app}\conky.ico"
Name: "{userstartup}\LibreHardwareMonitor"; Filename: "{app}\LibreHardwareMonitor\LibreHardwareMonitor.exe"; Parameters: "--minimize"; WorkingDir: "{app}\LibreHardwareMonitor"; Tasks: install_lhm

[Run]
Filename: "{app}\{#MyAppExeName}"; Parameters: "-c ""{userdocs}\Conky\btop.conkyrc"" -d"; Description: "Launch Conky"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "taskkill"; Parameters: "/F /IM conky.exe"; Flags: runhidden skipifdoesntexist
Filename: "taskkill"; Parameters: "/F /IM lhm-temp.exe"; Flags: runhidden skipifdoesntexist
Filename: "taskkill"; Parameters: "/F /IM LibreHardwareMonitor.exe"; Flags: runhidden skipifdoesntexist
Filename: "schtasks.exe"; Parameters: "/DELETE /TN ""ConkyTempHelper"" /F"; Flags: runhidden skipifdoesntexist

[UninstallDelete]
Type: files; Name: "{commonappdata}\Conky\temp.dat"
Type: files; Name: "{commonappdata}\Conky\gpu.dat"
Type: dirifempty; Name: "{commonappdata}\Conky"
Type: filesandordirs; Name: "{app}\LibreHardwareMonitor"
Type: files; Name: "{app}\conky.exe"
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\conky.ico"
Type: dirifempty; Name: "{app}"
Type: dirifempty; Name: "{userdocs}\Conky"

[Code]

{ -- Kill all Conky-related processes before install/uninstall -- }
procedure KillAllProcesses();
var
  ResultCode: Integer;
begin
  Exec('taskkill', '/F /IM conky.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill', '/F /IM lhm-temp.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Exec('taskkill', '/F /IM LibreHardwareMonitor.exe', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function InitializeSetup: Boolean;
begin
  KillAllProcesses();
  Result := True;
end;

function InitializeUninstall: Boolean;
var
  R: Integer;
begin
  KillAllProcesses();
  Exec('schtasks.exe', '/DELETE /TN "ConkyTempHelper" /F', '', SW_HIDE,
       ewWaitUntilTerminated, R);
  Result := True;
end;

{ -- Launch lhm-temp.exe and create the scheduled task -- }
procedure SetupTempHelper;
var
  R: Integer;
  HelperPath: string;
  HelperDir: string;
begin
  HelperPath := ExpandConstant('{app}\LibreHardwareMonitor\lhm-temp.exe');
  HelperDir := ExpandConstant('{app}\LibreHardwareMonitor');
  if not FileExists(HelperPath) then
    exit;

  // Launch helper directly — installer is elevated, no 740 possible.
  ShellExec('open', HelperPath, '', HelperDir, SW_HIDE, ewNoWait, R);

  // Create ONLOGON scheduled task so helper auto-starts at next login.
  Exec('schtasks.exe',
       '/CREATE /SC ONLOGON /TN "ConkyTempHelper" /TR "' + HelperPath +
       '" /RL HIGHEST /F',
       '', SW_HIDE, ewWaitUntilTerminated, R);
end;

{ -- Try to detect the discrete GPU index from gpu.dat -- }
function DetectGpuIndex(): Integer;
var
  GpuPath: string;
  Lines: TArrayOfString;
  I, Idx, MaxIdx: Integer;
  Name: string;
begin
  Result := 0;
  MaxIdx := 0;
  GpuPath := ExpandConstant('{commonappdata}\Conky\gpu.dat');
  if not FileExists(GpuPath) then
    exit;
  if not LoadStringsFromFile(GpuPath, Lines) then
    exit;
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    Idx := 0;
    Name := '';
    if Lines[I] = '' then continue;
    Idx := StrToIntDef(Copy(Lines[I], 1, Pos('|', Lines[I]) - 1), -1);
    if Idx >= 0 then
    begin
      if Idx > MaxIdx then
        MaxIdx := Idx;
      Name := Lines[I];
      if (Pos('NVIDIA', Name) > 0) or (Pos('GeForce', Name) > 0) or
         (Pos('RTX', Name) > 0) or (Pos('GTX', Name) > 0) then
      begin
        Result := Idx;
      end;
    end;
  end;
  if (Result = 0) and (MaxIdx > 0) then
    Result := MaxIdx;
end;

{ -- Create sample config from template with GPU index detection -- }
procedure CreateConfig;
var
  ConfigPath: string;
  ConfigDir: string;
  GpuIdx: Integer;
  GpuIdxStr: string;
  TemplatePath: string;
  Lines: TArrayOfString;
  I: Integer;
begin
  ConfigPath := ExpandConstant('{userdocs}\Conky\btop.conkyrc');
  ConfigDir := ExpandConstant('{userdocs}\Conky');
  TemplatePath := ExpandConstant('{app}\btop.conkyrc');

  // Ensure the config directory exists
  if not DirExists(ConfigDir) then
    CreateDir(ConfigDir);

  // Detect GPU — wait a bit only if lhm-temp was actually installed
  if FileExists(ExpandConstant('{app}\LibreHardwareMonitor\lhm-temp.exe')) then
    Sleep(3000);
  GpuIdx := DetectGpuIndex();
  GpuIdxStr := IntToStr(GpuIdx);

  // Load template, patch GPU index, write config
  if LoadStringsFromFile(TemplatePath, Lines) then
  begin
    for I := 0 to GetArrayLength(Lines) - 1 do
    begin
      StringChangeEx(Lines[I], 'GPU_IDX', GpuIdxStr, True);
    end;
    SaveStringsToFile(ConfigPath, Lines, False);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Always launch lhm-temp (needed for CPU/GPU sensor data)
    SetupTempHelper();

    // Generate config with GPU detection (handles its own timing)
    CreateConfig();
  end;
end;
