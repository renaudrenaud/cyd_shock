// =============================================================================
//  GShockBLE — Scan BLE et synchronisation de l'heure sur les G-Shock
//
//  Protocole (inspiré de gshock-api-esp32, MIT © Ivo Zivkov) :
//
//  Service principal  : 26eb000d-b012-49a8-b1f8-394fb2032b0f
//  READ_REQUEST  (0C) : 26eb002c-b012-49a8-b1f8-394fb2032b0f  (write sans réponse)
//  ALL_FEATURES  (0E) : 26eb002d-b012-49a8-b1f8-394fb2032b0f  (write avec réponse + notify)
//  DATA_REQUEST  (11) : 26eb0023-b012-49a8-b1f8-394fb2032b0f  (notify)
//  CONVOY        (14) : 26eb0024-b012-49a8-b1f8-394fb2032b0f  (notify)
//
//  Séquence de sync :
//  1. Scan → UUID 0x1804 dans l'advertisement
//  2. Connexion GATT
//  3. Subscribe notifications sur ALL_FEATURES, DATA_REQUEST, CONVOY
//  4. Écrire [0x10] sur READ_REQUEST → notification [0x10, ...] → octet[8] = bouton
//  5. Read-echo DST watch states : écrire [0x1D, state] → notif 0x1D → echo vers ALL_FEATURES
//     (jusqu'à dstCount fois : states 0x00, 0x02, 0x04)
//  6. Read-echo DST world cities : écrire [0x1E, city] → notif 0x1E → echo
//     (pour city 0..worldCitiesCount-1)
//  7. Read-echo world cities (si hasWorldCities) : écrire [0x1F, city] → notif 0x1F → echo
//  8. Écrire 11 octets temps sur ALL_FEATURES (avec réponse) :
//     [0x09, year_lo, year_hi, month, day, hour, min, sec, weekday(0=Lun), 0, 1]
//  9. Déconnexion
// =============================================================================

#include "gshock_ble.h"
#include "config.h"
#include <NimBLEDevice.h>
#include <time.h>
#include <ctype.h>

GShockBLE gshockBLE;

// UUIDs Casio
static const char* SVC_UUID      = "26eb000d-b012-49a8-b1f8-394fb2032b0f";
static const char* CHR_READ_REQ  = "26eb002c-b012-49a8-b1f8-394fb2032b0f";
static const char* CHR_ALL_FEAT  = "26eb002d-b012-49a8-b1f8-394fb2032b0f";
static const char* CHR_DATA_REQ  = "26eb0023-b012-49a8-b1f8-394fb2032b0f";
static const char* CHR_CONVOY    = "26eb0024-b012-49a8-b1f8-394fb2032b0f";
static const uint16_t ADV_UUID   = 0x1804;  // UUID de scan G-Shock

// Buffer de notification partagé entre callback et tâche BLE
static SemaphoreHandle_t g_notifySem = nullptr;
static uint8_t           g_notifyBuf[64];
static size_t            g_notifyLen = 0;

// Callback notifications BLE
static void notifyCallback(NimBLERemoteCharacteristic* pChar,
                           uint8_t* pData, size_t length, bool isNotify) {
    if (length == 0 || length > sizeof(g_notifyBuf)) return;
    memcpy(g_notifyBuf, pData, length);
    g_notifyLen = length;
    xSemaphoreGive(g_notifySem);
}

// Attend une notification avec le premier octet attendu (timeout ms)
static bool waitForNotify(uint8_t expectedFirstByte, uint32_t timeoutMs = BLE_NOTIFY_TIMEOUT) {
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (xSemaphoreTake(g_notifySem, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (g_notifyLen > 0 && g_notifyBuf[0] == expectedFirstByte) return true;
            // Mauvais premier octet : continuer d'attendre
        }
    }
    return false;
}

// Infère le nombre de DST states et de villes depuis le nom de la montre
struct WatchCaps {
    int  dstCount;
    int  worldCitiesCount;
    bool hasWorldCities;
};

static WatchCaps inferCaps(const char* shortName) {
    // GW / GMW / MRG : 3 DST states, 6 villes
    if (strncmp(shortName, "GW",  2) == 0 ||
        strncmp(shortName, "GMW", 3) == 0 ||
        strncmp(shortName, "MRG", 3) == 0) {
        return {3, 6, true};
    }
    // GST / ABL : pas de world cities
    if (strncmp(shortName, "GST", 3) == 0 ||
        strncmp(shortName, "ABL", 3) == 0) {
        return {1, 2, false};
    }
    // GBD : world cities désactivées
    if (strncmp(shortName, "GBD", 3) == 0) {
        return {1, 2, false};
    }
    // Défaut : 1 DST state, 2 villes
    return {1, 2, true};
}

