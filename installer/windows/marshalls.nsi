; Marshalls Studio — Windows installer (NSIS 3)
; Built by scripts\windows\make-installer.ps1:
;   makensis /DVERSION=x.y.z /DSRCDIR=<dist\MarshallsStudio> /DOUTFILE=<setup.exe> marshalls.nsi

Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!define APPNAME "Marshalls Studio"
!define COMPANY "Marshalls"
!define EXE "MarshallsStudio.exe"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\MarshallsStudio"

Name "${APPNAME} ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\${COMPANY}\${APPNAME}" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
BrandingText "${APPNAME} ${VERSION}"

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\64bit\${EXE}"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "PortugueseBR"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "O ${APPNAME} requer Windows 64 bits."
    Abort
  ${EndIf}
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_ICONSTOP "O ${APPNAME} requer Windows 10 ou superior."
    Abort
  ${EndIf}
  SetRegView 64
FunctionEnd

Section "Aplicativo" SecMain
  SectionIn RO
  ; Never leave files from an older version mixed with the new ones.
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\obs-plugins"
  RMDir /r "$INSTDIR\data"
  SetOutPath "$INSTDIR"
  File /r "${SRCDIR}\*.*"

  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\bin\64bit\${EXE}" "" "$INSTDIR\bin\64bit\${EXE}" 0
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Desinstalar.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\bin\64bit\${EXE}" "" "$INSTDIR\bin\64bit\${EXE}" 0

  WriteRegStr HKLM "Software\${COMPANY}\${APPNAME}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "${COMPANY}"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\bin\64bit\${EXE}"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
  WriteUninstaller "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
  SetRegView 64
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir /r "$SMPROGRAMS\${APPNAME}"
  RMDir /r "$INSTDIR\bin"
  RMDir /r "$INSTDIR\obs-plugins"
  RMDir /r "$INSTDIR\data"
  Delete "$INSTDIR\LICENSE.txt"
  Delete "$INSTDIR\THIRD_PARTY.txt"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "${UNINSTKEY}"
  DeleteRegKey HKLM "Software\${COMPANY}\${APPNAME}"
  ; User settings (%APPDATA%\Marshalls\Marshalls Studio) and recordings are kept.
SectionEnd
