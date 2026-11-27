; MoreXournal NSIS installation script for Windows
; Based on the Xournal++ NSIS installation script by The Xournal++ Team

;--------------------------------
; NSIS setup

Unicode true

;--------------------------------
; Includes

!include "MUI2.nsh"
!include x64.nsh
!include "${SCRIPT_DIR}\FileAssociation.nsh"
!include nsDialogs.nsh

; Options for MultiUser plugin
!define MULTIUSER_INSTALLMODE_INSTDIR "MoreXournal"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "Software\MoreXournal"

!define MULTIUSER_EXECUTIONLEVEL Highest ; Mixed-mode installer that can both be per-machine or per-user
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_USE_PROGRAMFILES64
!include MultiUser.nsh


;--------------------------------
; Initialization

Function .onInit
	${If} ${RunningX64}
		# 64-bit Windows (both x86_64 and ARM64 via WOW64)
		SetRegView 64
	${Else}
		# 32-bit Windows
		MessageBox MB_OK "MoreXournal requires 64-bit Windows. Sorry!"
		Abort
	${EndIf}

	!insertmacro MULTIUSER_INIT
FunctionEnd

Function un.onInit
	${If} ${RunningX64}
		# 64-bit Windows (both x86_64 and ARM64 via WOW64)
		SetRegView 64
	${Else}
		# 32-bit Windows
		MessageBox MB_OK "MoreXournal requires 64-bit Windows. Sorry!"
		Abort
	${EndIf}

	!insertmacro MULTIUSER_UNINIT
FunctionEnd

; Name and file
Name "MoreXournal ${MOREXOURNAL_VERSION}"
OutFile "${OUTPUT_INSTALLER_FILE}"

;--------------------------------
; Global Variables

Var StartMenuFolder

;--------------------------------
; Interface Settings

!define MUI_ABORTWARNING

;--------------------------------
; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SCRIPT_DIR}\..\LICENSE"
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE OnDirectoryLeave
!insertmacro MUI_PAGE_DIRECTORY

;Start Menu Folder Page Configuration
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "SHCTX"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\MoreXournal"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "StartMenuEntry"
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "MoreXournal"

!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder

!insertmacro MUI_PAGE_INSTFILES

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
; Languages

!insertmacro MUI_LANGUAGE "English"

;-------------------------------
; Uninstall previous MoreXournal version (never touches upstream Xournal++)

Section "" SecUninstallPrevious
	ReadRegStr $R0 SHCTX "Software\MoreXournal" ""
	${If} $R0 != ""
		DetailPrint "Removing previous version located at $R0"
		; _?= runs the uninstaller in place and waits for it. Without it the
		; uninstaller copies itself to %TEMP% and returns at once, racing the install.
		ExecWait '"$R0\Uninstall.exe" /S _?=$R0'
		; In-place uninstall leaves Uninstall.exe behind
		Delete "$R0\Uninstall.exe"
		RMDir "$R0"
	${EndIf}
SectionEnd

;-------------------------------
; File association macros
; see https://docs.microsoft.com/en-us/windows/win32/shell/fa-file-types#registering-a-file-type

!macro SetDefaultExt EXT PROGID
	WriteRegStr SHCTX "Software\Classes\${EXT}" "" "${PROGID}"
!macroend

!macro RegisterExt EXT PROGID
	WriteRegStr SHCTX "Software\Classes\${EXT}\OpenWithProgIds" "${PROGID}" ""
	WriteRegStr SHCTX "Software\Classes\Applications\morexournal.exe\SupportedTypes" "${EXT}" ""
!macroend

!macro UnregisterExt EXT PROGID
	Push $R0
	ReadRegStr $R0 SHCTX "Software\Classes\${EXT}" ""
	StrCmp $R0 "${PROGID}" 0 +2
		DeleteRegValue SHCTX "Software\Classes\${EXT}" ""
	DeleteRegValue SHCTX "Software\Classes\${EXT}\OpenWithProgIds" "${PROGID}"
	Pop $R0
!macroend

!macro AddProgId PROGID CMD DESC
	; Define ProgId. See https://docs.microsoft.com/en-us/windows/win32/shell/fa-progids
	WriteRegStr SHCTX "Software\Classes\${PROGID}" "" "${DESC}"
	WriteRegStr SHCTX "Software\Classes\${PROGID}\DefaultIcon" "" '"${CMD}",0'
	WriteRegStr SHCTX "Software\Classes\${PROGID}\shell" "" "open"
	WriteRegStr SHCTX "Software\Classes\${PROGID}\shell\open\command" "" '"${CMD}" "%1"'
	WriteRegStr SHCTX "Software\Classes\${PROGID}\shell\edit" "" "Edit with MoreXournal"
	WriteRegStr SHCTX "Software\Classes\${PROGID}\shell\edit\command" "" '"${CMD}" "%1"'
