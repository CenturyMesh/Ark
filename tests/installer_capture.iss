// Compiled only into a separately registered private test installer.
// The application captures and drives its own controls; there is no external
// desktop automation and these entry points do not exist in a public build.
function PrintOwnWizard(Window: HWND; Message: Cardinal; DC: LongWord; Flags: Longint): Longint;
  external 'SendMessageW@user32.dll stdcall';
function ShowOwnWizard(Window: HWND; Command: Integer): Boolean;
  external 'ShowWindow@user32.dll stdcall';
function CaptureOwnWizard(Window: HWND; DC: LongWord; Flags: Cardinal): Boolean;
  external 'PrintWindow@user32.dll stdcall';
function OwnWindowDC(Window: HWND): LongWord;
  external 'GetWindowDC@user32.dll stdcall';
function ReleaseOwnDC(Window: HWND; DC: LongWord): Integer;
  external 'ReleaseDC@user32.dll stdcall';
function CopyOwnPixels(Dest: LongWord; X, Y, Width, Height: Integer; Source: LongWord; SourceX, SourceY: Integer; Operation: LongWord): Boolean;
  external 'BitBlt@gdi32.dll stdcall';
function CaptureLastError: LongWord;
  external 'GetLastError@kernel32.dll stdcall';
function SetCaptureTimer(Window: HWND; Event: LongWord; Interval: Cardinal; Callback: LongWord): LongWord;
  external 'SetTimer@user32.dll stdcall';
function StopCaptureTimer(Window: HWND; Event: LongWord): Boolean;
  external 'KillTimer@user32.dll stdcall';

var
  CaptureTimer: LongWord;
  LastCapturedPage: Integer;
  CaptureDirectory: String;

function Bounds(Control: TControl): String;
begin
  Result := IntToStr(Control.Left) + ',' + IntToStr(Control.Top) + ',' +
    IntToStr(Control.Width) + ',' + IntToStr(Control.Height);
end;

procedure RecordLayout(Name: String; Page: TNewNotebookPage; Heading, Description: TNewStaticText);
var
  I: Integer;
  Control: TControl;
begin
  SaveStringToFile(CaptureDirectory + '\layout.txt', Name + ': page=' + Bounds(Page) +
    '; heading=' + Bounds(Heading) + '; heading-font=' + Heading.Font.Name + '/' + IntToStr(Heading.Font.Size) +
    '; description=' + Bounds(Description) + '; description-font=' + Description.Font.Name + '/' + IntToStr(Description.Font.Size) +
    '; text=' + Heading.Caption + #13#10, True);
  for I := 0 to Page.ControlCount - 1 do
  begin
    Control := Page.Controls[I];
    if (Control is TBitmapImage) and Control.Visible then
      SaveStringToFile(CaptureDirectory + '\layout.txt', Name + ': mark=' + Bounds(Control) +
        '; centered=' + IntToStr(Ord(Abs((2 * Control.Left + Control.Width) - Page.ClientWidth) <= 1)) +
        '; no-overlap=' + IntToStr(Ord((Control.Top + Control.Height <= Heading.Top) and
          (Heading.Top + Heading.Height <= Description.Top) and
          (Description.Top + Description.Height <= Page.ClientHeight))) + #13#10, True);
  end;
end;

procedure CaptureTick(Window: HWND; Message: Cardinal; Event: LongWord; Time: Cardinal);
var
  Bitmap: TBitmap;
  Name: String;
  SourceDC, DestinationDC: LongWord;
  Printed, Copied: Boolean;
  CaptureError: LongWord;
begin
  if (WizardForm.CurPageID = LastCapturedPage) or not WizardForm.Showing then exit;
  if WizardForm.CurPageID = wpWelcome then Name := 'welcome'
  else if WizardForm.CurPageID = wpSelectDir then Name := 'directory'
  else if WizardForm.CurPageID = wpFinished then Name := 'finished'
  else exit;
  LastCapturedPage := WizardForm.CurPageID;
  if WizardForm.CurPageID = wpWelcome then
    RecordLayout(Name, WizardForm.WelcomePage, WizardForm.WelcomeLabel1, WizardForm.WelcomeLabel2)
  else if WizardForm.CurPageID = wpFinished then
    RecordLayout(Name, WizardForm.FinishedPage, WizardForm.FinishedHeadingLabel, WizardForm.FinishedLabel)
  else
    SaveStringToFile(CaptureDirectory + '\layout.txt', 'directory: path=' + Bounds(WizardForm.DirEdit) +
      '; browse=' + Bounds(WizardForm.DirBrowseButton) + '; next=' + Bounds(WizardForm.NextButton) + #13#10, True);
  Bitmap := TBitmap.Create;
  try
    Bitmap.Width := WizardForm.Width;
    Bitmap.Height := WizardForm.Height;
    ShowOwnWizard(WizardForm.Handle, SW_SHOW);
    WizardForm.Repaint;
    // Omit PRF_CHECKVISIBLE: CLI tests can start with a hidden launch window.
    DestinationDC := Bitmap.Canvas.Handle;
    PrintOwnWizard(WizardForm.Handle, $0317, DestinationDC, $3E);
    Printed := CaptureOwnWizard(WizardForm.Handle, DestinationDC, 2);
    SourceDC := OwnWindowDC(WizardForm.Handle);
    Copied := CopyOwnPixels(DestinationDC, 0, 0, Bitmap.Width, Bitmap.Height, SourceDC, 0, 0, $00CC0020);
    CaptureError := CaptureLastError;
    ReleaseOwnDC(WizardForm.Handle, SourceDC);
    Bitmap.SaveToFile(CaptureDirectory + '\' + Name + '.bmp');
    SaveStringToFile(CaptureDirectory + '\controls.txt', Name + ': next=' +
      WizardForm.NextButton.Caption + '; path=' + WizardDirValue + #13#10, True);
    SaveStringToFile(CaptureDirectory + '\capture-results.txt', Name + ': hwnd=' + IntToStr(WizardForm.Handle) +
      '; dc=' + IntToStr(DestinationDC) + '; source=' + IntToStr(SourceDC) + '; print=' + IntToStr(Ord(Printed)) + '; copy=' + IntToStr(Ord(Copied)) + '; error=' + IntToStr(CaptureError) + #13#10, True);
  finally
    Bitmap.Free;
  end;
  if WizardForm.CurPageID = wpFinished then StopCaptureTimer(0, CaptureTimer);
  WizardForm.NextButton.OnClick(WizardForm.NextButton);
end;

procedure StartInstallerSelfTest;
begin
  CaptureDirectory := ExpandConstant('{param:ARKTESTCAPTURE|}');
  if CaptureDirectory = '' then exit;
  ForceDirectories(CaptureDirectory);
  LastCapturedPage := -100;
  CaptureTimer := SetCaptureTimer(0, 0, 700, CreateCallback(@CaptureTick));
end;
