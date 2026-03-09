// =============================================================================
//  CYD Shock — Serveur BLE de synchronisation G-Shock pour ESP32-2432S028
//
//  Écran 320×240 ILI9341 (HSPI) + tactile XPT2046 (VSPI) via LovyanGFX
//
//  Écrans :
//    SCR_MAIN   — double fuseau horaire + historique des synchros (défaut)
//    SCR_LOG    — historique complet (10 entrées)
//    SCR_PORTAL — portail AP Wi-Fi pour reconfiguration
//
//  Barre latérale (40 px, droite) :
//    [HOME]    — retour à SCR_MAIN
//    [LIST]    — afficher SCR_LOG
//    [WIFI]    — activer SCR_PORTAL (mode AP)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <time.h>
#include <ctype.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "version.h"
#include "config.h"
#include "app_config.h"
#include "activity_log.h"
#include "gshock_ble.h"

// =============================================================================
//  LovyanGFX — configuration matérielle CYD (ESP32-2432S028)
// =============================================================================
class LGFX_CYD : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341  _panel;
    lgfx::Bus_SPI        _bus;
    lgfx::Light_PWM      _light;
    lgfx::Touch_XPT2046  _touch;
public:
    LGFX_CYD() {
        { auto cfg = _bus.config();
          cfg.spi_host   = HSPI_HOST; cfg.spi_mode = 0;
          cfg.freq_write = 40000000;  cfg.freq_read = 16000000;
          cfg.spi_3wire  = true;      cfg.use_lock  = true;
          cfg.dma_channel= SPI_DMA_CH_AUTO;
          cfg.pin_sclk   = TFT_SCLK; cfg.pin_mosi = TFT_MOSI;
          cfg.pin_miso   = TFT_MISO; cfg.pin_dc   = TFT_DC;
          _bus.config(cfg); _panel.setBus(&_bus); }

        { auto cfg = _panel.config();
          cfg.pin_cs = TFT_CS; cfg.pin_rst = TFT_RST; cfg.pin_busy = -1;
          cfg.panel_width  = 240; cfg.panel_height = 320;
          cfg.readable     = true; cfg.invert = false;
          cfg.rgb_order    = false; cfg.dlen_16bit = false;
          cfg.bus_shared   = true;
          _panel.config(cfg); }

        { auto cfg = _light.config();
          cfg.pin_bl = TFT_BL; cfg.invert = false;
          cfg.freq   = 44100;  cfg.pwm_channel = 7;
          _light.config(cfg); _panel.setLight(&_light); }

        { auto cfg = _touch.config();
          cfg.x_min = 300; cfg.x_max = 3900;
          cfg.y_min = 300; cfg.y_max = 3900;
          cfg.pin_int  = TOUCH_IRQ; cfg.bus_shared = false;
          cfg.offset_rotation = TOUCH_ROTATION;
          cfg.spi_host = VSPI_HOST; cfg.freq = 1000000;
          cfg.pin_sclk = TOUCH_CLK; cfg.pin_mosi = TOUCH_MOSI;
          cfg.pin_miso = TOUCH_MISO; cfg.pin_cs  = TOUCH_CS;
          _touch.config(cfg); _panel.setTouch(&_touch); }

        setPanel(&_panel);
    }
};

// =============================================================================
//  Palette de couleurs (RGB888)
// =============================================================================
static const uint32_t C_BG        = 0x000000u;
static const uint32_t C_HDR_BG    = 0x001030u;
static const uint32_t C_SIDEBAR   = 0x101820u;
static const uint32_t C_SEP       = 0x203060u;
static const uint32_t C_TZ_LABEL  = 0x80C0FFu;  // bleu clair
static const uint32_t C_TZ_TIME   = 0xFFFFFFu;  // blanc
static const uint32_t C_TZ_DATE   = 0x808080u;  // gris
static const uint32_t C_LOG_HDR   = 0x606060u;
static const uint32_t C_LOG_NAME  = 0xFFD700u;  // or
static const uint32_t C_LOG_TIME  = 0xA0A0A0u;  // gris clair
static const uint32_t C_BTN_ACT   = 0x0055CCu;  // bouton actif
static const uint32_t C_BTN_IDLE  = 0x303030u;  // bouton inactif
static const uint32_t C_OK        = 0x00CC00u;
static const uint32_t C_WARN      = 0xFF8000u;

// =============================================================================
//  États et objets globaux
// =============================================================================
enum Screen { SCR_MAIN, SCR_LOG, SCR_PORTAL };
static Screen currentScreen = SCR_MAIN;