!macroend

!macro DeleteProgId PROGID
	; See https://docs.microsoft.com/en-us/windows/win32/shell/fa-file-types#deleting-registry-information-during-uninstallation
	DeleteRegKey SHCTX "Software\Classes\${PROGID}"
!macroend

; Only SHCNE_ASSOCCHANGED with null items is sent. The old per-shortcut
; SHCNE_CREATE/SHCNE_DELETE calls wrote "i 0x1000 | 0x1": the System plugin
; keeps only the last number, so the shell got SHCNF_PATHA (0x1) with a UTF-16
; path and crashed in windows.storage.dll. Explorer already watches the Start
; Menu folders, so those calls were not needed.
!define SHCNE_ASSOCCHANGED 0x08000000
!define SHCNF_IDLIST 0x0

;-------------------------------
; Installer Sections

Section "Associate .xopp files with MoreXournal" SecFileXopp
	!insertmacro SetDefaultExt ".xopp" "MoreXournal.File"
SectionEnd

Section "Associate .xopt files with MoreXournal" SecFileXopt
	!insertmacro SetDefaultExt ".xopt" "MoreXournal.Template"
SectionEnd

Section "Associate .xoj files with MoreXournal" SecFileXoj
	!insertmacro SetDefaultExt ".xoj" "MoreXournal.Xournal"
SectionEnd

Function OnDirectoryLeave
	${IfNot} ${FileExists} "$INSTDIR\*.*"
		Return
	${EndIf}

	; Initialize Flag: 1 = Empty, 0 = Not Empty
	StrCpy $1 1

	FindFirst $2 $3 "$INSTDIR\*.*"

	${If} $2 == ""
		Return
	${EndIf}

	${Do}
		${If} $3 != "."
		${AndIf} $3 != ".."
			StrCpy $1 0
			${Break}
		${EndIf}
		ClearErrors
		FindNext $2 $3

		${If} ${Errors}
			${Break}
		${EndIf}
	${Loop}
	FindClose $2

	; Check the result
	${If} $1 == 0
		MessageBox MB_ICONEXCLAMATION|MB_YESNO|MB_DEFBUTTON2 \
		"The folder $INSTDIR is not empty.$\r$\nDo you want to continue using this folder?" \
		IDYES ignore

		Abort

		ignore:
	${EndIf}
FunctionEnd

Function .onVerifyInstDir
	; Get last component of path
	${GetFileName} $INSTDIR $0

	${If} $0 != "MoreXournal"
		StrCpy $INSTDIR "$INSTDIR\MoreXournal"
	${EndIf}
FunctionEnd

Section "MoreXournal" SecMoreXournal
	; Required
	SectionIn RO

	SetOutPath "$INSTDIR"

	; Files to put into the setup
	File /r ${SETUP_DIR}\*

	; Set install information
	WriteRegStr SHCTX "Software\MoreXournal" "" "$INSTDIR"

	; Set program information
	WriteRegStr SHCTX "Software\Classes\Applications\morexournal.exe" "" '"$INSTDIR\bin\morexournal-wrapper.exe"'
	WriteRegStr SHCTX "Software\Classes\Applications\morexournal.exe" "FriendlyAppName" "MoreXournal"
	WriteRegExpandStr SHCTX "Software\Classes\Applications\morexournal.exe" "DefaultIcon" '"$INSTDIR\bin\morexournal-wrapper.exe",0'
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\App Paths\morexournal.exe" "" '"$INSTDIR\bin\morexournal-wrapper.exe"'

	; Add file type information
	!insertmacro RegisterExt ".xopp" "MoreXournal.File"
	!insertmacro RegisterExt ".xopt" "MoreXournal.Template"
	!insertmacro RegisterExt ".xoj" "MoreXournal.Xournal"
	!insertmacro RegisterExt ".pdf" "MoreXournal.AnnotatePdf"
	push $R0
	StrCpy $R0 "$INSTDIR\bin\morexournal-wrapper.exe"
	!insertmacro AddProgId "MoreXournal.File" "$R0" "MoreXournal file"
	!insertmacro AddProgId "MoreXournal.Template" "$R0" "MoreXournal template file"
	!insertmacro AddProgId "MoreXournal.Xournal" "$R0" "Xournal file"
	!insertmacro AddProgId "MoreXournal.AnnotatePdf" "$R0" "PDF file"
	pop $R0

	; Create uninstaller
	WriteUninstaller "$INSTDIR\Uninstall.exe"
	; Add uninstall entry. See https://docs.microsoft.com/en-us/windows/win32/msi/uninstall-registry-key
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoreXournal" "DisplayIcon" '"$INSTDIR\bin\morexournal-wrapper.exe"'
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoreXournal" "DisplayName" "MoreXournal"
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoreXournal" "DisplayVersion" "${MOREXOURNAL_VERSION}"
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoreXournal" "Publisher" "Xournal++ developers and MoreXournal contributors"
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoreXournal" "URLInfoAbout" "https://github.com/AntoPatanto/MoreXournal"
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoreXournal" "InstallLocation" '"$INSTDIR"'
	WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoreXournal" "UninstallString" '"$INSTDIR\Uninstall.exe"'
	WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoreXournal" "NoModify" 1
	WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoreXournal" "NoRepair" 1

	!insertmacro MUI_STARTMENU_WRITE_BEGIN Application
		;Create shortcuts
		CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
		CreateShortcut "$SMPROGRAMS\$StartMenuFolder\MoreXournal.lnk" '"$INSTDIR\bin\morexournal-wrapper.exe"'
		CreateShortcut "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk" '"$INSTDIR\Uninstall.exe"'
	!insertmacro MUI_STARTMENU_WRITE_END

	!insertmacro RefreshShellIcons
