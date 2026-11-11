#ifndef AppVersion
  #error Pass the version with /DAppVersion=<version>
#endif
#ifndef ReleaseDir
  #error Pass the folder of release files with /DReleaseDir=<folder>
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
#ifndef AppUrl
  #define AppUrl "https://github.com/NuoRan/GameScrcpy"
#endif

[Setup]
AppId={{0E92AB34-0BC4-4585-981A-51F0A82446DD}
AppName=GameScrcpy
AppVersion={#AppVersion}
AppVerName=GameScrcpy {#AppVersion}
AppPublisher=GameScrcpy
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}
AppUpdatesURL={#AppUrl}
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\GameScrcpy
DefaultGroupName=GameScrcpy
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
AppMutex=GameScrcpyRunning,Global\GameScrcpyRunning
CloseApplications=yes
RestartApplications=no
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage
WizardStyle=modern
SetupIconFile=..\src\app\resources\GameScrcpy.ico
UninstallDisplayName=GameScrcpy
UninstallDisplayIcon={app}\GameScrcpy.exe
OutputDir={#OutputDir}
OutputBaseFilename=GameScrcpy-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zh_CN"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[InstallDelete]
Type: filesandordirs; Name: "{app}\qml"
Type: filesandordirs; Name: "{app}\typescript"
Type: filesandordirs; Name: "{app}\components"
Type: filesandordirs; Name: "{app}\tasks"
Type: filesandordirs; Name: "{app}\extensions"

[Files]
Source: "{#ReleaseDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\GameScrcpy"; Filename: "{app}\GameScrcpy.exe"
Name: "{autodesktop}\GameScrcpy"; Filename: "{app}\GameScrcpy.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\gamescrcpy"; ValueType: string; ValueName: ""; ValueData: "URL:GameScrcpy"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\gamescrcpy"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\gamescrcpy\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\GameScrcpy.exe"",0"
Root: HKA; Subkey: "Software\Classes\gamescrcpy\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\GameScrcpy.exe"" ""%1"""

[Run]
Filename: "{app}\GameScrcpy.exe"; Description: "{cm:LaunchProgram,GameScrcpy}"; Flags: nowait postinstall skipifsilent