// Extrait le modèle court depuis le nom BLE : "CASIO GW-B5600" → "GW-B5600"
static void extractShortName(const char* fullName, char* out, size_t outLen) {
    const char* sp = strchr(fullName, ' ');
    if (sp && *(sp + 1)) {
        strlcpy(out, sp + 1, outLen);
    } else {
        strlcpy(out, fullName, outLen);
    }
    // Supprimer les caractères non imprimables
    for (char* p = out; *p; p++) {
        if (*p < 32 || *p > 126) *p = ' ';
    }
    // Trim trailing spaces
    int len = strlen(out);
    while (len > 0 && out[len - 1] == ' ') out[--len] = '\0';
}

// Encode l'heure courante en 11 octets Casio
static void buildTimePayload(uint8_t* buf) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);

    int year = t->tm_year + 1900;
    buf[0]  = 0x09;              // CASIO_CURRENT_TIME
    buf[1]  = year & 0xFF;       // year lo
    buf[2]  = (year >> 8) & 0xFF;// year hi
    buf[3]  = t->tm_mon + 1;     // month 1-12
    buf[4]  = t->tm_mday;        // day
    buf[5]  = t->tm_hour;
    buf[6]  = t->tm_min;
    buf[7]  = t->tm_sec;
    buf[8]  = (t->tm_wday + 6) % 7; // weekday : C(0=Sun) → Casio(0=Lun)
    buf[9]  = 0;
    buf[10] = 1;
}

// Effectue une séquence read-echo :
//   1. Écrit `req` sur READ_REQUEST
//   2. Attend une notification avec le premier octet = req[0]
//   3. Renvoie les données reçues sur ALL_FEATURES
static bool readEcho(NimBLERemoteCharacteristic* pReadReq,
                     NimBLERemoteCharacteristic* pAllFeat,
                     const uint8_t* req, size_t reqLen) {
    pReadReq->writeValue(req, reqLen, false);
    if (!waitForNotify(req[0])) {
        Serial.printf("[ble] readEcho timeout for 0x%02X\n", req[0]);
        return false;
    }
    pAllFeat->writeValue(g_notifyBuf, g_notifyLen, true);
    return true;
}

// =============================================================================
//  Scan — cherche une montre G-Shock (UUID 0x1804)
// =============================================================================

// NimBLE-Arduino v1.x : NimBLEAdvertisedDeviceCallbacks + setAdvertisedDeviceCallbacks
class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
public:
    NimBLEAddress foundAddr;
    char          foundName[64] = {};
    bool          found = false;

    void onResult(NimBLEAdvertisedDevice* device) {
        if (device->haveServiceUUID() &&
            device->isAdvertisingService(NimBLEUUID(ADV_UUID))) {
            Serial.printf("[ble] Found: %s  name=%s\n",
                          device->getAddress().toString().c_str(),
                          device->getName().c_str());
            foundAddr = device->getAddress();
            strlcpy(foundName, device->getName().c_str(), sizeof(foundName));
            found = true;
            NimBLEDevice::getScan()->stop();
        }
    }
};

// =============================================================================
//  Interface publique
// =============================================================================

void GShockBLE::begin() {
    g_notifySem = xSemaphoreCreateBinary();
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    Serial.println("[ble] NimBLE initialized");
}

