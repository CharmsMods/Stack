#ifndef StackVersion
  #error StackVersion must be defined on the ISCC command line.
#endif
#ifndef StackVersionTag
  #error StackVersionTag must be defined on the ISCC command line.
#endif
#ifndef StackPublisher
  #define StackPublisher "Charm"
#endif
#ifndef StackSourceDir
  #error StackSourceDir must be defined on the ISCC command line.
#endif
#ifndef StackOutputDir
  #error StackOutputDir must be defined on the ISCC command line.
#endif
#ifndef StackLicensePageFile
  #error StackLicensePageFile must be defined on the ISCC command line.
#endif
#ifndef StackMarkerFile
  #error StackMarkerFile must be defined on the ISCC command line.
#endif

#define StackAppId "{{F8F0A486-A815-42B4-B3C4-55B0F7D4F7F7}"

[Setup]
AppId={#StackAppId}
AppName=Stack
AppVersion={#StackVersion}
AppVerName=Stack {#StackVersion}
AppPublisher={#StackPublisher}
DefaultDirName={autopf}\Stack
DefaultGroupName=Stack
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\Stack.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
LicenseFile={#StackLicensePageFile}
OutputDir={#StackOutputDir}
OutputBaseFilename=StackSetup-{#StackVersionTag}-win-x64
Compression=lzma2
SolidCompression=yes
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#StackVersion}
VersionInfoCompany={#StackPublisher}
VersionInfoDescription=Stack Installer
VersionInfoProductName=Stack
VersionInfoTextVersion={#StackVersion}

[Tasks]
Name: "startmenuicon"; Description: "Create a Start Menu shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce

[Files]
Source: "{#StackSourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StackMarkerFile}"; DestDir: "{app}"; DestName: "StackInstalledBuild.marker"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Stack"; Filename: "{app}\Stack.exe"; Tasks: startmenuicon
Name: "{autoprograms}\Uninstall Stack"; Filename: "{uninstallexe}"; Tasks: startmenuicon
Name: "{autodesktop}\Stack"; Filename: "{app}\Stack.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Stack.exe"; Description: "Launch Stack"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
var
  RemoveUserData: Boolean;

function InitializeUninstall(): Boolean;
var
  Response: Integer;
begin
  Result := True;
  Response :=
    SuppressibleMsgBox(
      'Remove Stack user data too?' + #13#10 + #13#10 +
      'Choose Yes to remove known Stack data from AppData and LocalAppData, including settings, presets, the Stack library, cached downloads, and update files.' + #13#10 + #13#10 +
      'Choose No to uninstall only the app files and preserve your data.',
      mbConfirmation,
      MB_YESNO or MB_DEFBUTTON2,
      IDNO);
  RemoveUserData := Response = IDYES;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usUninstall) and RemoveUserData then
  begin
    DelTree(ExpandConstant('{userappdata}\Stack'), True, True, True);
    DelTree(ExpandConstant('{localappdata}\Stack'), True, True, True);
  end;
end;
