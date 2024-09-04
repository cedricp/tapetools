[Setup]
AppName=TapeTools
AppVersion=1.0b
DefaultDirName={autopf}\TapeTools
DefaultGroupName=TapeTools
OutputBaseFilename=tapetools_installer

[Files]
Source: "../INSTALL_Release/bin/tapetools.exe"; DestDir: "{app}"
Source: "../INSTALL_Release/bin/*.dll"; DestDir: "{app}"

[Icons]
Name: "{group}\TapeTools"; Filename: "{app}\tapetools.exe"