static LGFX_CYD  display;
static AppConfig appCfg;

// Résultat BLE partagé entre la tâche BLE (core 0) et le loop d'affichage (core 1)
static SemaphoreHandle_t g_syncMutex = nullptr;
static SyncResult        g_syncResult;
static volatile bool     g_syncAvailable = false;

// Portail
static WebServer* g_portalServer = nullptr;
static DNSServer  g_dnsServer;

// =============================================================================
//  Conversion fuseau IANA → POSIX (même table que lmscyd)
// =============================================================================
static const char* ianaToposix(const char* iana) {
    static const struct { const char* iana; const char* posix; } TZ[] = {
        { "UTC",                "UTC0"                                      },
        { "Europe/London",      "GMT0BST,M3.5.0/1,M10.5.0"                 },
        { "Europe/Paris",       "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Berlin",      "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Rome",        "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Madrid",      "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Amsterdam",   "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Brussels",    "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Zurich",      "CET-1CEST,M3.5.0,M10.5.0/3"              },
        { "Europe/Helsinki",    "EET-2EEST,M3.5.0/3,M10.5.0/4"            },
        { "Europe/Athens",      "EET-2EEST,M3.5.0/3,M10.5.0/4"            },
        { "Europe/Moscow",      "MSK-3"                                     },
        { "Africa/Cairo",       "EET-2"                                     },
        { "Africa/Johannesburg","SAST-2"                                    },
        { "America/New_York",   "EST5EDT,M3.2.0,M11.1.0"                   },
        { "America/Chicago",    "CST6CDT,M3.2.0,M11.1.0"                   },
        { "America/Denver",     "MST7MDT,M3.2.0,M11.1.0"                   },
        { "America/Phoenix",    "MST7"                                      },
        { "America/Los_Angeles","PST8PDT,M3.2.0,M11.1.0"                   },
        { "America/Anchorage",  "AKST9AKDT,M3.2.0,M11.1.0"                 },
        { "America/Sao_Paulo",  "BRT3BRST,M10.3.0/0,M2.3.0/0"             },
        { "America/Buenos_Aires","ART3"                                     },
        { "America/Mexico_City","CST6CDT,M4.1.0,M10.5.0"                   },
        { "America/Toronto",    "EST5EDT,M3.2.0,M11.1.0"                   },
        { "America/Vancouver",  "PST8PDT,M3.2.0,M11.1.0"                   },
        { "Asia/Dubai",         "GST-4"                                     },
        { "Asia/Karachi",       "PKT-5"                                     },
        { "Asia/Kolkata",       "IST-5:30"                                  },
        { "Asia/Dhaka",         "BST-6"                                     },
        { "Asia/Bangkok",       "ICT-7"                                     },
        { "Asia/Jakarta",       "WIB-7"                                     },
        { "Asia/Shanghai",      "CST-8"                                     },
        { "Asia/Hong_Kong",     "HKT-8"                                     },
        { "Asia/Singapore",     "SGT-8"                                     },
        { "Asia/Taipei",        "CST-8"                                     },
        { "Asia/Tokyo",         "JST-9"                                     },
        { "Asia/Seoul",         "KST-9"                                     },
        { "Australia/Perth",    "AWST-8"                                    },
        { "Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"          },
        { "Australia/Sydney",   "AEST-10AEDT,M10.1.0,M4.1.0/3"            },
        { "Pacific/Auckland",   "NZST-12NZDT,M9.5.0,M4.1.0/3"             },
        { nullptr, nullptr }
    };
    for (int i = 0; TZ[i].iana; i++) {
        if (strcmp(iana, TZ[i].iana) == 0) return TZ[i].posix;
    }
    return "UTC0";
}

// Offset UTC en secondes depuis une chaîne POSIX (heure standard uniquement)
static int32_t posixTzToOffset(const char* posix) {
    const char* p = posix;
    while (*p && isalpha(*p)) p++;   // sauter uniquement les lettres, PAS le '-'
    if (!*p) return 0;
    int sign = (*p == '-') ? -1 : 1;
    if (*p == '-' || *p == '+') p++;
    int hours = atoi(p);
    while (*p && isdigit(*p)) p++;
    int minutes = 0;
    if (*p == ':') { p++; minutes = atoi(p); }
    return -(sign * (hours * 3600 + minutes * 60));
}

// Heure locale dans un fuseau IANA donné (sans modifier TZ global)
static bool getTimeInZone(const char* iana, struct tm& t) {
    time_t now;
    time(&now);
    if (now < 1000000) return false;

    struct tm utc;
    gmtime_r(&now, &utc);
    int32_t offset = posixTzToOffset(ianaToposix(iana));

    time_t local = now + offset;
    gmtime_r(&local, &t);
    return true;
}

