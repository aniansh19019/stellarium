; Stellarium installer
; Run "make install" first to generate the executable and translation files.
; @ISS_AUTOGENERATED_WARNING@

[Setup]
@ISS_ARCHITECTURE_SPECIFIC@
DisableStartupPrompt=yes
DisableDirPage=no
DisableProgramGroupPage=no
WizardSmallImageFile=@CMAKE_SOURCE_DIR@\data\icon.bmp
WizardImageFile=@CMAKE_SOURCE_DIR@\data\splash.bmp
WizardImageStretch=no
WizardImageBackColor=clBlack
AppName=Stellarium
AppVersion=@PACKAGE_VERSION@
AppVerName=Stellarium @PACKAGE_VERSION@
AppCopyright=@STELLARIUM_COPYRIGHT@
AppPublisher=Stellarium team
AppPublisherURL=@STELLARIUM_URL@
AppSupportURL=@STELLARIUM_URL@
AppUpdatesURL=@STELLARIUM_URL@
VersionInfoVersion=@WINDOWS_PACKAGE_VERSION@
MinVersion=0,@ISS_MIN_WIN_VERSION@
SetupIconFile=@CMAKE_SOURCE_DIR@\data\@PACKAGE_ICON@.ico
OutputBaseFilename=stellarium-@PACKAGE_VERSION@-@ISS_PACKAGE_PLATFORM@
OutputDir=@CMAKE_SOURCE_DIR@\installers
; In 64-bit mode, {pf} is equivalent to {pf64},
; see http://www.jrsoftware.org/ishelp/index.php?topic=32vs64bitinstalls
DefaultDirName={pf}\Stellarium
DefaultGroupName=Stellarium
UninstallDisplayIcon={app}\data\stellarium.ico
LicenseFile=@CMAKE_SOURCE_DIR@\COPYING
ChangesAssociations=yes
; LZMA2/max required 95 MB RAM for compression and 8 MB RAM for decompression
; Using LZMA2/max algorithm reduces size of package on ~10%
Compression=lzma2/max

[Files]
Source: "@CMAKE_INSTALL_PREFIX@\bin\stellarium.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "@CMAKE_INSTALL_PREFIX@\bin\*.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "@CMAKE_SOURCE_DIR@\data\stellarium.url"; DestDir: "{app}"; Flags: ignoreversion
Source: "@CMAKE_SOURCE_DIR@\data\stellarium-devdocs.url"; DestDir: "{app}"; Flags: ignoreversion
Source: "@CMAKE_SOURCE_DIR@\*.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "@CMAKE_SOURCE_DIR@\COPYING"; DestDir: "{app}"; Flags: ignoreversion; DestName: "GPL.txt"
Source: "@CMAKE_SOURCE_DIR@\ChangeLog"; DestDir: "{app}"; Flags: ignoreversion; DestName: "ChangeLog.txt"
; Qt5 stuff
Source: "@CMAKE_INSTALL_PREFIX@\qt5stuff\*"; DestDir: "{app}\"; Flags: recursesubdirs ignoreversion
; Stellarium's stuff
Source: "@CMAKE_INSTALL_PREFIX@\share\stellarium\*"; DestDir: "{app}\"; Flags: recursesubdirs ignoreversion
@ISS_OPENSSL_LIBS@
@ISS_STELLARIUM_STUFF@

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: desktopicon\common; Description: "{cm:ForAllUsers}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: exclusive
Name: desktopicon\user; Description: "{cm:ForCurrentUserOnly}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: exclusive unchecked
; Name: removecache; Description: "{cm:RemoveCache}"; GroupDescription: "{cm:RemoveFromPreviousInstallation}"
; Name: removeconfig; Description: "{cm:RemoveMainConfig}"; GroupDescription: "{cm:RemoveFromPreviousInstallation}"
; Name: removeplugins; Description: "{cm:RemovePluginsConfig}"; GroupDescription: "{cm:RemoveFromPreviousInstallation}"; Flags: unchecked
; Name: removesolar; Description: "{cm:RemoveSolarConfig}"; GroupDescription: "{cm:RemoveFromPreviousInstallation}"

[Run]
; An option to start Stellarium after setup has finished
Filename: "{app}\stellarium.exe"; Description: "{cm:LaunchProgram,Stellarium}"; Flags: postinstall nowait skipifsilent unchecked

