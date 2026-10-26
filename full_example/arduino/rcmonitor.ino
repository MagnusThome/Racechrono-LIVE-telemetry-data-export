// Built and tested with ESP32 Arduino core 3.3.x and NimBLE-Arduino 2.5.2.
// Do not use core 4.0.0-RC1 on ESP32-S3: it crashes at startup in the
// Bluetooth controller (see the README).

#include <Arduino.h>
#include "rcmonitor.h"

#ifdef ESP32
  #include <NimBLEDevice.h>  // NOTE!!! Version 2.x of NimBLE-Arduino (tested with 2.5.2), 1.x will not compile
#else
  #include <bluefruit.h>
#endif

#define CMD_TYPE_REMOVE_ALL 0
#define CMD_TYPE_REMOVE 1
#define CMD_TYPE_ADD_INCOMPLETE 2
#define CMD_TYPE_ADD 3
#define CMD_TYPE_UPDATE_ALL 4
#define CMD_TYPE_UPDATE 5

#define CMD_RESULT_OK 0
#define CMD_RESULT_PAYLOAD_OUT_OF_SEQUENCE 1
#define CMD_RESULT_EQUATION_EXCEPTION 2

#define MAX_PAYLOAD_PART 17

#define MAIN_SERVICE_NAME           (uint16_t) 0x1ff8
#define CONFIG_CHARACTERISTIC       (uint16_t) 0x05
#define NOTIFICATION_CHARACTERISTIC (uint16_t) 0x06

#define CONFIG_INDICATE_DELAY_MS 50



#ifdef ESP32
  static NimBLEServer* pServer;
  static NimBLECharacteristic* monitorConfigCharacteristic;
  static NimBLECharacteristic* monitorNotificationCharacteristic;
#else
  BLEService mainService = BLEService(MAIN_SERVICE_NAME);
  BLECharacteristic monitorConfigCharacteristic = BLECharacteristic (CONFIG_CHARACTERISTIC);
  BLECharacteristic monitorNotificationCharacteristic = BLECharacteristic (NOTIFICATION_CHARACTERISTIC);
#endif 


#define CONFIG_RESULT_TIMEOUT_MS 1000
#define CONFIG_ATTEMPTS 3

#define CONFIG_RESULT_NONE 0xff


boolean wasConnected = true;
boolean monitorConfigStarted = false;
int nextMonitorId = 0;

/* Set from the BLE stack's own task. A connect event is kept even when the
   link has already dropped and come back before rcmonitor() runs again (it can
   be blocked in an HTTP request for seconds), which a comparison of the
   connection state alone would miss. The app forgets every monitor on a new
   connection, so each one has to be configured again. */
static volatile uint32_t connectCount = 0;
static uint32_t handledConnectCount = 0;

#ifdef ESP32
/* Whether the app has registered for indications on the config characteristic,
   kept from the BLE task. NimBLE 2.x has no call that answers this directly.
   Cleared on disconnect there too, not in handleConnected(): the app may
   already have subscribed again by the time loop() notices the reconnect. */
static volatile boolean configIndicateEnabled = false;
#endif

/* Reply from the app to the last "Add complete", also set from the BLE task. */
static volatile uint8_t configResult = CONFIG_RESULT_NONE;
static volatile uint8_t configResultMonitorId = 0;


// ---------------------------------------------------------------------------
// Platform abstraction
// ---------------------------------------------------------------------------

static boolean monitorConnected() {
#ifdef ESP32
    return pServer != NULL && pServer->getConnectedCount() > 0;
#else
    return Bluefruit.connected();
#endif
}

/* The app has to register for indications on the config characteristic before
   it will accept any configuration. Notifications sent before that are
   silently dropped, so this is what we wait for, not just a connection. */
static boolean monitorConfigSubscribed() {
#ifdef ESP32
    return configIndicateEnabled;
#else
    return monitorConfigCharacteristic.indicateEnabled();
#endif
}

/* One INDICATE on the configuration characteristic. */
static boolean indicateConfig(const uint8_t* data, size_t length) {
#ifdef ESP32
    if (!monitorConfigCharacteristic->indicate(data, length)) {
        return false;
    }
#else
    if (!monitorConfigCharacteristic.indicate((uint8_t*) data, length)) {
        return false;
    }
#endif
    delay(CONFIG_INDICATE_DELAY_MS);
    return true;
}


// ---------------------------------------------------------------------------
// Configuration commands
// ---------------------------------------------------------------------------

/* Sends one monitored value definition. Equations longer than one payload are
   split; every part but the last is CMD_TYPE_ADD_INCOMPLETE. */