// Partie ville du nom IANA : "Europe/Paris" → "Paris"
static String tzCityName(const char* iana) {
    const char* slash = strrchr(iana, '/');
    return String(slash ? slash + 1 : iana);
}

// =============================================================================
//  Dessin des icônes sidebar
// =============================================================================

// Maison (HOME)
static void drawIconHome(int cx, int cy, uint32_t color) {
    display.fillTriangle(cx - 12, cy + 4, cx + 12, cy + 4, cx, cy - 8, color); // toit
    display.fillRect(cx - 9, cy + 4, 18, 12, color);                            // murs
    display.fillRect(cx - 4, cy + 9, 8, 7, C_SIDEBAR);                         // porte
}

// Liste (LOG)
static void drawIconList(int cx, int cy, uint32_t color) {
    display.fillRect(cx - 11, cy - 7, 22, 3, color);
    display.fillRect(cx - 11, cy - 1, 22, 3, color);
    display.fillRect(cx - 11, cy + 5, 22, 3, color);
}

// WiFi arcs (PORTAL)
static void drawIconWifi(int cx, int cy, uint32_t color) {
    display.fillCircle(cx, cy + 10, 3, color);
    // Arcs approximés avec des demi-cercles partiels (LovyanGFX drawArc :
    // angles en degrés depuis 12h, sens horaire)
    display.drawArc(cx, cy + 10,  8,  6, 230, 310, color);
    display.drawArc(cx, cy + 10, 14, 12, 230, 310, color);
}

// =============================================================================
//  Dessin de la barre latérale
// =============================================================================
static void drawSidebar() {
    display.fillRect(SIDEBAR_X, 0, SIDEBAR_W, SCREEN_H, C_SIDEBAR);
    display.drawFastVLine(SIDEBAR_X + SIDEBAR_W, 0, SCREEN_H, C_SEP);

    // 3 boutons de 80px chacun
    static const struct { Screen scr; int yc; } BTNS[] = {
        {SCR_MAIN,   40},
        {SCR_LOG,   120},
        {SCR_PORTAL, 200},
    };
    int cx = SIDEBAR_X + SIDEBAR_W / 2;  // 20

    for (auto& btn : BTNS) {
        uint32_t bg = (currentScreen == btn.scr) ? C_BTN_ACT : C_SIDEBAR;
        display.fillRect(SIDEBAR_X, btn.yc - 38, SIDEBAR_W, 76, bg);
        display.drawFastHLine(SIDEBAR_X, btn.yc + 38, SIDEBAR_W, C_SEP);
    }

    // Icônes
    drawIconHome(cx, BTNS[0].yc,   (currentScreen == SCR_MAIN)   ? C_TZ_TIME : 0x707070u);
    drawIconList(cx, BTNS[1].yc,   (currentScreen == SCR_LOG)    ? C_TZ_TIME : 0x707070u);
    drawIconWifi(cx, BTNS[2].yc,   (currentScreen == SCR_PORTAL) ? C_TZ_TIME : 0x707070u);
}

// =============================================================================
//  En-tête (titre + indicateur WiFi en barres)
// =============================================================================

// 4 barres de hauteur croissante, alignées en bas, couleur selon signal
static void drawWifiIndicator() {
    static const int BAR_W       = 3;
    static const int BAR_GAP     = 2;
    static const int BAR_BOTTOM  = HDR_H - 4;
    static const int BAR_H[]     = {4, 8, 12, 16};
    // x de la barre la plus à gauche (les 4 barres + 3 espaces, marge droite 4px)
    int x0 = SCREEN_W - 4 - 4 * BAR_W - 3 * BAR_GAP;

    bool connected = (WiFi.status() == WL_CONNECTED);
    int activeBars = 0;
    uint32_t activeColor = C_OK;

    if (connected) {
        int rssi = WiFi.RSSI();
        if      (rssi >= -55) { activeBars = 4; activeColor = C_OK;       }
        else if (rssi >= -65) { activeBars = 3; activeColor = C_OK;       }
        else if (rssi >= -75) { activeBars = 2; activeColor = C_WARN;     }
        else                  { activeBars = 1; activeColor = 0xFF2020u;  }
    }

    for (int i = 0; i < 4; i++) {
        int bx  = x0 + i * (BAR_W + BAR_GAP);
        int bh  = BAR_H[i];
        int by  = BAR_BOTTOM - bh;
        uint32_t col = (i < activeBars) ? activeColor : 0x303030u;
        display.fillRect(bx, by, BAR_W, bh, col);
    }
}

