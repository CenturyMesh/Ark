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
#ifndef BrandingDirectory
  #error BrandingDirectory is required
#endif
#ifndef InstallerAppId
  #define InstallerAppId "MeshSphere.ARK"
#endif
#ifndef InstallerFilename
  #define InstallerFilename "ARK-" + IterationCode + "-windows-x64-setup"
#endif

[Setup]
AppId={#InstallerAppId}
AppName=ARK
AppVersion={#AppVersion}
AppVerName=ARK {#IterationCode}
AppPublisher=MeshSphere
DefaultDirName={localappdata}\ARK\app
DisableWelcomePage=no
DisableDirPage=no
UsePreviousAppDir=yes
DisableReadyPage=yes
DefaultGroupName=ARK
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
OutputDir={#OutputDirectory}
OutputBaseFilename={#InstallerFilename}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
WizardSmallImageFile={#BrandingDirectory}\ark-setup-small.bmp
SetupIconFile=..\assets\ui\ark-app.ico
UninstallDisplayIcon={app}\versions\{#AppVersion}\ark-studio.exe
CloseApplications=no
RestartApplications=no

[Messages]
WelcomeLabel1=Welcome to ARK
WelcomeLabel2=ARK {#IterationCode}%n%nChoose where to install. Setup will take care of the rest.
SelectDirLabel3=Choose the folder for ARK, then select Install.
FinishedHeadingLabel=ARK is ready
FinishedLabel=Open ARK from your desktop or the Start menu.%n%nYour drawings and settings stay with you when ARK updates.

[Files]
Source: "{#BrandingDirectory}\ark-setup-mark.bmp"; Flags: dontcopy
Source: "{#RuntimeDirectory}\*"; DestDir: "{tmp}\ark-runtime"; Flags: recursesubdirs createallsubdirs ignoreversion uninsneveruninstall

[Icons]
#ifndef InstallerIsolatedTest
Name: "{group}\ARK"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\launcher\ark-launch.ps1"" -InstallRoot ""{app}"""; WorkingDir: "{userdocs}"; IconFilename: "{app}\versions\{#AppVersion}\ark-studio.exe"
Name: "{autodesktop}\ARK"; Filename: "{sys}\WindowsPowerShell\v1.0\powershell.exe"; Parameters: "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\launcher\ark-launch.ps1"" -InstallRoot ""{app}"""; WorkingDir: "{userdocs}"; IconFilename: "{app}\versions\{#AppVersion}\ark-studio.exe"
#endif

[UninstallDelete]
Type: filesandordirs; Name: "{app}\versions"
Type: filesandordirs; Name: "{app}\launcher"
Type: files; Name: "{app}\ARK.cmd"
Type: files; Name: "{app}\current.json"
Type: files; Name: "{app}\update.lock"

[Code]
#ifdef InstallerSelfTest
  #include "..\tests\installer_capture.iss"
#endif

procedure StyleWelcomePage(Page: TNewNotebookPage; Heading, Description: TNewStaticText);
var
  Mark: TBitmapImage;
begin
  Page.ParentBackground := False;
  Page.Color := $24170D;
  Mark := TBitmapImage.Create(WizardForm);
  Mark.Parent := Page;
  Mark.SetBounds((Page.ClientWidth - ScaleX(234)) div 2, ScaleY(20), ScaleX(234), ScaleY(156));
  Mark.Stretch := True;
  Mark.Bitmap.LoadFromFile(ExpandConstant('{tmp}\ark-setup-mark.bmp'));
  Heading.SetBounds(ScaleX(20), ScaleY(187), Page.ClientWidth - ScaleX(40), ScaleY(36));
  Heading.Alignment := taCenter;
  Heading.Font.Name := 'Segoe UI';
  Heading.Font.Size := 19;
  Heading.Font.Color := clWhite;
  Heading.Color := Page.Color;
  Description.SetBounds(ScaleX(30), ScaleY(235), Page.ClientWidth - ScaleX(60), ScaleY(70));
  Description.Alignment := taCenter;
  Description.Font.Color := $E5D4C5;
  Description.Color := Page.Color;
end;

procedure InitializeWizard;
begin
  ExtractTemporaryFile('ark-setup-mark.bmp');
  WizardForm.WizardBitmapImage.Visible := False;
  WizardForm.WizardBitmapImage2.Visible := False;
  StyleWelcomePage(WizardForm.WelcomePage, WizardForm.WelcomeLabel1, WizardForm.WelcomeLabel2);
  StyleWelcomePage(WizardForm.FinishedPage, WizardForm.FinishedHeadingLabel, WizardForm.FinishedLabel);
#ifdef InstallerSelfTest
  StartInstallerSelfTest;
#endif
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpSelectDir then
    WizardForm.NextButton.Caption := SetupMessage(msgButtonInstall);
end;

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
