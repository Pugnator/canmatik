; ---------------------------------------------------------------------------
; CANmatik Inno Setup Installer Script
; Requires Inno Setup 6.0+ (https://jrsoftware.org/isinfo.php)
;
; Build from the repository root:
;   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\canmatik.iss
;
; Or from CMake:
;   cmake --build build32 --target installer
; ---------------------------------------------------------------------------

#define MyAppName      "CANmatik"
#define MyAppVersion   "0.1.0"
#define MyAppPublisher "CANmatik Project"
#define MyAppURL       "https://github.com/canmatik/canmatik"
#define MyAppExeName   "canmatik.exe"
#define BuildDir       "..\build32"
#define SourceDir      ".."

[Setup]
AppId={{B8F4E2A1-3C7D-4A9E-B6D5-1F2E3A4B5C6D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=
OutputDir=Output
OutputBaseFilename=canmatik-{#MyAppVersion}-setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x86 x64
UninstallDisplayIcon={app}\{#MyAppExeName}
WizardStyle=modern
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main application
Source: "{#BuildDir}\canmatik.exe"; DestDir: "{app}"; Flags: ignoreversion

; Fake J2534 DLL (needed for proxy mode)
Source: "{#BuildDir}\fake_j2534.dll"; DestDir: "{app}"; Flags: ignoreversion

; Sample configs
Source: "{#SourceDir}\samples\configs\*"; DestDir: "{app}\configs"; Flags: ignoreversion recursesubdirs createallsubdirs

; Documentation
Source: "{#SourceDir}\docs\user-guide.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#SourceDir}\docs\adapter-compatibility.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#SourceDir}\docs\obd-diagnostics.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#SourceDir}\docs\log-format-spec.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#SourceDir}\docs\safety-notice.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\SAFETY.md"; DestDir: "{app}"; Flags: ignoreversion

; Screenshots (bundled with docs for offline reference)
Source: "{#SourceDir}\docs\*.png"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#SourceDir}\docs\*.jpg"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