static void drawHeader() {
    display.fillRect(CONTENT_X, 0, CONTENT_W, HDR_H, C_HDR_BG);
    display.drawFastHLine(CONTENT_X, HDR_H, CONTENT_W, C_SEP);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TZ_TIME, C_HDR_BG);
    display.setTextDatum(lgfx::middle_left);
    display.drawString(APP_NAME " v" APP_VERSION, CONTENT_X + 6, HDR_H / 2);
    display.setTextDatum(lgfx::top_left);

    drawWifiIndicator();
}

// =============================================================================
//  Bloc fuseau horaire
// =============================================================================
static void drawTzBlock(int y, const char* iana, bool highlight) {
    display.fillRect(CONTENT_X, y, CONTENT_W, TZ_BLOCK_H, C_BG);
    display.drawFastHLine(CONTENT_X, y + TZ_BLOCK_H - 1, CONTENT_W, C_SEP);

    // Nom de la ville
    String city = tzCityName(iana);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TZ_LABEL, C_BG);
    display.setTextDatum(lgfx::top_left);
    display.drawString(city.c_str(), CONTENT_X + 6, y + 4);

    // Heure dans ce fuseau
    struct tm t = {};
    if (getTimeInZone(iana, t)) {
        char timeBuf[16];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", t.tm_hour, t.tm_min);

        display.setFont(&fonts::FreeSans24pt7b);
        display.setTextColor(C_TZ_TIME, C_BG);
        display.setTextDatum(lgfx::top_left);
        display.drawString(timeBuf, CONTENT_X + 6, y + 20);

        // Secondes (plus petites, à droite de l'heure)
        char secBuf[8];
        snprintf(secBuf, sizeof(secBuf), ":%02d", t.tm_sec);
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(C_TZ_DATE, C_BG);
        int timeW = display.textWidth(timeBuf, &fonts::FreeSans24pt7b);
        display.drawString(secBuf, CONTENT_X + 6 + timeW + 2, y + 44);

        // Date (jour/mois)
        static const char* DAY_NAMES[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* MON_NAMES[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                           "Jul","Aug","Sep","Oct","Nov","Dec"};
        char dateBuf[24];
        snprintf(dateBuf, sizeof(dateBuf), "%s %d %s",
                 DAY_NAMES[t.tm_wday], t.tm_mday, MON_NAMES[t.tm_mon]);
        display.setTextColor(C_TZ_DATE, C_BG);
        display.drawString(dateBuf, CONTENT_X + 6, y + TZ_BLOCK_H - 18);
    } else {
        display.setFont(&fonts::FreeSans12pt7b);
        display.setTextColor(C_WARN, C_BG);
        display.setTextDatum(lgfx::middle_left);
        display.drawString("NTP not synced", CONTENT_X + 6, y + TZ_BLOCK_H / 2);
        display.setTextDatum(lgfx::top_left);
    }
}

// =============================================================================
//  Section historique (écran principal : 3 entrées)
// =============================================================================
static void drawLogSection() {
    display.fillRect(CONTENT_X, LOG_HDR_Y, CONTENT_W, SCREEN_H - LOG_HDR_Y, C_BG);
    display.drawFastHLine(CONTENT_X, LOG_HDR_Y, CONTENT_W, C_SEP);

    // Titre
    display.setFont(&fonts::Font0);
    display.setTextColor(C_LOG_HDR, C_BG);
    display.setTextDatum(lgfx::top_left);
    display.drawString("  Last syncs", CONTENT_X + 4, LOG_HDR_Y + 2);

    int n = min(activityLog.count(), LOG_VISIBLE);
    if (n == 0) {
        display.setFont(&fonts::Font2);
        display.setTextColor(0x505050u, C_BG);
        display.drawString("No sync yet", CONTENT_X + 8, LOG_ENTRY_Y + 4);
        return;
    }

    for (int i = 0; i < n; i++) {
        LogEntry e = activityLog.getEntry(i);
        int ey = LOG_ENTRY_Y + i * LOG_ENTRY_H;

        display.setFont(&fonts::Font0);
        display.setTextColor(C_LOG_NAME, C_BG);
        display.setTextDatum(lgfx::top_left);
        display.drawString(e.watchName, CONTENT_X + 8, ey + 1);

        display.setTextColor(C_LOG_TIME, C_BG);
        display.setTextDatum(lgfx::top_right);
        display.drawString(e.syncTime, SCREEN_W - 6, ey + 1);
        display.setTextDatum(lgfx::top_left);
    }
}

