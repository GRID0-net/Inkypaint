; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright 2026 the inkypaint contributors
;
; The Windows installer (scripts/package-windows.sh runs makensis with VERSION, SOURCE and OUTFILE). It installs for
; the user who runs it, in %LOCALAPPDATA%\Programs\inkypaint, without administrator rights: the launcher, the runtime and
; their DLLs. The game itself is prepared later by the launcher, from the player's own files, into
; %LOCALAPPDATA%\inkypaint\data, which uninstalling leaves alone.
Unicode true
!include "MUI2.nsh"

Name "inkypaint"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\inkypaint"
InstallDirRegKey HKCU "Software\inkypaint" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "inkypaint ${VERSION}"

!define MUI_ICON "${SOURCE}\inkypaint.ico"
!define MUI_UNICON "${SOURCE}\inkypaint.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\inkypaint.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Start inkypaint"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SOURCE}\licenses\inkypaint-LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\inkypaint"

Section "inkypaint" Main
    SectionIn RO
    SetOutPath "$INSTDIR"
    ; a newer version replaces the old files: what the old one had and this one hasn't goes first
    RMDir /r "$INSTDIR\plugins"
    RMDir /r "$INSTDIR\qml"
    File /r "${SOURCE}\*.*"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    WriteRegStr HKCU "Software\inkypaint" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "inkypaint"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "the inkypaint contributors"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\inkypaint.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    CreateShortCut "$SMPROGRAMS\inkypaint.lnk" "$INSTDIR\inkypaint.exe"
    CreateShortCut "$DESKTOP\inkypaint.lnk" "$INSTDIR\inkypaint.exe"
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\inkypaint.lnk"
    Delete "$DESKTOP\inkypaint.lnk"
    RMDir /r "$INSTDIR"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    DeleteRegKey HKCU "Software\inkypaint"
    ; the prepared game, its saves and settings stay in %LOCALAPPDATA%\inkypaint: they are the player's
SectionEnd
