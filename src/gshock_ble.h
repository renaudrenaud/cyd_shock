#pragma once
#include <Arduino.h>
#include <time.h>

// Résultat d'une synchronisation
struct SyncResult {
    bool   success;
    char   watchName[48];
    time_t timestamp;
};

// Gère le scan BLE et la synchronisation de l'heure sur les montres G-Shock.
// syncWatch() est bloquant — à appeler depuis une tâche FreeRTOS dédiée.
class GShockBLE {
public:
    void begin();

    // Scanne jusqu'à trouver une montre G-Shock, synchronise l'heure et retourne.
    // Retourne true si la synchronisation a réussi.
    bool syncWatch(SyncResult& result);
};

extern GShockBLE gshockBLE;