// =============================================================================
//  Écran principal
// =============================================================================
static void drawMainScreen() {
    display.fillRect(CONTENT_X, 0, CONTENT_W, SCREEN_H, C_BG);
    drawHeader();
    drawTzBlock(TZ1_Y, appCfg.timezone, true);

    bool hasTz2 = (appCfg.timezone2[0] != '\0');
    if (hasTz2) {
        drawTzBlock(TZ2_Y, appCfg.timezone2, false);
    } else {
        display.fillRect(CONTENT_X, TZ2_Y, CONTENT_W, TZ_BLOCK_H, C_BG);
        display.drawFastHLine(CONTENT_X, TZ2_Y + TZ_BLOCK_H - 1, CONTENT_W, C_SEP);
    }
    drawLogSection();
    drawSidebar();
}

// =============================================================================
//  Écran historique complet
// =============================================================================
static void drawLogScreen() {
    display.fillRect(CONTENT_X, 0, CONTENT_W, SCREEN_H, C_BG);

    // Header
    display.fillRect(CONTENT_X, 0, CONTENT_W, HDR_H, C_HDR_BG);
    display.drawFastHLine(CONTENT_X, HDR_H, CONTENT_W, C_SEP);
    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TZ_TIME, C_HDR_BG);
    display.setTextDatum(lgfx::middle_center);
    display.drawString("Sync history", CONTENT_X + CONTENT_W / 2, HDR_H / 2);
    display.setTextDatum(lgfx::top_left);

    int n = activityLog.count();
    if (n == 0) {
        display.setFont(&fonts::FreeSans9pt7b);
        display.setTextColor(0x606060u, C_BG);
        display.setTextDatum(lgfx::middle_center);
        display.drawString("No sync yet", CONTENT_X + CONTENT_W / 2, SCREEN_H / 2);
        display.setTextDatum(lgfx::top_left);
        drawSidebar();
        return;
    }

    int entryH  = (SCREEN_H - HDR_H) / LOG_MAX_ENTRIES;
    int maxShow = min(n, LOG_MAX_ENTRIES);

    for (int i = 0; i < maxShow; i++) {
        LogEntry e = activityLog.getEntry(i);
        int ey = HDR_H + i * entryH;

        display.setFont(&fonts::Font2);
        display.setTextColor(C_LOG_NAME, C_BG);
        display.setTextDatum(lgfx::top_left);
        display.drawString(e.watchName, CONTENT_X + 8, ey + 2);

        display.setTextColor(C_LOG_TIME, C_BG);
        display.setTextDatum(lgfx::top_right);
        display.drawString(e.syncTime, SCREEN_W - 8, ey + 2);

        if (i < maxShow - 1)
            display.drawFastHLine(CONTENT_X, ey + entryH - 1, CONTENT_W, C_SEP);
    }
    display.setTextDatum(lgfx::top_left);
    drawSidebar();
}

