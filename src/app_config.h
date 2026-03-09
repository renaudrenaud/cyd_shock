#pragma once
#include <Arduino.h>

// Paramètres lus depuis /config.json sur le filesystem LittleFS.
// Pour modifier : éditer data/config.json puis flasher avec
//   pio run -t uploadfs
// Ou utiliser le portail web en passant la carte en mode AP.

struct AppConfig {
    char wifi_ssid    [64];
    char wifi_password[64];
    char timezone     [64];   // fuseau principal, format IANA ex: "Europe/Paris"
    char timezone2    [64];   // fuseau secondaire, "" = désactivé
    bool valid;               // false si le fichier est absent ou invalide
};

// Charge /config.json depuis LittleFS.
// Retourne true si la lecture a réussi, false sinon (cfg.valid == false).
bool loadAppConfig(AppConfig& cfg);

// Sauvegarde cfg dans /config.json sur LittleFS.
// Retourne true si l'écriture a réussi.
bool saveAppConfig(const AppConfig& cfg);
