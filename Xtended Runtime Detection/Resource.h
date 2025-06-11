#pragma once
// ─────────────────────────────────────────────
//  Application-weit
// ─────────────────────────────────────────────
#define IDS_APP_TITLE            100

// ─────────────────────────────────────────────
//  Icons
// ─────────────────────────────────────────────
#define IDI_APP_MAIN             101      // großes Tray-/App-Icon
#define IDI_APP_SMALL            102      // 16×16-Variante


// Alias, falls .rc noch IDI_PROJECT1 erwartet
#define IDI_XTENDEDRUNTIMEDETECTION            IDI_APP_MAIN

// ─────────────────────────────────────────────
//  Dialogs
// ─────────────────────────────────────────────
#define IDD_ABOUTBOX             110

// ─────────────────────────────────────────────
//  Standard-Controls
// ─────────────────────────────────────────────
#define IDOK                      1
#define IDC_STATIC               -1

// ─────────────────────────────────────────────
//  Tray-Kontextmenü – Kommandos
// ─────────────────────────────────────────────
#define IDM_TRAY_OPENLOGS        3001
#define IDM_TRAY_DONATE          3002
#define IDM_TRAY_ABOUT           3003
#define IDM_TRAY_EXIT            3004

// ─────────────────────────────────────────────
//  Rückwärts-Kompatibilität / alte Namen
// ─────────────────────────────────────────────
#define IDM_ABOUT                IDM_TRAY_ABOUT
#define IDM_EXIT                 IDM_TRAY_EXIT
#define ID_OPENLOGS              IDM_TRAY_OPENLOGS
#define ID_DONATE                IDM_TRAY_DONATE
#define ID_SHOW_ABOUT            IDM_TRAY_ABOUT
#define ID_EXIT_APP              IDM_TRAY_EXIT

// ─────────────────────────────────────────────
//  (falls du in .rc ein Menü-Template verwendest)
// ─────────────────────────────────────────────
#define IDC_XTENDEDRUNTIMEDETECTION             40001
#define IDR_TRAY_MENU            40002

