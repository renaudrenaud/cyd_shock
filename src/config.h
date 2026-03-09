#pragma once

// =============================================================================
//  Comportement (modifiable sans toucher au matériel)
// =============================================================================

#define DISPLAY_BRIGHTNESS  200   // 0-255

#define LONG_PRESS_MS       800   // ms → ouvre menu / change écran
#define TOUCH_DEBOUNCE_MS   400   // ms minimum entre deux taps courts
#define TOUCH_GLITCH_MS     150   // ms tolérance parasites XPT2046

#define CLOCK_UPDATE_MS    1000   // ms entre rafraîchissements de l'horloge

#define BLE_SCAN_SECS         8   // durée d'un passage de scan BLE
#define BLE_CONNECT_TIMEOUT 8000  // ms pour établir une connexion BLE
#define BLE_NOTIFY_TIMEOUT  5000  // ms pour recevoir une notification attendue

#define LOG_MAX_ENTRIES      10   // entrées max dans l'historique (données + flash)

#define NTP_SERVER "pool.ntp.org"

// Portail de configuration AP
#define PORTAL_SSID "CYD-Shock-Config"

// =============================================================================
//  Broches CYD (ESP32-2432S028) — normalement pas besoin de modifier
// =============================================================================

// Écran ILI9341 sur HSPI
#define TFT_SCLK  14
#define TFT_MOSI  13
#define TFT_MISO  12
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1
#define TFT_BL    21   // rétroéclairage (PWM)

// Tactile XPT2046 sur VSPI
#define TOUCH_CLK  25
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CS   33
#define TOUCH_IRQ  36
#define TOUCH_ROTATION 0

// =============================================================================
//  Layout écran (paysage 320×240)
// =============================================================================

#define SCREEN_W    320
#define SCREEN_H    240
#define SIDEBAR_W    40    // colonne GAUCHE avec les boutons de navigation
#define SIDEBAR_X     0    // x de départ de la sidebar
#define CONTENT_X   SIDEBAR_W   // x de départ du contenu (40)
#define CONTENT_W   (SCREEN_W - SIDEBAR_W)  // 280 px pour le contenu

// Hauteurs des zones du contenu (y depuis 0)
#define HDR_H        26    // en-tête
#define TZ_BLOCK_H   80    // bloc fuseau horaire (label + heure)
#define TZ1_Y        HDR_H                    // 26
#define TZ2_Y        (TZ1_Y + TZ_BLOCK_H)    // 106
#define LOG_HDR_Y    (TZ2_Y + TZ_BLOCK_H)    // 186
#define LOG_HDR_H    14
#define LOG_ENTRY_Y  (LOG_HDR_Y + LOG_HDR_H) // 200
#define LOG_ENTRY_H  13    // hauteur d'une entrée
#define LOG_VISIBLE   3    // entrées visibles sur l'écran principal
