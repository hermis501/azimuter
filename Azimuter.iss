; ============================================================================
; Azimuter.iss - Inno Setup project file for the Azimuter VST3 installer.
;
; What this builds:
;   A single Setup .exe that installs the Azimuter.vst3 bundle into the
;   system-wide VST3 folder (%CommonProgramFiles%\VST3, requires admin).
;   This is what you hand to end users.
;
; Prerequisite: run build_release.bat first, so the bundle exists at:
;   build\Azimuter_artefacts\Release\VST3\Azimuter.vst3\
;
; How to compile this installer:
;   1. Install Inno Setup 6+ (separate from the plugin build toolchain).
;      The compiler is "ISCC.exe", typically at:
;        C:\Program Files (x86)\Inno Setup 6\ISCC.exe
;   2. From the project root, run:
;        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" Azimuter.iss
;      ...or just double-click this .iss in Explorer and press Ctrl+F9 in
;      the Inno Setup IDE.
;   3. Output:
;        dist\Azimuter-0.1.0-Setup.exe
;
; If ISCC errors with "no files matched pattern" it means build_release.bat
; hasn't been run (or the bundle path moved). Build the Release config first.
; ============================================================================

#define MyAppName       "Azimuter"
#define MyAppVersion    "1.1"
#define MyAppPublisher  "Hermis Kasperavičius"
#define MyAppDescription "Accessible cassette deck azimuth alignment plugin"
#define VST3SourceDir   "build\Azimuter_artefacts\Release\VST3\Azimuter.vst3"

[Setup]
; AppId uniquely identifies this product across installs / upgrades.
; DO NOT change this GUID once the plugin has been published to users —
; upgrades rely on it to recognise the existing install and replace it
; cleanly instead of installing a second copy side-by-side.
AppId={{8E2A1F3C-7B95-4D6E-AF12-7C9D5E1B4A02}}

AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppCopyright=Copyright (C) {#MyAppPublisher}
VersionInfoDescription={#MyAppDescription}
VersionInfoProductName={#MyAppName}
VersionInfoVersion={#MyAppVersion}

; The installer drops the bundle into a fixed VST3 location, so we hide the
; "choose install directory" page — end users don't pick where VST plugins go.
DefaultDirName={commoncf64}\VST3\{#MyAppName}.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes

; Start Menu group name. The group hosts shortcuts to the readme and to
; the uninstaller — there's no plugin .exe to launch.
DefaultGroupName={#MyAppName}

; Put the uninstaller (unins000.exe + unins000.dat) in %ProgramFiles%\Azimuter\
; alongside the readme, NOT inside the .vst3 bundle. The bundle is parsed by
; the VST3 host and should contain only the spec-defined Contents\ layout;
; dropping unrelated .exe files into it is sloppy and can confuse some hosts.
UninstallFilesDir={commonpf}\{#MyAppName}

; System-wide install needs admin elevation.
PrivilegesRequired=admin

; 64-bit only — the plugin DLL is x64. Refuse to install on 32-bit Windows.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

OutputDir=dist
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} {#MyAppVersion}

; The default Inno Setup wizard uses standard Win32 controls and is
; screen-reader compatible out of the box. Do not switch to custom-drawn
; wizard pages — that would break NVDA/Narrator navigation for the primary
; user. See CLAUDE.md accessibility constraints.

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Copy the entire VST3 bundle as a folder under %CommonProgramFiles%\VST3\.
; recursesubdirs + createallsubdirs preserve the Contents\x86_64-win\ +
; Contents\Resources\ layout the VST3 spec requires. ignoreversion forces
; overwrite even if the user has an older Azimuter installed.
Source: "{#VST3SourceDir}\*"; DestDir: "{app}"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

; The readme goes alongside the plugin under %ProgramFiles%\Azimuter\,
; not inside the .vst3 bundle (which has a strict layout the host parses).
; The Start Menu shortcut below points here.
Source: "readme.txt"; DestDir: "{commonpf}\{#MyAppName}"; Flags: ignoreversion
; GPLv3 requires the licence text to travel with the binaries.
Source: "LICENSE"; DestDir: "{commonpf}\{#MyAppName}"; DestName: "LICENSE.txt"; Flags: ignoreversion

[Icons]
; Start Menu entries. "Read me" opens the plain-text manual with whatever
; the user has registered for .txt (Notepad by default — screen-reader
; friendly out of the box). "Uninstall Azimuter" runs the uninstaller
; recorded by Inno Setup, so the user can remove the plugin without
; hunting through Settings → Apps.
Name: "{group}\Read me"; Filename: "{commonpf}\{#MyAppName}\readme.txt"; \
    Comment: "Open the Azimuter user manual"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"; \
    Comment: "Remove Azimuter from this computer"

[UninstallDelete]
; Remove the bundle directory itself on uninstall. The [Files] entry only
; tracks the contents we copied in; this line removes the now-empty folder.
Type: filesandordirs; Name: "{app}"
; The readme's own file is tracked by [Files] and removed automatically,
; but the parent %ProgramFiles%\Azimuter\ folder needs an explicit sweep
; once it's empty.
Type: dirifempty; Name: "{commonpf}\{#MyAppName}"
