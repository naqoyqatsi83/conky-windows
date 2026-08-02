; Inno Setup script for Conky Windows Port

#define MyAppName "Conky"
#define MyAppVersion "1.24.3-wp.4"
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
ArchitecturesInstallIn64BitMode=x64compatible
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
  try
    ShellExec('open', HelperPath, '', HelperDir, SW_HIDE, ewNoWait, R);
  except
  end;

  // Create ONLOGON scheduled task so helper auto-starts at next login.
  try
    Exec('schtasks.exe',
         '/CREATE /SC ONLOGON /TN "ConkyTempHelper" /TR "' + HelperPath +
         '" /RL HIGHEST /F',
         '', SW_HIDE, ewWaitUntilTerminated, R);
  except
  end;
end;

{ -- Try to detect the discrete GPU index from gpu.dat -- }
{ Reads gpu.dat (written by lhm-temp.exe) to find the NVIDIA GPU index.
  Uses LoadStringsFromFile wrapped in try-except: if the file has lines
  exceeding the 255-char limit (some Inno Setup configurations), we
  safely fall back to index 0.                                          }
function DetectGpuIndex(): Integer;
var
  GpuPath: string;
  Lines: TArrayOfString;
  I, Idx, MaxIdx: Integer;
begin
  Result := 0;
  MaxIdx := 0;
  GpuPath := ExpandConstant('{commonappdata}\Conky\gpu.dat');
  if not FileExists(GpuPath) then
    exit;

  try
    if not LoadStringsFromFile(GpuPath, Lines) then
      exit;
  except
    // LoadStringsFromFile can throw "Cannot Import file N" on systems
    // with ANSI Inno Setup where TArrayOfString elements are limited
    // to 255 chars. Default to GPU index 0 (usually correct).
    exit;
  end;

  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    if Lines[I] = '' then continue;
    Idx := StrToIntDef(Copy(Lines[I], 1, Pos('|', Lines[I]) - 1), -1);
    if Idx >= 0 then
    begin
      if Idx > MaxIdx then
        MaxIdx := Idx;
      if (Pos('NVIDIA', Lines[I]) > 0) or (Pos('GeForce', Lines[I]) > 0) or
         (Pos('RTX', Lines[I]) > 0) or (Pos('GTX', Lines[I]) > 0) then
        Result := Idx;
    end;
  end;
  if (Result = 0) and (MaxIdx > 0) then
    Result := MaxIdx;
end;

{ -- Create sample config with GPU index detection -- }
{ Generates config inline instead of using LoadStringsFromFile to
  avoid runtime errors on systems where TArrayOfString has 255-char
  per-element limits.                                               }
procedure CreateConfig;
var
  ConfigPath: string;
  ConfigDir: string;
  GpuIdx: Integer;
  GpuIdxStr: string;
  Lines: TArrayOfString;
  I: Integer;
