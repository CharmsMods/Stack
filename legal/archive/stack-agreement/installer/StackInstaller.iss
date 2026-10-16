#ifndef StackVersion
  #error StackVersion must be defined on the ISCC command line.
#endif
#ifndef StackVersionTag
  #error StackVersionTag must be defined on the ISCC command line.
#endif
#ifndef StackPublisher
  #define StackPublisher "Darynn Ho"
#endif
#ifndef StackSourceDir
  #error StackSourceDir must be defined on the ISCC command line.
#endif
#ifndef StackOutputDir
  #error StackOutputDir must be defined on the ISCC command line.
#endif
#ifndef StackEulaFile
  #error StackEulaFile must be defined on the ISCC command line.
#endif
#ifndef StackInfoBeforeFile
  #error StackInfoBeforeFile must be defined on the ISCC command line.
#endif
#ifndef StackMarkerFile
  #error StackMarkerFile must be defined on the ISCC command line.
#endif
#ifndef StackEulaVersion
  #error StackEulaVersion must be defined on the ISCC command line.
#endif
#ifndef StackEulaSha256
  #error StackEulaSha256 must be defined on the ISCC command line.
#endif
#ifndef StackPrivacyVersion
  #error StackPrivacyVersion must be defined on the ISCC command line.
#endif
#ifndef StackLocalTest
  #error StackLocalTest must be 0 or 1 on the ISCC command line.
#endif
#ifndef StackPublicRelease
  #error StackPublicRelease must be 0 or 1 on the ISCC command line.
#endif
#ifndef StackProduct
  #define StackProduct "Stack"
#endif
#ifndef StackShellClsid
  #error StackShellClsid must be supplied from the product manifest.
#endif
#if (StackProduct != "Stack") && (StackProduct != "Burst")
  #error StackProduct must be Stack or Burst.
#endif

#if StackProduct == "Burst"
  #define StackExecutable "StackBurst.exe"
  #define StackMarkerBase "StackBurst"
  #if StackLocalTest == "1"
    #define StackAppId "{{83CB2558-D43B-4978-A335-E04558BCCDEE}"
    #define StackAppName "Stack Burst Local Test"
    #define StackDefaultDirName "{autopf}\Stack Burst Local Test"
    #define StackOutputBaseFilename "StackBurstSetup-LOCAL-TEST-UNSIGNED-LEGAL-DRAFT-" + StackVersion
    #define StackLegalRegistrySubkey "Software\Darynn Ho\Stack Burst Local Test\Legal"
    #define StackMarkerDestName "StackBurstLocalTestBuild.marker"
    #define StackUserDataDirectoryName "Stack Burst Local Test"
  #else
    #define StackAppId "{{C912DD27-B3A8-4E55-939F-046EA580CC9D}"
    #define StackAppName "Stack Burst"
    #define StackDefaultDirName "{autopf}\Stack Burst"
    #define StackOutputBaseFilename "StackBurstSetup-" + StackVersionTag + "-win-x64"
    #define StackLegalRegistrySubkey "Software\Darynn Ho\Stack Burst\Legal"
    #define StackMarkerDestName "StackBurstInstalledBuild.marker"
    #define StackUserDataDirectoryName "Stack Burst"
  #endif
#else
  #define StackExecutable "Stack.exe"
  #define StackMarkerBase "Stack"
#if StackLocalTest == "1"
  #define StackAppId "{{AF9EB59E-0F46-4B18-AB25-8F3D9B0E6C0F}"
  #define StackAppName "Stack Local Test"
  #define StackDefaultDirName "{autopf}\Stack Local Test"
  #define StackOutputBaseFilename "StackSetup-LOCAL-TEST-UNSIGNED-LEGAL-DRAFT-" + StackVersion
  #define StackLegalRegistrySubkey "Software\Darynn Ho\Stack Local Test\Legal"
  #define StackMarkerDestName "StackLocalTestBuild.marker"
  #define StackUserDataDirectoryName "Stack Local Test"
#else
  #define StackAppId "{{F8F0A486-A815-42B4-B3C4-55B0F7D4F7F7}"
  #define StackAppName "Stack"
  #define StackDefaultDirName "{autopf}\Stack"
  #define StackOutputBaseFilename "StackSetup-" + StackVersionTag + "-win-x64"
  #define StackLegalRegistrySubkey "Software\Darynn Ho\Stack\Legal"
  #define StackMarkerDestName "StackInstalledBuild.marker"
  #define StackUserDataDirectoryName "Stack"
#endif
#endif

#if StackLocalTest == "1"
  #define StackShellProvider StackProduct + "LocalTest"
#else
  #define StackShellProvider StackProduct
#endif

