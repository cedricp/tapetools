[Setup]
AppName=TapeTools
AppVersion=1.0b
DefaultDirName={autopf}\TapeTools
DefaultGroupName=TapeTools
OutputBaseFilename=tapetools_installer

[Code]
procedure DeinitializeSetup();
var
  ErrCode: integer;
begin
  if MsgBox('Would you like to support my work by making a dontation', mbConfirmation, MB_YESNO) = IDYES
  then begin
    ShellExec('open', 'https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=cedricpaille@gmail.com&lc=CY&item_name=codetronic&currency_code=EUR&bn=PP%2dDonationsBF%3abtn_donateCC_LG.if:NonHosted',
      '', '', SW_SHOW, ewNoWait, ErrCode);
  end;
end;

[Files]
Source: "../INSTALL_Release/bin/tapetools.exe"; DestDir: "{app}"
Source: "../INSTALL_Release/bin/*.dll"; DestDir: "{app}"

[Icons]
Name: "{group}\TapeTools"; Filename: "{app}\tapetools.exe"
