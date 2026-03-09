#pragma once
#include <Arduino.h>
#include "config.h"

// Entrée de l'historique de synchronisation
struct LogEntry {
    char    watchName[48];  // ex: "GW-B5600"
    char    syncTime [24];  // ex: "07/03 14:23"
    time_t  timestamp;      // epoch UTC, 0 = invalide
};

// Gère l'historique des synchronisations (max LOG_MAX_ENTRIES).
// Persisté dans /log.json sur LittleFS.
// Thread-safe via mutex interne.
class ActivityLog {
public:
    void      begin();
    void      addEntry(const char* watchName, time_t ts);
    int       count() const;
    LogEntry  getEntry(int index) const;  // 0 = plus récent
    void      clear();

private:
    LogEntry        _entries[LOG_MAX_ENTRIES];
    int             _count   = 0;
    SemaphoreHandle_t _mutex = nullptr;

    void _load();
    void _save() const;
};

extern ActivityLog activityLog;