[InstallDelete]
;The old log file in all cases
Type: files; Name: "{userappdata}\Stellarium\log.txt"
; Type: files; Name: "{userappdata}\Stellarium\config.ini"; Tasks: removeconfig
; Type: files; Name: "{userappdata}\Stellarium\data\ssystem_minor.ini"; Tasks: removesolar
; Type: filesandordirs; Name: "{userappdata}\Stellarium\modules"; Tasks: removeplugins
; Type: filesandordirs; Name: "{localappdata}\stellarium\stellarium"; Tasks: removecache

[UninstallDelete]

[Icons]
; Programs
Name: "{group}\Stellarium"; Filename: "{app}\stellarium.exe"; Parameters: "-platform windows:altgr"; WorkingDir: "{app}"; IconFilename: "{app}\data\stellarium.ico"
Name: "{group}\Stellarium {cm:AngleMode}"; Filename: "{app}\stellarium.exe"; Parameters: "--angle-d3d9 -platform windows:altgr"; WorkingDir: "{app}"; IconFilename: "{app}\data\stellarium.ico"
Name: "{group}\Stellarium {cm:AngleD3D11Mode}"; Filename: "{app}\stellarium.exe"; Parameters: "--angle-d3d11 -platform windows:altgr"; WorkingDir: "{app}"; IconFilename: "{app}\data\stellarium.ico"
Name: "{group}\Stellarium {cm:MesaMode}"; Filename: "{app}\stellarium.exe"; Parameters: "--mesa-mode -platform windows:altgr"; WorkingDir: "{app}"; IconFilename: "{app}\data\stellarium.ico"
@ISS_SPOUT@
Name: "{group}\Stellarium {cm:DebugMode}"; Filename: "{app}\stellarium.exe"; Parameters: "--dump-opengl-details -platform windows:altgr"; WorkingDir: "{app}"; IconFilename: "{app}\data\stellarium.ico"
; Extra stuff: config, change log, etc.
Name: "{group}\config.ini"; Filename: "{userappdata}\Stellarium\config.ini"
Name: "{group}\{cm:LastRunLog}"; Filename: "{userappdata}\Stellarium\log.txt"
Name: "{group}\{cm:OutputDataFile}"; Filename: "{userappdata}\Stellarium\output.txt"
Name: "{group}\{cm:ChangeLog}"; Filename: "{app}\ChangeLog.txt"
; Documents
@ISS_GUIDE@
Name: "{group}\{cm:ProgramOnTheWeb,Stellarium}"; Filename: "{app}\stellarium.url"; IconFilename: "{app}\data\stellarium.ico"
Name: "{group}\{cm:DevelopersDocsOnTheWeb}"; Filename: "{app}\stellarium-devdocs.url"; IconFilename: "{app}\data\stellarium.ico"
; Uninstaller
Name: "{group}\{cm:UninstallProgram,Stellarium}"; Filename: "{uninstallexe}"
; Common desktop
Name: "{commondesktop}\Stellarium"; Filename: "{app}\stellarium.exe"; Parameters: "-platform windows:altgr"; WorkingDir: "{app}"; IconFilename: "{app}\data\stellarium.ico"; Tasks: desktopicon\common
Name: "{commondesktop}\Stellarium {cm:AngleMode}"; Filename: "{app}\stellarium.exe"; Parameters: "--angle-d3d9 -platform windows:altgr"; WorkingDir: "{app}"; IconFilename: "{app}\data\stellarium.ico"; Tasks: desktopicon\common
@ISS_GUIDE_CD@
; User desktop
Name: "{userdesktop}\Stellarium"; Filename: "{app}\stellarium.exe"; Parameters: "-platform windows:altgr"; WorkingDir: "{app}"; IconFilename: "{app}\data\stellarium.ico"; Tasks: desktopicon\user
Name: "{userdesktop}\Stellarium {cm:AngleMode}"; Filename: "{app}\stellarium.exe"; Parameters: "--angle-d3d9 -platform windows:altgr"; WorkingDir: "{app}"; IconFilename: "{app}\data\stellarium.ico"; Tasks: desktopicon\user
@ISS_GUIDE_UD@

