{ Included inside [Code]. No DLL self-registration or cross-product file ownership.
  Distinct product CLSIDs point at each installation's private adapter. The single
  visible verb prefers standalone Burst, then full Stack, then isolated test builds.
  Installing/upgrading/uninstalling recomputes that preference from valid providers.
  Windows 11 modern menu identity/sparse-package registration is deliberately separate. }

const
  BurstProvidersKey = 'Software\Darynn Ho\StackBurstShell\Providers';
  BurstVerbKey = 'Software\Classes\*\shell\StackBurst.Denoise';
  BurstOwnedProviderKey = BurstProvidersKey + '\{#StackShellProvider}';
  BurstOwnedClassKey = 'Software\Classes\CLSID\{#StackShellClsid}';

var
  BurstRegistrationLock: THandle;

procedure BurstAssociationChanged(EventId: Integer; Flags: Cardinal; Item1: Integer; Item2: Integer);
  external 'SHChangeNotify@shell32.dll stdcall';

function BurstCreateMutex(Attributes: Integer; InitialOwner: Integer; Name: String): THandle;
  external 'CreateMutexW@kernel32.dll stdcall';
function BurstWaitForSingleObject(Handle: THandle; Milliseconds: Cardinal): Cardinal;
  external 'WaitForSingleObject@kernel32.dll stdcall';
function BurstReleaseMutex(Handle: THandle): Boolean;
  external 'ReleaseMutex@kernel32.dll stdcall';
function BurstCloseHandle(Handle: THandle): Boolean;
  external 'CloseHandle@kernel32.dll stdcall';

function LockBurstRegistration(): THandle;
var
  WaitResult: Cardinal;
begin
  Result := BurstCreateMutex(0, 0, 'Global\StackBurstShell.Registration.v1');
  if Result = 0 then exit;
  WaitResult := BurstWaitForSingleObject(Result, 5000);
  if (WaitResult <> 0) and (WaitResult <> $80) then
  begin
    BurstCloseHandle(Result);
    Result := 0;
  end;
end;

procedure UnlockBurstRegistration(Handle: THandle);
begin
  BurstReleaseMutex(Handle);
  BurstCloseHandle(Handle);
end;

function BeginBurstInstallation(): Boolean;
begin
  BurstRegistrationLock := LockBurstRegistration();
  Result := BurstRegistrationLock <> 0;
  if not Result then
    SuppressibleMsgBox('Another Stack or Stack Burst installer is running. Finish it before starting this installation or uninstall.', mbError, MB_OK, IDOK);
end;

procedure EndBurstInstallation();
begin
  if BurstRegistrationLock <> 0 then
  begin
    UnlockBurstRegistration(BurstRegistrationLock);
    BurstRegistrationLock := 0;
  end;
end;

function IsOwnedBurstProvider(): Boolean;
var
  AppPath: String;
begin
  Result := RegQueryStringValue(HKLM64, BurstOwnedProviderKey, 'AppPath', AppPath) and
    (CompareText(RemoveBackslashUnlessRoot(AppPath), RemoveBackslashUnlessRoot(ExpandConstant('{app}'))) = 0);
end;

procedure RemoveOwnedBurstProvider();
var
  DllPath: String;
begin
  { An older uninstaller must never erase a provider installed at another path. }
  if IsOwnedBurstProvider() then
    RegDeleteKeyIncludingSubkeys(HKLM64, BurstOwnedProviderKey);
  if RegQueryStringValue(HKLM64, BurstOwnedClassKey + '\InprocServer32', '', DllPath) and
     (CompareText(DllPath, ExpandConstant('{app}\Stack\App\Runtime\StackBurstShell.dll')) = 0) then
    RegDeleteKeyIncludingSubkeys(HKLM64, BurstOwnedClassKey);
end;

function ActivateBurstProvider(Provider: String): Boolean;
var
  ProviderKey, AppPath, DllPath, ClassId, RegisteredDll: String;