static boolean sendMonitorEquation(uint8_t monitorId, const char* equation) {
    if (equation == NULL || equation[0] == '\0') {
        return false;
    }

    // The app can also answer an incomplete part with an error, so the old
    // reply is cleared before the first part goes out.
    configResult = CONFIG_RESULT_NONE;

    const size_t equationLength = strlen(equation);
    size_t offset = 0;
    uint8_t sequence = 0;

    do {
        size_t partLength = equationLength - offset;
        if (partLength > MAX_PAYLOAD_PART) {
            partLength = MAX_PAYLOAD_PART;
        }
        const boolean isLastPart = (offset + partLength) == equationLength;

        uint8_t packet[3 + MAX_PAYLOAD_PART];
        packet[0] = isLastPart ? CMD_TYPE_ADD : CMD_TYPE_ADD_INCOMPLETE;
        packet[1] = monitorId;
        packet[2] = sequence++;
        memcpy(packet + 3, equation + offset, partLength);

        if (!indicateConfig(packet, 3 + partLength)) {
            return false;
        }
        offset += partLength;
    } while (offset < equationLength);

    return true;
}

/* Waits for the app's reply to an equation. Returns the result code, or
   CONFIG_RESULT_NONE when nothing came back in time or the link dropped. */
static uint8_t waitConfigResult(uint8_t monitorId) {
    const unsigned long start = millis();
    while (millis() - start < CONFIG_RESULT_TIMEOUT_MS) {
        const uint8_t result = configResult;
        if (result != CONFIG_RESULT_NONE && configResultMonitorId == monitorId) {
            return result;
        }
        if (!monitorConnected()) {
            break;
        }
        delay(10);
    }
    return CONFIG_RESULT_NONE;
}

static boolean addMonitor(const char* monitorName, const char* equation, float multiplier) {
    if (nextMonitorId >= MONITORS_MAX) {
        return false;
    }
    const uint8_t monitorId = (uint8_t) nextMonitorId;

    strncpy(monitorNames[monitorId], monitorName, MONITOR_NAME_MAX);
    monitorNames[monitorId][MONITOR_NAME_MAX] = '\0';
    monitorMultipliers[monitorId] = multiplier;
    monitorValues[monitorId] = INVALID_VALUE;
    // Accept values for this id already while waiting for the reply; the app
    // may send the first one before its confirmation arrives.
    nextMonitorId = monitorId + 1;

    uint8_t result = CONFIG_RESULT_NONE;
    for (int attempt = 0; attempt < CONFIG_ATTEMPTS; attempt++) {
        if (!sendMonitorEquation(monitorId, equation)) {
            break;
        }
        result = waitConfigResult(monitorId);
        if (result == CMD_RESULT_OK || result == CMD_RESULT_EQUATION_EXCEPTION || !monitorConnected()) {
            break;
        }
    }

    if (result == CMD_RESULT_EQUATION_EXCEPTION) {
        // Not retried, it would fail the same way. The id stays taken so the
        // ones after it still match MonitorIndex; this value just never arrives.
        Serial.print(monitorName);
        Serial.println(" rejected by the app, check its equation");
        return true;
    }
    if (result != CMD_RESULT_OK) {
        nextMonitorId = monitorId;
        return false;
    }
    return true;
}

static boolean configureMonitors() {
    // Clears whatever a failed earlier attempt left behind in the app.
    const uint8_t removeAll[] = { CMD_TYPE_REMOVE_ALL };
    if (!indicateConfig(removeAll, sizeof removeAll)) {
        return false;
    }

    nextMonitorId = 0;
    for (size_t i = 0; i < sizeof monitors / sizeof monitors[0]; i++) {
        if (!addMonitor(monitors[i].nam, monitors[i].def, monitors[i].mul)) {
            // Do not leave a half configured set behind.
            nextMonitorId = 0;
            return false;
        }
    }
    activeMonitors = (uint8_t) nextMonitorId;

    // Values are only sent when they change, so a value that arrived before its
    // id was accepted would otherwise not come again until it changes (for the
    // lap number, not until the next lap).
    const uint8_t updateAll[] = { CMD_TYPE_UPDATE_ALL };
    indicateConfig(updateAll, sizeof updateAll);
    return true;
}


// ---------------------------------------------------------------------------
// Incoming data from the app
// ---------------------------------------------------------------------------

/* A write can contain several 5 byte records: monitor id + big-endian int32. */
static void handleMonitorNotification(const uint8_t* data, size_t length) {
    size_t pos = 0;
    while (pos + 5 <= length) {
        const uint8_t monitorId = data[pos];
        // Cast before shifting: char is signed on most targets, and shifting
        // into the sign bit of an int is undefined behaviour.
        const uint32_t raw = ((uint32_t) data[pos + 1] << 24)
                           | ((uint32_t) data[pos + 2] << 16)
                           | ((uint32_t) data[pos + 3] << 8)
                           | ((uint32_t) data[pos + 4]);
        if (monitorId < nextMonitorId) {
            monitorValues[monitorId] = (int32_t) raw;
        }
        pos += 5;
    }
}