bool GShockBLE::syncWatch(SyncResult& result) {
    result.success = false;
    result.watchName[0] = '\0';

    // --- Scan ---
    ScanCallbacks scanCb;
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(&scanCb, false);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(80);
    pScan->start(BLE_SCAN_SECS, false);   // bloquant

    if (!scanCb.found) {
        Serial.println("[ble] No G-Shock found");
        return false;
    }

    char fullName[64];
    strlcpy(fullName, scanCb.foundName, sizeof(fullName));

    // --- Connexion (par adresse pour éviter un pointeur invalidé post-scan) ---
    NimBLEClient* pClient = NimBLEDevice::createClient();
    pClient->setConnectionParams(12, 12, 0, 400);
    if (!pClient->connect(scanCb.foundAddr)) {
        Serial.println("[ble] Connection failed");
        NimBLEDevice::deleteClient(pClient);
        return false;
    }
    Serial.println("[ble] Connected");

    // --- Service et caractéristiques ---
    NimBLERemoteService* pSvc = pClient->getService(SVC_UUID);
    if (!pSvc) {
        Serial.println("[ble] Service not found");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        return false;
    }

    NimBLERemoteCharacteristic* pReadReq = pSvc->getCharacteristic(CHR_READ_REQ);
    NimBLERemoteCharacteristic* pAllFeat = pSvc->getCharacteristic(CHR_ALL_FEAT);
    NimBLERemoteCharacteristic* pDataReq = pSvc->getCharacteristic(CHR_DATA_REQ);
    NimBLERemoteCharacteristic* pConvoy  = pSvc->getCharacteristic(CHR_CONVOY);

    if (!pReadReq || !pAllFeat) {
        Serial.println("[ble] Required characteristics not found");
        pClient->disconnect();
        NimBLEDevice::deleteClient(pClient);
        return false;
    }

    // Subscribe aux notifications
    if (pAllFeat->canNotify()) pAllFeat->subscribe(true, notifyCallback);
    if (pDataReq && pDataReq->canNotify()) pDataReq->subscribe(true, notifyCallback);
    if (pConvoy  && pConvoy->canNotify())  pConvoy->subscribe(true, notifyCallback);

    delay(300);  // laisser le temps à la montre de s'initialiser

    bool ok = false;
    char shortName[48] = {};
    extractShortName(fullName, shortName, sizeof(shortName));

    // Suffixe des 2 derniers octets MAC pour identifier chaque montre
    // ex: "GW-B5600" + "#A1B2"  (adresse "xx:xx:xx:xx:a1:b2")
    {
        std::string addr = scanCb.foundAddr.toString();  // "aa:bb:cc:dd:ee:ff"
        int len = (int)addr.length();
        if (len >= 5) {
            char suffix[8];
            snprintf(suffix, sizeof(suffix), "  #%c%c%c%c",
                     toupper((unsigned char)addr[len - 5]),
                     toupper((unsigned char)addr[len - 4]),
                     toupper((unsigned char)addr[len - 2]),
                     toupper((unsigned char)addr[len - 1]));
            strlcat(shortName, suffix, sizeof(shortName));
        }
    }

    WatchCaps caps = inferCaps(shortName);

    Serial.printf("[ble] Watch: %s  dstCount=%d  cities=%d  hasWC=%d\n",
                  shortName, caps.dstCount, caps.worldCitiesCount, caps.hasWorldCities);

    // --- Étape 1 : bouton pressé ---
    {
        uint8_t req[] = {0x10};
        pReadReq->writeValue(req, sizeof(req), false);
        if (!waitForNotify(0x10)) {
            Serial.println("[ble] Timeout waiting for button notification");
            goto done;
        }
        // Octet [8] : indicateur du bouton (0/1=gauche, 4=droite, 3=auto)
        // On continue dans tous les cas — on synchronise toujours l'heure
        if (g_notifyLen >= 9) {
            uint8_t btn = g_notifyBuf[8];
            Serial.printf("[ble] Button indicator: 0x%02X\n", btn);
        }
    }

    // --- Étape 2 : DST watch states (read-echo) ---
    {
        static const uint8_t states[] = {0x00, 0x02, 0x04};
        for (int i = 0; i < caps.dstCount; i++) {
            uint8_t req[] = {0x1D, states[i]};
            if (!readEcho(pReadReq, pAllFeat, req, sizeof(req))) goto done;
        }
    }

    // --- Étape 3 : DST world cities (read-echo) ---
    for (int city = 0; city < caps.worldCitiesCount; city++) {
        uint8_t req[] = {0x1E, (uint8_t)city};
        if (!readEcho(pReadReq, pAllFeat, req, sizeof(req))) goto done;
    }

    // --- Étape 4 : World cities (read-echo), si applicable ---
    if (caps.hasWorldCities) {
        for (int city = 0; city < caps.worldCitiesCount; city++) {
            uint8_t req[] = {0x1F, (uint8_t)city};
            if (!readEcho(pReadReq, pAllFeat, req, sizeof(req))) goto done;
        }
    }

    // --- Étape 5 : Envoi de l'heure ---
    {
        uint8_t timeBuf[11];
        buildTimePayload(timeBuf);
        if (!pAllFeat->writeValue(timeBuf, sizeof(timeBuf), true)) {
            Serial.println("[ble] Failed to write time");
            goto done;
        }
        Serial.printf("[ble] Time set OK for %s\n", shortName);
        ok = true;
    }

done:
    pClient->disconnect();
    NimBLEDevice::deleteClient(pClient);

    if (ok) {
        strlcpy(result.watchName, shortName, sizeof(result.watchName));
        result.timestamp = time(nullptr);
        result.success   = true;
    }
    return ok;
}
