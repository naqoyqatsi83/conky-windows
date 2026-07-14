; Inno Setup script for Conky Windows Port

#define MyAppName "Conky"
#define MyAppVersion "1.24.3-wp.1"
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
Source: "..\conky_examples\Windows-btop.conkyrc"; DestDir: "{userdocs}\Conky"; Flags: ignoreversion; DestName: "btop.conkyrc"; AfterInstall: CreateConfig
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

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('install_lhm') then
    SetupTempHelper();
end;

procedure CreateConfig;
var
  ConfigPath: string;
begin
  ConfigPath := ExpandConstant('{userdocs}\Conky\btop.conkyrc');
  if not FileExists(ConfigPath) then
  begin
    SaveStringToFile(ConfigPath,
      'conky.config = {' + #13#10 +
      '  alignment = ''top_right'',' + #13#10 +
      '  own_window = true,' + #13#10 +
      '  gap_x = 20, gap_y = 60,' + #13#10 +
      '  font = ''Consolas:size=10'',' + #13#10 +
      '  default_color = ''white'',' + #13#10 +
      '  update_interval = 1.0,' + #13#10 +
      '  minimum_width = 340,' + #13#10 +
      '  draw_shades = false,' + #13#10 +
      '}' + #13#10 + #13#10 +
      'conky.text = [[' + #13#10 +
      '${font Segoe UI:bold:size=12}${alignc}System Monitor' + #13#10 +
      '${hr 2}' + #13#10 +
      '${font Segoe UI:bold:size=10}${exec powershell -Command "$env:COMPUTERNAME"}' + #13#10 +
      ']];' + #13#10, False);
  end;
end;
