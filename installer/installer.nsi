!define APP_NAME "Takefuji Groove Vault"
!define APP_VERSION "1.1.2"
!define EXE_NAME "TakefujiGrooveVault.exe"
!define INSTALL_DIR "$PROGRAMFILES64\TakefujiGrooveVault"
!define UNINSTALLER_NAME "Uninstall.exe"
!define PUBLISHER "Takefuji Drums"

Name "${APP_NAME} v${APP_VERSION}"
OutFile "TakefujiGrooveVault_Setup_v${APP_VERSION}.exe"
InstallDir "${INSTALL_DIR}"
InstallDirRegKey HKLM "Software\TakefujiGrooveVault" "InstallDir"
RequestExecutionLevel admin
SetCompressor lzma

!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"

; デスクトップショートカット作成オプション
Var AddDesktopShortcut

!define MUI_WELCOMEPAGE_TITLE "Takefuji Groove Vault v${APP_VERSION} セットアップ"
!define MUI_WELCOMEPAGE_TEXT "インストールウィザードへようこそ。$\r$\nインストール先を確認・変更できます。"
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE_NAME}"
!define MUI_FINISHPAGE_RUN_TEXT "Takefuji Groove Vault を今すぐ起動する"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "インストールフォルダを開く"
!define MUI_FINISHPAGE_SHOWREADME_NOTCHECKED

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
Page custom DesktopShortcutPage DesktopShortcutLeave
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "Japanese"

; -----------------------------------------------------------------------
; DesktopShortcutPage — サイレント時はページをスキップ、
;   ショートカット作成なし（自動アップデート経路で余計な変更を防ぐ）
; -----------------------------------------------------------------------
Function DesktopShortcutPage
  IfSilent silent_shortcut_page
  nsDialogs::Create 1018
  Pop $0

  ${NSD_CreateLabel} 0 0 100% 20u "オプションを選択してください："
  Pop $0

  ${NSD_CreateCheckbox} 0 30u 100% 12u "デスクトップにショートカットを作成する"
  Pop $AddDesktopShortcut
  ${NSD_SetState} $AddDesktopShortcut ${BST_CHECKED}

  nsDialogs::Show
  Return

  silent_shortcut_page:
    StrCpy $AddDesktopShortcut ${BST_UNCHECKED}
    Abort
FunctionEnd

Function DesktopShortcutLeave
  IfSilent leave_silent
  ${NSD_GetState} $AddDesktopShortcut $AddDesktopShortcut
  Return
  leave_silent:
FunctionEnd

; -----------------------------------------------------------------------
; Section MainSection
; exeを上書きする前に旧exeをリネームして退避することで、
; TGV起動中でも新exeを書き込めるようにする（レースコンディション修正）。
; Rename失敗が続く場合（最大10秒）はサイレント時はベストエフォート継続、
; 通常インストール時はユーザーに終了を促す。
; -----------------------------------------------------------------------
Section "MainSection" SEC01
  SetOutPath "$INSTDIR"

  ; 既存 exe がなければ新規インストール → 待機不要
  IfFileExists "$INSTDIR\${EXE_NAME}" 0 do_install

  StrCpy $R0 0   ; ループカウンタ

  lockwait_loop:
    Rename "$INSTDIR\${EXE_NAME}" "$INSTDIR\${EXE_NAME}.old"
    IfErrors 0 lockwait_done   ; Rename 成功 → 退避完了

    ; Rename 失敗 → まだロック中
    IntOp $R0 $R0 + 1
    IntCmp $R0 20 lockwait_timeout 0 lockwait_timeout   ; $R0>=20 → タイムアウト
    Sleep 500
    Goto lockwait_loop

  lockwait_timeout:
    IfSilent do_install   ; サイレント時はベストエフォートで続行
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION \
      "Takefuji Groove Vault が起動中です。$\r$\nアプリを終了してから「再試行」を押してください。" \
      IDRETRY lockwait_retry
    ; キャンセル → 中断
    Quit

  lockwait_retry:
    StrCpy $R0 0
    Goto lockwait_loop

  lockwait_done:
    Delete "$INSTDIR\${EXE_NAME}.old"
    ClearErrors

  do_install:
  File "..\Builds\VisualStudio2026\x64\Release\Standalone Plugin\${EXE_NAME}"
  File /r "..\Resources"

  WriteRegStr HKLM "Software\TakefujiGrooveVault" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TakefujiGrooveVault" \
    "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TakefujiGrooveVault" \
    "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TakefujiGrooveVault" \
    "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TakefujiGrooveVault" \
    "UninstallString" "$INSTDIR\${UNINSTALLER_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TakefujiGrooveVault" \
    "DisplayIcon" "$INSTDIR\${EXE_NAME}"

  CreateDirectory "$SMPROGRAMS\${APP_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${EXE_NAME}"
  CreateShortcut "$SMPROGRAMS\${APP_NAME}\アンインストール.lnk" "$INSTDIR\${UNINSTALLER_NAME}"

  ${If} $AddDesktopShortcut == ${BST_CHECKED}
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${EXE_NAME}"
  ${EndIf}

  WriteUninstaller "$INSTDIR\${UNINSTALLER_NAME}"
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\${EXE_NAME}"
  Delete "$INSTDIR\${EXE_NAME}.old"
  Delete "$INSTDIR\${UNINSTALLER_NAME}"
  RMDir /r "$INSTDIR\Resources"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
  Delete "$SMPROGRAMS\${APP_NAME}\アンインストール.lnk"
  RMDir "$SMPROGRAMS\${APP_NAME}"
  Delete "$DESKTOP\${APP_NAME}.lnk"

  DeleteRegKey HKLM "Software\TakefujiGrooveVault"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\TakefujiGrooveVault"
SectionEnd
