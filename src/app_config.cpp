#include "app_config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

bool loadAppConfig(AppConfig& cfg) {
    cfg = AppConfig{};

    if (!LittleFS.begin(false)) {
        Serial.println("[config] Cannot mount LittleFS");
        return false;
    }

    File f = LittleFS.open("/config.json", "r");
    if (!f) {
        Serial.println("[config] /config.json not found");
        LittleFS.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    LittleFS.end();

    if (err) {
        Serial.printf("[config] JSON error: %s\n", err.c_str());
        return false;
    }

    if (!doc["wifi_ssid"].is<const char*>()) {
        Serial.println("[config] Missing required field: wifi_ssid");
        return false;
    }

    strlcpy(cfg.wifi_ssid,     doc["wifi_ssid"]     | "",             sizeof(cfg.wifi_ssid));
    strlcpy(cfg.wifi_password, doc["wifi_password"] | "",             sizeof(cfg.wifi_password));
    strlcpy(cfg.timezone,      doc["timezone"]      | "Europe/Paris", sizeof(cfg.timezone));
    strlcpy(cfg.timezone2,     doc["timezone2"]     | "",             sizeof(cfg.timezone2));
    cfg.valid = true;

    Serial.printf("[config] OK  wifi=%s  tz=%s  tz2=%s\n",
                  cfg.wifi_ssid, cfg.timezone, cfg.timezone2);
    return true;
}

bool saveAppConfig(const AppConfig& cfg) {
    if (!LittleFS.begin(false)) {
        Serial.println("[config] Cannot mount LittleFS for save");
        return false;
    }

    File f = LittleFS.open("/config.json", "w");
    if (!f) {
        Serial.println("[config] Cannot open /config.json for writing");
        LittleFS.end();
        return false;
    }

    JsonDocument doc;
    doc["wifi_ssid"]     = cfg.wifi_ssid;
    doc["wifi_password"] = cfg.wifi_password;
    doc["timezone"]      = cfg.timezone;
    doc["timezone2"]     = cfg.timezone2;

    bool ok = (serializeJson(doc, f) > 0);
    f.close();
    LittleFS.end();

    Serial.println(ok ? "[config] Saved OK" : "[config] Write failed");
    return ok;
}
