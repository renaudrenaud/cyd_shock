#include "activity_log.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>

ActivityLog activityLog;

static const char* LOG_PATH = "/log.json";

void ActivityLog::begin() {
    _mutex = xSemaphoreCreateMutex();
    _load();
}

void ActivityLog::addEntry(const char* watchName, time_t ts) {
    // Formater la date/heure locale
    char timeStr[24] = {};
    struct tm* t = localtime(&ts);
    if (t) snprintf(timeStr, sizeof(timeStr), "%02d/%02d %02d:%02d",
                    t->tm_mday, t->tm_mon + 1, t->tm_hour, t->tm_min);

    xSemaphoreTake(_mutex, portMAX_DELAY);

    // Décaler les entrées existantes pour insérer en tête
    int newCount = min(_count + 1, LOG_MAX_ENTRIES);
    for (int i = newCount - 1; i > 0; i--) _entries[i] = _entries[i - 1];

    strlcpy(_entries[0].watchName, watchName, sizeof(_entries[0].watchName));
    strlcpy(_entries[0].syncTime,  timeStr,   sizeof(_entries[0].syncTime));
    _entries[0].timestamp = ts;
    _count = newCount;

    _save();
    xSemaphoreGive(_mutex);

    Serial.printf("[log] Added: %s  %s\n", watchName, timeStr);
}

int ActivityLog::count() const {
    return _count;
}

LogEntry ActivityLog::getEntry(int index) const {
    if (index < 0 || index >= _count) return LogEntry{};
    return _entries[index];
}

void ActivityLog::clear() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    _count = 0;
    memset(_entries, 0, sizeof(_entries));
    _save();
    xSemaphoreGive(_mutex);
}

void ActivityLog::_load() {
    if (!LittleFS.begin(false)) return;

    File f = LittleFS.open(LOG_PATH, "r");
    if (!f) { LittleFS.end(); return; }

    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) {
        f.close(); LittleFS.end(); return;
    }
    f.close();
    LittleFS.end();

    JsonArray arr = doc.as<JsonArray>();
    _count = 0;
    for (JsonObject obj : arr) {
        if (_count >= LOG_MAX_ENTRIES) break;
        strlcpy(_entries[_count].watchName, obj["name"] | "", sizeof(_entries[_count].watchName));
        strlcpy(_entries[_count].syncTime,  obj["time"] | "", sizeof(_entries[_count].syncTime));
        _entries[_count].timestamp = obj["ts"] | (long long)0;
        _count++;
    }
    Serial.printf("[log] Loaded %d entries\n", _count);
}

void ActivityLog::_save() const {
    if (!LittleFS.begin(false)) return;

    File f = LittleFS.open(LOG_PATH, "w");
    if (!f) { LittleFS.end(); return; }

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < _count; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"] = _entries[i].watchName;
        obj["time"] = _entries[i].syncTime;
        obj["ts"]   = (long long)_entries[i].timestamp;
    }
    serializeJson(doc, f);
    f.close();
    LittleFS.end();
}