/* Result of a configuration command. */
static void handleConfigResult(const uint8_t* data, size_t length) {
    if (length < 2) {
        return;
    }
    const uint8_t result = data[0];
    const uint8_t monitorId = data[1];

    // Id first: waitConfigResult() checks the result, then the id.
    configResultMonitorId = monitorId;
    configResult = result;

    switch (result) {
        case CMD_RESULT_PAYLOAD_OUT_OF_SEQUENCE:
            Serial.print(monitorId);
            Serial.println(" out-of-sequence");
            break;

        case CMD_RESULT_EQUATION_EXCEPTION:
            Serial.print(monitorId);
            Serial.print(" exception");
            if (length >= 8) {
                const uint16_t exceptionType = ((uint16_t) data[2] << 8) | data[3];
                const uint16_t exceptionPos  = ((uint16_t) data[4] << 8) | data[5];
                const uint16_t exceptionLen  = ((uint16_t) data[6] << 8) | data[7];
                Serial.print(" type=");
                Serial.print(exceptionType);
                Serial.print(" pos=");
                Serial.print(exceptionPos);
                Serial.print(" len=");
                Serial.print(exceptionLen);
            }
            Serial.println();
            break;

        default:
            break;
    }
}


// ---------------------------------------------------------------------------
// BLE callbacks
// ---------------------------------------------------------------------------

#ifdef ESP32  // ---------------------------------------------------------------------------

// NimBLE 2.x callback signatures. "override" makes a mismatch a compile error;
// without it a 1.x style callback still compiles but is never called.

class monitorConfigWriteCallback: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        (void) connInfo;
        NimBLEAttValue value = pCharacteristic->getValue();
        handleConfigResult(reinterpret_cast<const uint8_t*>(value.data()), value.length());
    };

    // subValue: bit 0 notifications, bit 1 indications, 0 when unsubscribed.
    void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
        (void) pCharacteristic;
        (void) connInfo;
        configIndicateEnabled = (subValue & 0x02) != 0;
    };
};

class monitorNotificationWriteCallback: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        (void) connInfo;
        NimBLEAttValue value = pCharacteristic->getValue();
        handleMonitorNotification(reinterpret_cast<const uint8_t*>(value.data()), value.length());
    };
};

class monitorServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        (void) pServer;
        (void) connInfo;
        connectCount = connectCount + 1;
    };

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        (void) pServer;
        (void) connInfo;
        (void) reason;
        configIndicateEnabled = false;
#if CONFIG_BT_NIMBLE_EXT_ADV
        // advertiseOnDisconnect() has no effect with extended advertising, so
        // advertising is restarted here instead.
        NimBLEDevice::getAdvertising()->start(0);
#endif
    };
};

#else  // ---------------------------------------------------------------------------

void monitorConnectCallback(uint16_t conn_hdl) {
    (void) conn_hdl;
    connectCount = connectCount + 1;
}

void monitorConfigWriteCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    (void) conn_hdl;
    (void) chr;
    handleConfigResult(data, len);
}

void monitorNotificationWriteCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len) {
    (void) conn_hdl;
    (void) chr;
    handleMonitorNotification(data, len);
}

#endif  // ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Connection handling
// ---------------------------------------------------------------------------

static void handleConnected() {
    for (int i = 0; i < MONITORS_MAX; i++) {
        monitorValues[i] = INVALID_VALUE;
    }
    monitorConfigStarted = false;
    nextMonitorId = 0;
    Serial.println("BLE connected!");
}

static void handleDisconnected() {
    Serial.println("No BLE connection");
}


/* Configures once the app subscribes, retrying from loop() if it fails. Waits
   for the app to confirm each value, normally well under a second in all, and
   gives up as soon as the link drops so a disconnect cannot stall the sketch. */
boolean handleConfigure() {
    if (monitorConfigStarted) {
        return true;
    }
    if (!monitorConfigSubscribed()) {
        return false;
    }

    Serial.print("Configuring... ");
    monitorConfigStarted = true;
    if (!configureMonitors()) {
        monitorConfigStarted = false;
        Serial.println("Fail");
        return false;
    }
    Serial.println("Done");
    return true;
}


// ---------------------------------------------------------------------------
// BLE startup
// ---------------------------------------------------------------------------

#ifdef ESP32  // ---------------------------------------------------------------------------

static monitorConfigWriteCallback cbmonitorConfigWriteCallback;
static monitorNotificationWriteCallback cbmonitorNotificationWriteCallback;
static monitorServerCallbacks cbmonitorServerCallbacks;