#if StackPublicRelease == "1"
  #ifndef StackInnoSignToolName
    #error Public releases require StackInnoSignToolName.
  #endif
#endif

[Setup]
AppId={#StackAppId}
AppName={#StackAppName}
AppVersion={#StackVersion}
AppVerName={#StackAppName} {#StackVersion}
AppPublisher={#StackPublisher}
AppPublisherURL=https://github.com/CharmsMods/Stack
AppSupportURL=https://github.com/CharmsMods/Stack/issues
AppUpdatesURL=https://github.com/CharmsMods/Stack/releases
AppContact=https://github.com/CharmsMods/Stack/issues
AppCopyright=Copyright (c) 2024-2026 Darynn Ho. All rights reserved.
DefaultDirName={#StackDefaultDirName}
DefaultGroupName={#StackAppName}
DisableProgramGroupPage=yes
UninstallDisplayName={#StackAppName}
UninstallDisplayIcon={app}\{#StackExecutable}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
WizardStyle=modern
LicenseFile={#StackEulaFile}
InfoBeforeFile={#StackInfoBeforeFile}
OutputDir={#StackOutputDir}
OutputBaseFilename={#StackOutputBaseFilename}
Compression=lzma2
SolidCompression=yes
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#StackVersion}
VersionInfoCompany={#StackPublisher}
VersionInfoCopyright=Copyright (c) 2024-2026 Darynn Ho. All rights reserved.
VersionInfoDescription={#StackAppName} Installer
VersionInfoProductName={#StackAppName}
VersionInfoTextVersion={#StackVersion}
#if StackPublicRelease == "1"
SignTool={#StackInnoSignToolName}
SignedUninstaller=yes
#else
SignedUninstaller=no
#endif

[Tasks]
Name: "startmenuicon"; Description: "Create a Start Menu shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: checkedonce

#if StackProduct != "Burst"
[Dirs]
Name: "{app}\Stack Projects"; Permissions: users-modify
#endif

[Files]
Source: "{#StackSourceDir}\*"; DestDir: "{app}"; Excludes: "Stack\App\Runtime\StackBurstShell.dll"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StackSourceDir}\Stack\App\Runtime\StackBurstShell.dll"; DestDir: "{app}\Stack\App\Runtime"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#StackMarkerFile}"; DestDir: "{app}\Stack\App"; DestName: "{#StackMarkerDestName}"; Flags: ignoreversion

[Registry]
Root: HKLM; Subkey: "{#StackLegalRegistrySubkey}"; ValueType: none; Flags: uninsdeletekey; Permissions: users-modify

[Icons]
Name: "{autoprograms}\{#StackAppName}"; Filename: "{app}\{#StackExecutable}"; WorkingDir: "{app}"; Tasks: startmenuicon
Name: "{autodesktop}\{#StackAppName}"; Filename: "{app}\{#StackExecutable}"; WorkingDir: "{app}"; Tasks: desktopicon
Name: "{autoprograms}\Uninstall {#StackAppName}"; Filename: "{uninstallexe}"; WorkingDir: "{app}"; Tasks: startmenuicon

[Run]
Filename: "{app}\{#StackExecutable}"; Description: "Launch {#StackAppName}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
type
  TSystemTime = record
    Year: Word;
    Month: Word;
    DayOfWeek: Word;
    Day: Word;
    Hour: Word;
    Minute: Word;
    Second: Word;
    Milliseconds: Word;
  end;

var
  RemoveUserData: Boolean;

procedure GetSystemTime(var SystemTime: TSystemTime);
  external 'GetSystemTime@kernel32.dll stdcall';

{ One common visible verb, with isolated installation-owned COM registrations. }
#include "BurstShellRegistration.iss"

function TwoDigits(Value: Word): String;
begin
  Result := Format('%.2d', [Value]);
end;

function CurrentUtcTimestamp(): String;
var
  SystemTime: TSystemTime;
begin
  GetSystemTime(SystemTime);
  Result := Format('%.4d-%s-%sT%s:%s:%sZ', [
    SystemTime.Year,
    TwoDigits(SystemTime.Month),
    TwoDigits(SystemTime.Day),
    TwoDigits(SystemTime.Hour),
    TwoDigits(SystemTime.Minute),
    TwoDigits(SystemTime.Second)]);
end;

function AcceptanceMatchesCurrentEula(): Boolean;
var
  StoredVersion: String;
  StoredHash: String;
begin
  Result :=
    RegQueryStringValue(HKLM, '{#StackLegalRegistrySubkey}', 'EulaVersion', StoredVersion) and
    RegQueryStringValue(HKLM, '{#StackLegalRegistrySubkey}', 'EulaSha256', StoredHash) and
    (CompareText(StoredVersion, '{#StackEulaVersion}') = 0) and
    (CompareText(StoredHash, '{#StackEulaSha256}') = 0);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = wpLicense) and AcceptanceMatchesCurrentEula();
end;

function InitializeSetup(): Boolean;
var
  AcceptedVersion: String;
begin
  Result := BeginBurstInstallation();
  if not Result then exit;
  if WizardSilent then
  begin
    AcceptedVersion := ExpandConstant('{param:ACCEPTEULA|}');
    if CompareText(AcceptedVersion, '{#StackEulaVersion}') <> 0 then
    begin
      SuppressibleMsgBox(
        'Silent installation requires /ACCEPTEULA={#StackEulaVersion}. ' +
        'The supplied value was missing or did not match the EULA embedded in this installer.',
        mbCriticalError,
        MB_OK,
        IDOK);
      Result := False;
    end;
  end;
end;

procedure RecordAcceptance();
var
  Source: String;
begin
  if WizardSilent then
    Source := 'silent-installer'
  else
    Source := 'interactive-installer';

  RegWriteStringValue(HKLM, '{#StackLegalRegistrySubkey}', 'EulaVersion', '{#StackEulaVersion}');
  RegWriteStringValue(HKLM, '{#StackLegalRegistrySubkey}', 'EulaSha256', '{#StackEulaSha256}');
  RegWriteStringValue(HKLM, '{#StackLegalRegistrySubkey}', 'PrivacyVersion', '{#StackPrivacyVersion}');
  RegWriteStringValue(HKLM, '{#StackLegalRegistrySubkey}', 'AcceptedAtUtc', CurrentUtcTimestamp());
  RegWriteStringValue(HKLM, '{#StackLegalRegistrySubkey}', 'AcceptanceSource', Source);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    InstallBurstShell();
  { Preserve the original audit timestamp on an unchanged upgrade. }
  if (CurStep = ssPostInstall) and (not AcceptanceMatchesCurrentEula()) then
    RecordAcceptance();
end;

function InitializeUninstall(): Boolean;
var
  Response: Integer;
begin
  Result := BeginBurstInstallation();
  if not Result then exit;
  Response :=
    SuppressibleMsgBox(
      'Remove {#StackAppName} user data too?' + #13#10 + #13#10 +
#if StackProduct == "Burst"
      'Choose Yes to remove this product''s settings and cached files from AppData and LocalAppData.' + #13#10 + #13#10 +
#else
      'Choose Yes to remove known Stack data from AppData and LocalAppData, including settings, presets, the Stack library, cached downloads, and update files.' + #13#10 + #13#10 +
#endif
      'Choose No to uninstall only the app files and preserve your data.',
      mbConfirmation,
      MB_YESNO or MB_DEFBUTTON2,
      IDNO);
  RemoveUserData := Response = IDYES;
end;

procedure DeinitializeSetup();
begin
  EndBurstInstallation();
end;

procedure DeinitializeUninstall();
begin
  EndBurstInstallation();
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
#if StackProduct == "Burst"
  if FileExists(ExpandConstant('{app}\Stack.exe')) or
     FileExists(ExpandConstant('{app}\Stack\App\StackInstalledBuild.marker')) or
     FileExists(ExpandConstant('{app}\Stack\App\StackLocalTestBuild.marker')) then
    Result := 'This folder belongs to full Stack. Choose a separate Stack Burst installation folder so each product can update and uninstall safely.';
#else
  if FileExists(ExpandConstant('{app}\Stack\App\StackBurstInstalledBuild.marker')) or
     FileExists(ExpandConstant('{app}\Stack\App\StackBurstLocalTestBuild.marker')) then
    Result := 'This folder belongs to Stack Burst. Choose a separate full Stack installation folder so each product can update and uninstall safely.';
#endif
#if StackLocalTest == "1"
  if FileExists(ExpandConstant('{app}\Stack\App\{#StackMarkerBase}InstalledBuild.marker')) then
    Result := 'This folder belongs to the regular product. Choose a separate local-test installation folder.';
#else
  if FileExists(ExpandConstant('{app}\Stack\App\{#StackMarkerBase}LocalTestBuild.marker')) then
    Result := 'This folder belongs to a local-test product. Choose a separate regular installation folder.';
#endif
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    UninstallBurstShell();
    RegDeleteKeyIncludingSubkeys(HKLM, '{#StackLegalRegistrySubkey}');
    if RemoveUserData then
    begin
      DelTree(ExpandConstant('{userappdata}\{#StackUserDataDirectoryName}'), True, True, True);
      DelTree(ExpandConstant('{localappdata}\{#StackUserDataDirectoryName}'), True, True, True);
    end;
  end;
end;
