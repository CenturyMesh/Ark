#ifndef RuntimeDirectory
  #error RuntimeDirectory is required
#endif
#ifndef OutputDirectory
  #error OutputDirectory is required
#endif
#ifndef AppVersion
  #error AppVersion is required
#endif
#ifndef IterationCode
  #error IterationCode is required
#endif

[Setup]
AppId=MeshSphere.ARK
AppName=ARK
AppVersion={#AppVersion}
AppVerName=ARK {#IterationCode}
AppPublisher=MeshSphere
DefaultDirName={localappdata}\ARK\app
DisableDirPage=yes
UsePreviousAppDir=no
DefaultGroupName=ARK
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
OutputDir={#OutputDirectory}
OutputBaseFilename=ARK-{#IterationCode}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\versions\{#AppVersion}\ark-studio.exe
CloseApplications=no
RestartApplications=no

[Tasks]
Name: desktopicon; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#RuntimeDirectory}\*"; DestDir: "{tmp}\ark-runtime"; Flags: recursesubdirs createallsubdirs ignoreversion uninsneveruninstall

[Icons]
Name: "{group}\ARK"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\launcher\ark-launch.ps1"" -InstallRoot ""{app}"""; WorkingDir: "{userdocs}"; IconFilename: "{app}\versions\{#AppVersion}\ark-studio.exe"
Name: "{autodesktop}\ARK"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\launcher\ark-launch.ps1"" -InstallRoot ""{app}"""; WorkingDir: "{userdocs}"; IconFilename: "{app}\versions\{#AppVersion}\ark-studio.exe"; Tasks: desktopicon

[UninstallDelete]
Type: filesandordirs; Name: "{app}\versions"
Type: filesandordirs; Name: "{app}\launcher"
Type: files; Name: "{app}\ARK.cmd"
Type: files; Name: "{app}\current.json"
Type: files; Name: "{app}\update.lock"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  Parameters: String;
begin
  if CurStep = ssPostInstall then
  begin
    Parameters := '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
      ExpandConstant('{tmp}\ark-runtime\Install-ARK.ps1') + '" -InstallRoot "' +
      ExpandConstant('{app}') + '"';
    if not Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
      Parameters, ExpandConstant('{tmp}\ark-runtime'), SW_HIDE,
      ewWaitUntilTerminated, ResultCode) then
      RaiseException('ARK installation could not start.');
    if ResultCode <> 0 then
      RaiseException('ARK installation failed (code ' + IntToStr(ResultCode) + ').');
  end;
end;