begin
  Result := False;
  ProviderKey := BurstProvidersKey + '\' + Provider;
  if not RegQueryStringValue(HKLM64, ProviderKey, 'AppPath', AppPath) then exit;
  if not RegQueryStringValue(HKLM64, ProviderKey, 'DllPath', DllPath) then exit;
  if not RegQueryStringValue(HKLM64, ProviderKey, 'Clsid', ClassId) then exit;
  if not FileExists(AddBackslash(AppPath) + 'StackBurst.exe') then exit;
  if not FileExists(DllPath) then exit;
  if not RegQueryStringValue(HKLM64, 'Software\Classes\CLSID\' + ClassId + '\InprocServer32', '', RegisteredDll) then exit;
  if CompareText(DllPath, RegisteredDll) <> 0 then exit;

  { Extension properties only. Explorer receives one complete IShellItemArray,
    including mixed supported extensions; no command-line %1/per-file spawning. }
  Result := RegWriteStringValue(HKLM64, BurstVerbKey, 'MUIVerb', 'Burst denoise with Stack') and
    RegWriteStringValue(HKLM64, BurstVerbKey, 'ExplorerCommandHandler', ClassId) and
    RegWriteStringValue(HKLM64, BurstVerbKey, 'MultiSelectModel', 'Player') and
    RegWriteStringValue(HKLM64, BurstVerbKey, 'NeverDefault', '') and
    RegWriteStringValue(HKLM64, BurstVerbKey, 'Icon', '"' + DllPath + '",-1') and
    RegWriteStringValue(HKLM64, BurstVerbKey, 'AppliesTo',
      'System.FileExtension:=.arw OR System.FileExtension:=.srf OR System.FileExtension:=.sr2 OR System.FileExtension:=.raw OR System.FileExtension:=.dng') and
    RegWriteStringValue(HKLM64, BurstVerbKey, 'StackProvider', Provider);
end;

procedure RefreshBurstVerb();
var
  Active: Boolean;
begin
  Active := ActivateBurstProvider('Burst');
  if not Active then Active := ActivateBurstProvider('Stack');
  if not Active then Active := ActivateBurstProvider('BurstLocalTest');
  if not Active then Active := ActivateBurstProvider('StackLocalTest');
  if not Active then RegDeleteKeyIncludingSubkeys(HKLM64, BurstVerbKey);
  BurstAssociationChanged($08000000, 0, 0, 0); { SHCNE_ASSOCCHANGED, SHCNF_IDLIST }
end;

procedure InstallBurstShell();
var
  Registered: Boolean;
  DllPath: String;
begin
  { An x64 in-process DLL cannot be loaded by native ARM64 Explorer. }
  if IsX64OS then
  begin
    DllPath := ExpandConstant('{app}\Stack\App\Runtime\StackBurstShell.dll');
    Registered := RegWriteStringValue(HKLM64, BurstOwnedClassKey, '', '{#StackAppName} Burst Explorer command') and
      RegWriteStringValue(HKLM64, BurstOwnedClassKey + '\InprocServer32', '', DllPath) and
      RegWriteStringValue(HKLM64, BurstOwnedClassKey + '\InprocServer32', 'ThreadingModel', 'Apartment') and
      RegWriteStringValue(HKLM64, BurstOwnedProviderKey, 'AppPath', ExpandConstant('{app}')) and
      RegWriteStringValue(HKLM64, BurstOwnedProviderKey, 'DllPath', DllPath) and
      RegWriteStringValue(HKLM64, BurstOwnedProviderKey, 'Clsid', '{#StackShellClsid}');
    if not Registered then
    begin
      RemoveOwnedBurstProvider();
      Log('Could not register the Stack Burst Explorer command.');
      SuppressibleMsgBox('Stack Burst was installed, but the File Explorer command could not be registered. Run the installer again to repair it.', mbError, MB_OK, IDOK);
    end;
  end
  else
    RemoveOwnedBurstProvider();
  RefreshBurstVerb();
end;

procedure UninstallBurstShell();
begin
  RemoveOwnedBurstProvider();
  RefreshBurstVerb();
end;
