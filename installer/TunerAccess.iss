[Setup]
AppName=TunerAccess
AppVersion=1.00
AppPublisher=ReaperAccessible
AppPublisherURL=https://reaccessible.net
LicenseFile=data\EULA.txt
DefaultDirName={autopf}\TunerAccess
DefaultGroupName=TunerAccess
OutputDir=output
OutputBaseFilename=ReaperAccessible_TunerAccess_1.00_Setup
Compression=lzma2/ultra64
SolidCompression=yes
DiskSpanning=no
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
UninstallDisplayIcon={app}\TunerAccess.exe
PrivilegesRequired=admin
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[CustomMessages]
; English
english.FullInstall=Full installation (Standalone + VST3)
english.VST3Only=VST3 plugin only (no standalone)
english.CustomInstall=Custom installation
english.CompStandalone=TunerAccess Standalone
english.CompVST3=TunerAccess VST3 Plugin
english.TaskDesktop=Create a desktop shortcut
english.TaskDesktopGroup=Additional shortcuts:
english.VST3PageTitle=Select VST3 Plugin Directory
english.VST3PageDesc=Where should the VST3 plugin be installed?
english.VST3PageSub=The VST3 plugin will be installed in the following folder.
; French
french.FullInstall=Installation complète (Standalone + VST3)
french.VST3Only=Plugin VST3 uniquement (sans standalone)
french.CustomInstall=Installation personnalisée
french.CompStandalone=TunerAccess Standalone
french.CompVST3=Plugin VST3 TunerAccess
french.TaskDesktop=Créer un raccourci sur le bureau
french.TaskDesktopGroup=Raccourcis supplémentaires :
french.VST3PageTitle=Sélection du dossier VST3
french.VST3PageDesc=Où souhaitez-vous installer le plugin VST3 ?
french.VST3PageSub=Le plugin VST3 sera installé dans le dossier suivant.

[Types]
Name: "full"; Description: "{cm:FullInstall}"
Name: "vst3only"; Description: "{cm:VST3Only}"
Name: "custom"; Description: "{cm:CustomInstall}"; Flags: iscustom

[Components]
Name: "standalone"; Description: "{cm:CompStandalone}"; Types: full custom
Name: "vst3"; Description: "{cm:CompVST3}"; Types: full vst3only custom

[Dirs]
Name: "{userappdata}\TunerAccess"

[Files]
; Standalone
Source: "..\build\TunerAccess_artefacts\Release\Standalone\TunerAccess.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

; VST3 Plugin (entire .vst3 bundle)
Source: "..\build\TunerAccess_artefacts\Release\VST3\TunerAccess.vst3\*"; DestDir: "{code:GetVST3Dir}\TunerAccess.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

; NVDA Controller Client DLL — always installed (required for screen reader accessibility)
Source: "..\nvda_2025.3.3_controllerClient\x64\nvdaControllerClient.dll"; DestDir: "{userappdata}\TunerAccess"; Flags: ignoreversion

; User manual + changelog — installed to AppData (found by F1) and to the app folder.
Source: "..\Docs\TunerAccess_Manual.html"; DestDir: "{userappdata}\TunerAccess"; Flags: ignoreversion
Source: "..\Docs\TunerAccess_Manual.html"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Docs\CHANGELOG.html";           DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\TunerAccess"; Filename: "{app}\TunerAccess.exe"; Components: standalone
Name: "{group}\TunerAccess User Manual"; Filename: "{app}\TunerAccess_Manual.html"
Name: "{group}\Uninstall TunerAccess"; Filename: "{uninstallexe}"
Name: "{userdesktop}\TunerAccess"; Filename: "{app}\TunerAccess.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:TaskDesktop}"; GroupDescription: "{cm:TaskDesktopGroup}"; Components: standalone

[UninstallDelete]
; Remove the NVDA DLL, the manual, and the AppData folder itself when empty
Type: files; Name: "{userappdata}\TunerAccess\nvdaControllerClient.dll"
Type: files; Name: "{userappdata}\TunerAccess\TunerAccess_Manual.html"
Type: dirifempty; Name: "{userappdata}\TunerAccess"

[Code]
var
  VST3DirPage: TInputDirWizardPage;

procedure InitializeWizard;
begin
  VST3DirPage := CreateInputDirPage(wpSelectDir,
    CustomMessage('VST3PageTitle'),
    CustomMessage('VST3PageDesc'),
    CustomMessage('VST3PageSub'),
    False, 'New Folder');
  VST3DirPage.Add('');
  VST3DirPage.Values[0] := ExpandConstant('{commoncf}\VST3');
end;

function GetVST3Dir(Param: String): String;
begin
  Result := VST3DirPage.Values[0];
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if (PageID = VST3DirPage.ID) and not WizardIsComponentSelected('vst3') then
    Result := True;
end;