begin
  ConfigPath := ExpandConstant('{userdocs}\Conky\btop.conkyrc');
  ConfigDir := ExpandConstant('{userdocs}\Conky');

  // Ensure the config directory exists
  if not DirExists(ConfigDir) then
    CreateDir(ConfigDir);

  // Detect GPU — wait a bit only if lhm-temp was actually installed
  if FileExists(ExpandConstant('{app}\LibreHardwareMonitor\lhm-temp.exe')) then
    Sleep(3000);
  GpuIdx := DetectGpuIndex();
  GpuIdxStr := IntToStr(GpuIdx);

  // Build config content inline (avoids LoadStringsFromFile entirely)
  // This is a copy of btop.conkyrc with GPU_IDX as the placeholder.
  try
    SetArrayLength(Lines, 67);
    Lines[0]  := 'conky.config = {';
    Lines[1]  := '  alignment = ''top_right'',';
    Lines[2]  := '  own_window = true,';
    Lines[3]  := '  gap_x = 20, gap_y = 60,';
    Lines[4]  := '  font = ''Consolas:size=10'',';
    Lines[5]  := '  default_color = ''white'',';
    Lines[6]  := '  update_interval = 1.0,';
    Lines[7]  := '  minimum_width = 340, minimum_height = 0,';
    Lines[8]  := '  maximum_width = 350,';
    Lines[9]  := '  draw_shades = false,';
    Lines[10] := '}';
    Lines[11] := '';
    Lines[12] := 'conky.text = [[';
    Lines[13] := '${font Segoe UI:bold:size=12}${alignc}System Monitor';
    Lines[14] := '${hr 2}';
    Lines[15] := '';
    Lines[16] := '${font Segoe UI:bold:size=10}${exec powershell -Command "$env:COMPUTERNAME"}';
    Lines[17] := '${font Consolas:size=10}';
    Lines[18] := 'Uptime:             ${alignr}$uptime';
    Lines[19] := '';
    Lines[20] := '${font Segoe UI:bold:size=10}CPU [${exec powershell -Command "((Get-CimInstance Win32_Processor).Name).Trim()"}]';
    Lines[21] := '${font Consolas:size=10}';
    Lines[22] := 'Usage:              ${alignr}${cpu}%';
    Lines[23] := 'Freq:               ${alignr}${exec powershell -Command "& { [math]::Round(((Get-CimInstance Win32_Processor).MaxClockSpeed[0] * (Get-CimInstance Win32_PerfFormattedData_Counters_ProcessorInformation)[0].PercentProcessorPerformance) / 100 / 1000, 2) }"} GHz';
    Lines[24] := 'Temp:               ${alignr}${acpitemp}C';
    Lines[25] := '${cpugraph 50,340 FFA500 FF4500}';
    Lines[26] := '';
    Lines[27] := '';
    Lines[28] := '${font Segoe UI:bold:size=10}GPU [${gpuname GPU_IDX}]';
    Lines[29] := '${font Consolas:size=10}';
    Lines[30] := 'GPU Temp:           ${alignr}${gputemp GPU_IDX}C';
    Lines[31] := 'GPU Util:           ${alignr}${gpuutil GPU_IDX}%';
    Lines[32] := 'Fan Speed:          ${alignr}${gpufan GPU_IDX} RPM';
    Lines[33] := 'VRAM Used:          ${alignr}${gpumemused GPU_IDX}';
    Lines[34] := 'VRAM Total:         ${alignr}${gpumemtotal GPU_IDX}';
    Lines[35] := '${nvidiagraph gpuutil 50,340 00FF00 00AA00 0}';
    Lines[36] := '';
    Lines[37] := '';
    Lines[38] := '${font Segoe UI:bold:size=10}Memory';
    Lines[39] := '${font Consolas:size=10}';
    Lines[40] := 'RAM:                ${alignr}$mem / $memmax ($memperc%)';
    Lines[41] := '${membar 8}';
    Lines[42] := '';
    Lines[43] := 'Swap:               ${alignr}$swap / $swapmax ($swapperc%)';
    Lines[44] := '${swapbar 8}';
    Lines[45] := '';
    Lines[46] := '${font Segoe UI:bold:size=10}Storage';
    Lines[47] := '${font Consolas:size=10}';
    Lines[48] := 'C: ${alignr}${fs_used C:} / ${fs_size C:} (${fs_free_perc C:}% free)';
    Lines[49] := '${fs_bar 8 C:}';
    Lines[50] := '';
    Lines[51] := 'D: ${alignr}${fs_used D:} / ${fs_size D:} (${fs_free_perc D:}% free)';
    Lines[52] := '${fs_bar 8 D:}';
    Lines[53] := '';
    Lines[54] := '${font Segoe UI:bold:size=10}Network [Ethernet]';
    Lines[55] := '${font Consolas:size=10}';
    Lines[56] := 'Down:               ${alignr}${downspeed Ethernet}';
    Lines[57] := 'Up:                 ${alignr}${upspeed Ethernet}';
    Lines[58] := '${downspeedgraph Ethernet 38,340 66ddff 0088ee}';
    Lines[59] := '${upspeedgraph Ethernet 38,340 dd66ff 8800dd -y}';
    Lines[60] := '';
    Lines[61] := '';
    Lines[62] := 'Total Down:         ${alignr}${totaldown Ethernet}';
    Lines[63] := 'Total Up:           ${alignr}${totalup Ethernet}';
    Lines[64] := 'IP Ethernet:        ${alignr}${addr Ethernet}';
    Lines[65] := 'IP Tailscale:       ${alignr}${addr Tailscale}';
    Lines[66] := ']];';

    // Patch GPU index placeholder
    for I := 0 to GetArrayLength(Lines) - 1 do
    begin
      StringChangeEx(Lines[I], 'GPU_IDX', GpuIdxStr, True);
    end;

    SaveStringsToFile(ConfigPath, Lines, False);
  except
    // If inline generation fails for any reason, skip config creation.
    // The user can manually copy the template from {app}\btop.conkyrc.
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    // Wrap all post-install steps in try-except so a failure in any
    // step doesn't roll back the installation or leave a broken state.
    try
      // Always launch lhm-temp (needed for CPU/GPU sensor data)
      SetupTempHelper();

      // Generate config with GPU detection (handles its own timing)
      CreateConfig();
    except
      // Post-install errors are non-fatal — conky will still run,
      // CPU/GPU data just won't be available until lhm-temp is
      // launched manually or at next login.
    end;
  end;
end;