[Registry]
; Set file associations for Stellarium scripts
Root: HKCR; Subkey: ".ssc"; ValueType: string; ValueName: ""; ValueData: "Stellarium.Script"; Flags: uninsdeletevalue
Root: HKCR; Subkey: "Stellarium.Script"; ValueType: string; ValueName: ""; ValueData: "Stellarium Script"; Flags: uninsdeletekey
Root: HKCR; Subkey: "Stellarium.Script\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\stellarium.exe,0"
Root: HKCR; Subkey: "Stellarium.Script\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\stellarium.exe"" --startup-script ""%1"""

; Recommended use Inno Setup 5.5.3+
[Languages]
; Official translations of GUI of Inno Setup + translation Stellarium specific lines
Name: "en"; MessagesFile: "compiler:Default.isl,@CMAKE_SOURCE_DIR@\util\ISL\EnglishCM.isl"
Name: "ca"; MessagesFile: "compiler:Languages\Catalan.isl,@CMAKE_SOURCE_DIR@\util\ISL\CatalanCM.isl"
Name: "co"; MessagesFile: "compiler:Languages\Corsican.isl"
Name: "cs"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "da"; MessagesFile: "compiler:Languages\Danish.isl"
Name: "nl"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "fi"; MessagesFile: "compiler:Languages\Finnish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl,@CMAKE_SOURCE_DIR@\util\ISL\FrenchCM.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl,@CMAKE_SOURCE_DIR@\util\ISL\GermanCM.isl"
Name: "he"; MessagesFile: "compiler:Languages\Hebrew.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "ja"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "no"; MessagesFile: "compiler:Languages\Norwegian.isl,@CMAKE_SOURCE_DIR@\util\ISL\NorwegianCM.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "pt_BR"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl,@CMAKE_SOURCE_DIR@\util\ISL\BrazilianPortugueseCM.isl"
Name: "pt"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl,@CMAKE_SOURCE_DIR@\util\ISL\RussianCM.isl"
Name: "sl"; MessagesFile: "compiler:Languages\Slovenian.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "uk"; MessagesFile: "compiler:Languages\Ukrainian.isl,@CMAKE_SOURCE_DIR@\util\ISL\UkrainianCM.isl"
; Unofficial translations of GUI of Inno Setup
Name: "el"; MessagesFile: "@CMAKE_SOURCE_DIR@\util\ISL\Greek.isl"
Name: "hu"; MessagesFile: "@CMAKE_SOURCE_DIR@\Hungarian.isl"
Name: "sr"; MessagesFile: "@CMAKE_SOURCE_DIR@\SerbianCyrillic.isl"
Name: "bg"; MessagesFile: "@CMAKE_SOURCE_DIR@\util\ISL\Bulgarian.isl,@CMAKE_SOURCE_DIR@\util\ISL\BulgarianCM.isl"
Name: "bs"; MessagesFile: "@CMAKE_SOURCE_DIR@\util\ISL\Bosnian.isl,@CMAKE_SOURCE_DIR@\util\ISL\BosnianCM.isl"
Name: "ko"; MessagesFile: "@CMAKE_SOURCE_DIR@\util\ISL\Korean.isl,@CMAKE_SOURCE_DIR@\util\ISL\KoreanCM.isl"
Name: "zh_CN"; MessagesFile: "@CMAKE_SOURCE_DIR@\util\ISL\ChineseSimplified.isl,@CMAKE_SOURCE_DIR@\util\ISL\ChineseSimplifiedCM.isl"
Name: "zh_TW"; MessagesFile: "@CMAKE_SOURCE_DIR@\util\ISL\ChineseTraditional.isl,@CMAKE_SOURCE_DIR@\util\ISL\ChineseTraditionalCM.isl"

[Code]
procedure CurUninstallStepChanged (CurUninstallStep: TUninstallStep);
var
  mres : integer;
begin
  case CurUninstallStep of
    usPostUninstall:
      begin
        mres := SuppressibleMsgBox(ExpandConstant('{cm:DeleteUserData}'), mbConfirmation, MB_YESNO, IDNO)
        if mres = IDYES then
          DelTree(ExpandConstant('{userappdata}\Stellarium'), True, True, True);
          DelTree(ExpandConstant('{userdocs}\Stellarium'), True, True, True);
          DelTree(ExpandConstant('{localappdata}\stellarium'), True, True, True);
      end;
  end;
end;