// =============================================================================
//  Portail AP de configuration
// =============================================================================
static const char PORTAL_HTML[] PROGMEM = R"rawhtml(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD Shock</title><style>
body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}
h1{color:#333;font-size:1.4em}
label{display:block;margin-top:1em;font-weight:bold;font-size:.9em}
input{width:100%;padding:.4em;box-sizing:border-box;border:1px solid #ccc;border-radius:3px}
.hint{font-weight:normal;color:#888;font-size:.8em}
button{display:block;width:100%;margin-top:1.5em;padding:.7em;background:#0055cc;
       color:#fff;border:none;border-radius:4px;font-size:1em;cursor:pointer}
button:hover{background:#003a9e}</style></head><body>
<h1>CYD Shock — Configuration</h1>
<form method="POST" action="/save">
<label>WiFi SSID</label>
<input name="ssid" value="%SSID%" required>
<label>WiFi Password <span class="hint">(leave blank to keep current)</span></label>
<input name="pass" type="password" placeholder="(unchanged)">
<label>Timezone 1 <span class="hint">e.g. Europe/Paris, Asia/Shanghai</span></label>
<input name="tz" value="%TZ%" required>
<label>Timezone 2 <span class="hint">(optional)</span></label>
<input name="tz2" value="%TZ2%">
<button type="submit">Save &amp; Restart</button>
</form></body></html>)rawhtml";

static const char PORTAL_SAVED[] PROGMEM =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<style>body{font-family:sans-serif;text-align:center;padding:3em}"
    "h2{color:green}</style></head><body>"
    "<h2>Saved!</h2><p>The board is restarting&hellip;</p></body></html>";

static void drawPortalScreen() {
    display.fillRect(CONTENT_X, 0, CONTENT_W, SCREEN_H, C_BG);
    display.fillRect(CONTENT_X, 0, CONTENT_W, HDR_H, C_HDR_BG);
    display.drawFastHLine(CONTENT_X, HDR_H, CONTENT_W, C_SEP);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TZ_TIME, C_HDR_BG);
    display.setTextDatum(lgfx::middle_center);
    display.drawString("Wi-Fi Portal", CONTENT_X + CONTENT_W / 2, HDR_H / 2);

    display.setTextColor(C_TZ_DATE, C_BG);
    display.drawString("Connect your phone to:", CONTENT_X + CONTENT_W / 2, 60);

    display.setFont(&fonts::FreeSans12pt7b);
    display.setTextColor(C_LOG_NAME, C_BG);
    display.drawString(PORTAL_SSID, CONTENT_X + CONTENT_W / 2, 82);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TZ_DATE, C_BG);
    display.drawString("then open your browser:", CONTENT_X + CONTENT_W / 2, 124);

    display.setFont(&fonts::FreeSans12pt7b);
    display.setTextColor(C_TZ_LABEL, C_BG);
    display.drawString("192.168.4.1", CONTENT_X + CONTENT_W / 2, 146);

    display.setFont(&fonts::Font2);
    display.setTextColor(0x404040u, C_BG);
    display.drawString("Long press to cancel", CONTENT_X + CONTENT_W / 2, 210);
    display.setTextDatum(lgfx::top_left);

    drawSidebar();
}

static void portalHandleRoot() {
    String html = FPSTR(PORTAL_HTML);
    html.replace("%SSID%", String(appCfg.wifi_ssid));
    html.replace("%TZ%",   String(appCfg.timezone));
    html.replace("%TZ2%",  String(appCfg.timezone2));
    g_portalServer->send(200, "text/html; charset=utf-8", html);
}

static void portalHandleSave() {
    String ssid = g_portalServer->arg("ssid");
    String pass = g_portalServer->arg("pass");
    String tz   = g_portalServer->arg("tz");
    String tz2  = g_portalServer->arg("tz2");

    if (ssid.isEmpty() || tz.isEmpty()) {
        g_portalServer->send(400, "text/plain", "SSID and timezone are required.");
        return;
    }

    strlcpy(appCfg.wifi_ssid,  ssid.c_str(), sizeof(appCfg.wifi_ssid));
    if (pass.length() > 0)
        strlcpy(appCfg.wifi_password, pass.c_str(), sizeof(appCfg.wifi_password));
    strlcpy(appCfg.timezone,  tz.c_str(),  sizeof(appCfg.timezone));
    strlcpy(appCfg.timezone2, tz2.c_str(), sizeof(appCfg.timezone2));

    if (saveAppConfig(appCfg)) {
        g_portalServer->send(200, "text/html; charset=utf-8", FPSTR(PORTAL_SAVED));
        delay(2000);
        ESP.restart();
    } else {
        g_portalServer->send(500, "text/plain", "Save failed.");
    }
}

static void stopPortal() {
    if (!g_portalServer) return;
    g_dnsServer.stop();
    g_portalServer->stop();
    delete g_portalServer;
    g_portalServer = nullptr;
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
}

static void startPortal() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(PORTAL_SSID);
    g_dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
    g_portalServer = new WebServer(80);
    g_portalServer->on("/",     HTTP_GET,  portalHandleRoot);
    g_portalServer->on("/save", HTTP_POST, portalHandleSave);
    g_portalServer->onNotFound([]() {
        g_portalServer->sendHeader("Location", "http://192.168.4.1/");
        g_portalServer->send(302, "text/plain", "");
    });
    g_portalServer->begin();
    drawPortalScreen();
}

// =============================================================================
//  Navigation entre écrans
// =============================================================================
static void enterScreen(Screen s) {
    if (currentScreen == SCR_PORTAL && s != SCR_PORTAL) stopPortal();
    currentScreen = s;

    switch (s) {
        case SCR_MAIN:
            drawMainScreen();
            break;
        case SCR_LOG:
            drawLogScreen();
            break;
        case SCR_PORTAL:
            startPortal();
            break;
    }
}

// =============================================================================
//  Gestion du tactile
//
//  Machine à 3 états (identique à lmscyd) :
//    TS_IDLE       : pas de contact
//    TS_PRESSING   : contact en cours
//    TS_PENDING_TAP: contact relâché, attente confirmation (anti-glitch XPT2046)
// =============================================================================
enum TouchState { TS_IDLE, TS_PRESSING, TS_PENDING_TAP };
static TouchState    touchState  = TS_IDLE;
static unsigned long touchDownMs = 0;
static unsigned long touchUpMs   = 0;
static unsigned long lastTouchMs = 0;
static int16_t       lastTx      = 0;
static int16_t       lastTy      = 0;

static void handleShortTap(int16_t tx, int16_t ty) {
    // Sidebar (x < SIDEBAR_W) : navigation entre écrans
    if (tx < SIDEBAR_W) {
        Screen dest;
        if      (ty < 80)  dest = SCR_MAIN;
        else if (ty < 160) dest = SCR_LOG;
        else               dest = SCR_PORTAL;

        if (dest != currentScreen) enterScreen(dest);
        return;
    }

    // Zone de contenu selon l'écran courant
    switch (currentScreen) {
        case SCR_PORTAL:
            break;  // navigation via téléphone uniquement
        default:
            break;
    }
}

static void handleLongPress(int16_t tx, int16_t ty) {
    // Appui long sur la zone de contenu → retour à l'écran principal
    if (tx >= SIDEBAR_W && currentScreen != SCR_MAIN) {
        enterScreen(SCR_MAIN);
    }
    // Appui long sur le portail → annuler le portail
    if (currentScreen == SCR_PORTAL) {
        enterScreen(SCR_MAIN);
    }
}

static void handleTouch() {
    int16_t tx, ty;
    bool pressed = display.getTouch(&tx, &ty);
    if (pressed) tx = (SCREEN_W - 1) - tx;  // X inversé sur ce CYD
    unsigned long now = millis();

    switch (touchState) {
        case TS_IDLE:
            if (pressed) {
                touchDownMs = now;
                touchState  = TS_PRESSING;
                lastTx = tx; lastTy = ty;
            }
            break;

        case TS_PRESSING:
            if (pressed) {
                lastTx = tx; lastTy = ty;
                if (now - touchDownMs >= (unsigned long)LONG_PRESS_MS) {
                    lastTouchMs = now;
                    touchState  = TS_IDLE;
                    handleLongPress(lastTx, lastTy);
                }
            } else {
                touchUpMs  = now;
                touchState = TS_PENDING_TAP;
            }
            break;

        case TS_PENDING_TAP:
            if (pressed) {
                // Recontact rapide = parasite XPT2046
                touchState = TS_PRESSING;
            } else if (now - touchUpMs >= (unsigned long)TOUCH_GLITCH_MS) {
                // Tap confirmé
                if (now - lastTouchMs >= (unsigned long)TOUCH_DEBOUNCE_MS) {
                    lastTouchMs = now;
                    handleShortTap(lastTx, lastTy);
                }
                touchState = TS_IDLE;
            }
            break;
    }
}

// =============================================================================
//  Tâche BLE (core 0)
//  Scanne en boucle et appelle syncWatch() à chaque montre trouvée.
// =============================================================================
static void bleTask(void*) {
    // Attendre que le WiFi et NTP soient prêts
    vTaskDelay(pdMS_TO_TICKS(8000));

    gshockBLE.begin();

    while (true) {
        SyncResult result;
        bool ok = gshockBLE.syncWatch(result);

        if (ok) {
            // Enregistrer dans l'historique
            activityLog.addEntry(result.watchName, result.timestamp);

            // Signaler à la tâche d'affichage
            xSemaphoreTake(g_syncMutex, portMAX_DELAY);
            g_syncResult    = result;
            g_syncAvailable = true;
            xSemaphoreGive(g_syncMutex);

            Serial.printf("[main] Synced: %s\n", result.watchName);
            vTaskDelay(pdMS_TO_TICKS(5000));  // pause après synchro réussie
        } else {
            vTaskDelay(pdMS_TO_TICKS(2000));  // pause entre les scans
        }
    }
}

// =============================================================================
//  Setup
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("\n%s v%s\n", APP_NAME, APP_VERSION);

    // --- Affichage ---
    display.init();
    display.setRotation(1);  // paysage, USB en bas
    display.setBrightness(DISPLAY_BRIGHTNESS);
    display.fillScreen(C_BG);

    display.setFont(&fonts::FreeSans9pt7b);
    display.setTextColor(C_TZ_TIME, C_BG);
    display.setTextDatum(lgfx::middle_center);
    display.drawString("Starting...", SCREEN_W / 2, SCREEN_H / 2);

    // --- Filesystem ---
    if (!LittleFS.begin(false)) {
        Serial.println("[main] LittleFS mount failed — formatting");
        LittleFS.begin(true);
    }

    // --- Configuration ---
    if (!loadAppConfig(appCfg)) {
        // Pas de config : afficher instructions et attendre le portail
        display.fillScreen(C_BG);
        display.setTextColor(C_WARN, C_BG);
        display.drawString("No configuration!", SCREEN_W / 2, 80);
        display.setFont(&fonts::Font2);
        display.setTextColor(C_TZ_DATE, C_BG);
        display.drawString("Connect to WiFi network:", SCREEN_W / 2, 120);
        display.setTextColor(C_LOG_NAME, C_BG);
        display.drawString(PORTAL_SSID, SCREEN_W / 2, 140);
        display.setTextColor(C_TZ_LABEL, C_BG);
        display.drawString("then open: 192.168.4.1", SCREEN_W / 2, 160);
        display.setTextDatum(lgfx::top_left);

        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(PORTAL_SSID);
        g_dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));
        g_portalServer = new WebServer(80);
        g_portalServer->on("/",     HTTP_GET,  portalHandleRoot);
        g_portalServer->on("/save", HTTP_POST, portalHandleSave);
        g_portalServer->onNotFound([]() {
            g_portalServer->sendHeader("Location", "http://192.168.4.1/");
            g_portalServer->send(302, "text/plain", "");
        });
        g_portalServer->begin();
        currentScreen = SCR_PORTAL;
        return;  // loop() gèrera le portail
    }

    // --- Historique ---
    activityLog.begin();

    // --- WiFi ---
    display.fillScreen(C_BG);
    display.setTextDatum(lgfx::middle_center);
    display.setTextColor(C_TZ_TIME, C_BG);
    display.drawString("Connecting to WiFi...", SCREEN_W / 2, SCREEN_H / 2);

    WiFi.mode(WIFI_STA);
    WiFi.begin(appCfg.wifi_ssid, appCfg.wifi_password);

    unsigned long wStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wStart < 15000) {
        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[main] WiFi OK: %s\n", WiFi.localIP().toString().c_str());
        // NTP
        configTzTime(ianaToposix(appCfg.timezone), NTP_SERVER);
        // Attendre la synchronisation NTP (max 10 s)
        {
            time_t t = 0;
            for (int i = 0; i < 20 && t < 1000000; i++) { delay(500); time(&t); }
            if (t > 1000000) Serial.println("[main] NTP synced");
            else             Serial.println("[main] NTP timeout");
        }
    } else {
        Serial.println("[main] WiFi failed — continuing without NTP");
    }

    // --- Mutex sync BLE ↔ display ---
    g_syncMutex = xSemaphoreCreateMutex();

    // --- Tâche BLE sur core 0 ---
    xTaskCreatePinnedToCore(bleTask, "BLE", 8192, nullptr, 1, nullptr, 0);

    // --- Écran principal ---
    display.setTextDatum(lgfx::top_left);
    enterScreen(SCR_MAIN);
}