SectionEnd

;--------------------------------
; Descriptions

; Language strings
LangString DESC_SecMoreXournal ${LANG_ENGLISH} "MoreXournal executable"
LangString DESC_SecFileXopp ${LANG_ENGLISH} "Open .xopp files with MoreXournal"
LangString DESC_SecFileXopt ${LANG_ENGLISH} "Open .xopt files with MoreXournal"
LangString DESC_SecFileXoj ${LANG_ENGLISH} "Open .xoj files with MoreXournal"

; Assign language strings to sections
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
	!insertmacro MUI_DESCRIPTION_TEXT ${SecMoreXournal} $(DESC_SecMoreXournal)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecFileXopp} $(DESC_SecFileXopp)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecFileXopt} $(DESC_SecFileXopt)
	!insertmacro MUI_DESCRIPTION_TEXT ${SecFileXoj} $(DESC_SecFileXoj)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
; Uninstaller

Section "Uninstall"

	SetRegView 64

	; FIXME: ask if the user wants to uninstall the user or system wide install
	ReadRegStr $0 HKCU "Software\MoreXournal" ""
	${IF} $0 == ""
		SetShellVarContext all
	${ELSE}
		SetShellVarContext current
	${ENDIF}

	; Clean up start menu before deleting the registry key that stores the folder name
	!insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder
	Delete "$SMPROGRAMS\$StartMenuFolder\MoreXournal.lnk"
	Delete "$SMPROGRAMS\$StartMenuFolder\Uninstall.lnk"
	RMDir "$SMPROGRAMS\$StartMenuFolder"

	; Remove registry keys
	DeleteRegKey SHCTX "Software\MoreXournal"
	DeleteRegKey SHCTX "Software\Classes\Applications\morexournal.exe"
	DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\App Paths\morexournal.exe"
	DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoreXournal"

	!insertmacro UnregisterExt ".xopp" "MoreXournal.File"
	!insertmacro UnregisterExt ".xopt" "MoreXournal.Template"
	!insertmacro UnregisterExt ".xoj" "MoreXournal.Xournal"
	!insertmacro UnregisterExt ".pdf" "MoreXournal.AnnotatePdf"

	!insertmacro DeleteProgId "MoreXournal.File"
	!insertmacro DeleteProgId "MoreXournal.Template"
	!insertmacro DeleteProgId "MoreXournal.Xournal"
	!insertmacro DeleteProgId "MoreXournal.AnnotatePdf"

	; Remove files
	RMDir /r "$INSTDIR\bin"
	RMDir /r "$INSTDIR\lib"
	RMDir /r "$INSTDIR\share"
	RMDir /r "$INSTDIR\ui"
	Delete "$INSTDIR\Uninstall.exe"
	RMDir "$INSTDIR"

	System::Call 'shell32::SHChangeNotify(i ${SHCNE_ASSOCCHANGED}, i ${SHCNF_IDLIST}, i 0, i 0)'
SectionEnd