void bluetoothStart(void) {

    NimBLEDevice::init("RC DIY");
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(&cbmonitorServerCallbacks, false);  // static, never delete it
#if !CONFIG_BT_NIMBLE_EXT_ADV
    // Off by default in NimBLE 2.x; without it the app cannot find the device
    // again after the first disconnect. With extended advertising this does not
    // exist, and onDisconnect() restarts advertising instead.
    pServer->advertiseOnDisconnect(true);
#endif
    NimBLEService* pMainService = pServer->createService(MAIN_SERVICE_NAME);
  
    monitorConfigCharacteristic = pMainService->createCharacteristic( 
                CONFIG_CHARACTERISTIC, 
                NIMBLE_PROPERTY::READ | 
                NIMBLE_PROPERTY::WRITE | 
                NIMBLE_PROPERTY::INDICATE 
                );
    monitorConfigCharacteristic->setCallbacks(&cbmonitorConfigWriteCallback);

    monitorNotificationCharacteristic = pMainService->createCharacteristic( 
                NOTIFICATION_CHARACTERISTIC, 
                NIMBLE_PROPERTY::WRITE_NR
                );
    monitorNotificationCharacteristic->setCallbacks(&cbmonitorNotificationWriteCallback);
    
    // Native order is least significant byte first, so these are the last two
    // bytes of the printed address; the first ones are the same Espressif
    // vendor prefix on every board.
    const uint8_t* mac = NimBLEDevice::getAddress().getVal();
    char name[255];
    snprintf(name, sizeof(name), "RC DIY #%02X%02X", mac[1], mac[0]);

#if CONFIG_BT_NIMBLE_EXT_ADV
    // Bluetooth 5 builds (for example ESP32-S3 on ESP32 core 4.x) only have the
    // extended advertising API. It is used for plain legacy advertising here,
    // which is what phones scan for. Flags, service and name fit in one packet.
    NimBLEExtAdvertisement advertisement;
    advertisement.setLegacyAdvertising(true);
    advertisement.setConnectable(true);
    advertisement.setFlags(BLE_HS_ADV_F_DISC_GEN);
    advertisement.addServiceUUID(pMainService->getUUID());
    advertisement.setName(name);
    advertisement.setMinInterval(160);
    advertisement.setMaxInterval(160);
    NimBLEExtAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setInstanceData(0, advertisement);
    pAdvertising->start(0);
#else
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName(name);
    pAdvertising->setMinInterval(160);
    pAdvertising->setMaxInterval(160);
    pAdvertising->addServiceUUID(pMainService->getUUID());
    pAdvertising->enableScanResponse(true);
    pAdvertising->start();
#endif
}


#else  // ---------------------------------------------------------------------------

void monitorConfigWriteCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len);
void monitorNotificationWriteCallback(uint16_t conn_hdl, BLECharacteristic* chr, uint8_t* data, uint16_t len);
void monitorConnectCallback(uint16_t conn_hdl);

void bluetoothSetupMainService() {
    mainService.begin();
    monitorConfigCharacteristic.setProperties(CHR_PROPS_INDICATE | CHR_PROPS_WRITE);
    monitorConfigCharacteristic.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    monitorConfigCharacteristic.setWriteCallback(monitorConfigWriteCallback);
    monitorConfigCharacteristic.begin();
    monitorNotificationCharacteristic.setProperties(CHR_PROPS_WRITE_WO_RESP);
    monitorNotificationCharacteristic.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
    monitorNotificationCharacteristic.setWriteCallback(monitorNotificationWriteCallback);
    monitorNotificationCharacteristic.begin();
}

void bluetoothStartAdvertising(void) {
    Bluefruit.setTxPower(+4);
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(mainService);
    Bluefruit.Advertising.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Periph.setConnectCallback(monitorConnectCallback);
    Bluefruit.Advertising.setInterval(160, 160); // in unit of 0.625 ms
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
}

void bluetoothStart() {
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();
    uint8_t mac[6] = { 0, 0, 0, 0, 0, 0 };
    Bluefruit.getAddr(mac);
    char name[255];
    snprintf(name, sizeof(name), "RC DIY #%02X%02X", mac[4], mac[5]);
    Bluefruit.setName(name);     
    bluetoothSetupMainService();
    bluetoothStartAdvertising(); 
}
#endif  // ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void rcmonitorstart() {
    bluetoothStart();
}

boolean rcmonitor() {
    const boolean isConnected = monitorConnected();
    const uint32_t connects = connectCount;
    const boolean newConnection = connects != handledConnectCount;
    handledConnectCount = connects;

    if (newConnection || (isConnected && !wasConnected)) {
        handleConnected();
    } else if (!isConnected && wasConnected) {
        handleDisconnected();
    }
    wasConnected = isConnected;

    if (!isConnected) {
        return false;
    }
    return handleConfigure();
}


// end