// =============================================================================
//  Loop
// =============================================================================
static unsigned long lastClockUpdate = 0;

void loop() {
    unsigned long now = millis();

    // --- Portail : traiter les requêtes HTTP ---
    if (currentScreen == SCR_PORTAL && g_portalServer) {
        g_dnsServer.processNextRequest();
        g_portalServer->handleClient();
    }

    // --- Tactile ---
    handleTouch();

    // --- Synchro BLE disponible → rafraîchir l'affichage ---
    bool syncAvail = false;
    if (g_syncMutex && xSemaphoreTake(g_syncMutex, 0) == pdTRUE) {
        syncAvail       = g_syncAvailable;
        g_syncAvailable = false;
        xSemaphoreGive(g_syncMutex);
    }
    if (syncAvail && currentScreen == SCR_MAIN) {
        drawLogSection();  // rafraîchir uniquement la section log
    }

    // --- Mise à jour de l'horloge (toutes les secondes) ---
    if (now - lastClockUpdate >= (unsigned long)CLOCK_UPDATE_MS) {
        lastClockUpdate = now;
        if (currentScreen == SCR_MAIN) {
            drawHeader();
            drawTzBlock(TZ1_Y, appCfg.timezone, true);
            if (appCfg.timezone2[0] != '\0') {
                drawTzBlock(TZ2_Y, appCfg.timezone2, false);
            }
        }
    }

    delay(10);
}
