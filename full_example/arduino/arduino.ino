// Built and tested with ESP32 Arduino core 3.3.x and NimBLE-Arduino 2.5.2.
// Do not use core 4.0.0-RC1 on ESP32-S3: it crashes at startup in the
// Bluetooth controller (see the README).

#include <Arduino.h>
#include "rcmonitor.h"            // https://github.com/aollin/racechrono-ble-diy-device
#include <WiFi.h>
#include <HTTPClient.h>
#include "config.h"              // WiFi, server address and key


// Everything we want from the app. The index into monitorValues[] and
// monitorMultipliers[] is the position in this array, so MonitorIndex below has
// to match the order here.
struct strmon monitors[] = {
    {"engine_oil_temp", "channel(device(obd), engine_oil_temp)", 1.0},
    {"coolant_temp",    "channel(device(obd), coolant_temp)",    1.0},
    // previous_* is the last completed lap, i.e. a final time. lap_number and
    // lap_time would be the lap in progress, which counts up as you drive.
    {"previous_lap_number", "channel(device(lap), previous_lap_number)",      1.0},
    {"previous_lap_time",   "channel(device(lap), previous_lap_time)*100.0",  0.01},
    // Fuel tank level, 0-100 %. Sent as tenths, since values arrive as integers.
    {"fuel_level",      "channel(device(obd), fuel_level)*10.0",  0.1},
};

enum MonitorIndex {
    MON_ENGINE_OIL_TEMP = 0,
    MON_COOLANT_TEMP,
    MON_PREVIOUS_LAP_NUMBER,
    MON_PREVIOUS_LAP_TIME,
    MON_FUEL_LEVEL,
};

static_assert(sizeof monitors / sizeof monitors[0] == 5, "MonitorIndex must match the monitors array");


#define BUFFSIZE 500
char buff[BUFFSIZE];
HTTPClient http;

// How often the monitored values are polled. One row is uploaded per completed
// lap, so this only sets how quickly a finished lap is noticed: it takes
// LAP_CONFIRM_READS polls, so 1 to 2 seconds here. It is also how often
// queued laps are sent and the BLE and WiFi connections are checked.
#define POLL_INTERVAL_MS 1000

// Limits how long one upload attempt can hold up the loop.
#define HTTP_TIMEOUT_MS 4000

// Wait after a failed upload before trying the same lap again.
#define UPLOAD_RETRY_MS 5000

// How often to ask the WiFi to reconnect while it is down.
#define WIFI_RETRY_MS 15000

// Laps waiting to be uploaded, oldest first. A lap stays here until the server
// has accepted it, so a WiFi dropout or a server error only delays it. When the
// queue is full, the oldest lap is dropped to make room.
#define PENDING_LAPS_MAX 20
#define QUERY_SIZE 160

// previous_lap_number is only updated once a lap is finished. The app may write
// the number and the time in separate notifications, so a new lap is only
// uploaded after the pair has been read the same way twice.
#define LAP_CONFIRM_READS 2


void setup() {
  Serial.begin(115200);
  // Bluetooth first, so the app can connect and laps are recorded even while
  // there is no WiFi yet. They are queued and sent once it is up.
  rcmonitorstart();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
}


// Appends "&name=value" for a monitor that has produced a value. Values that
// have not arrived yet are simply left out, and update.php stores an empty
// field for them.
static void appendQuery(char* query, size_t querySize, const char* name, uint8_t monitorId, int decimals) {
  if (!rcmonitorHasValue(monitorId)) {
    return;
  }
  const size_t used = strlen(query);
  if (used >= querySize) {
    return;
  }
  const char* separator = (used == 0) ? "" : "&";
  snprintf(query + used, querySize - used, "%s%s=%.*f", separator, name, decimals, rcmonitorValue(monitorId));
}


static char pendingLaps[PENDING_LAPS_MAX][QUERY_SIZE];
static uint8_t pendingFirst = 0;
static uint8_t pendingCount = 0;

static boolean uploadFailed = false;
static unsigned long lastUploadFailure = 0;
static unsigned long lastWifiRetry = 0;


// Returns an empty queue slot for a new lap to fill in.
static char* newPendingLap() {
  if (pendingCount == PENDING_LAPS_MAX) {
    Serial.println("Upload queue full, dropping the oldest lap");
    pendingFirst = (pendingFirst + 1) % PENDING_LAPS_MAX;
    pendingCount--;
  }

  char* query = pendingLaps[(pendingFirst + pendingCount) % PENDING_LAPS_MAX];
  query[0] = '\0';
  pendingCount++;
  return query;
}


// Takes the current values as the finished lap and puts it in the queue.
static void queueLap() {
  char* query = newPendingLap();

  appendQuery(query, QUERY_SIZE, "engine_oil_temp", MON_ENGINE_OIL_TEMP,     1);
  appendQuery(query, QUERY_SIZE, "coolant_temp",    MON_COOLANT_TEMP,        1);
  appendQuery(query, QUERY_SIZE, "prev_lap_number", MON_PREVIOUS_LAP_NUMBER, 0);
  appendQuery(query, QUERY_SIZE, "prev_lap_time",   MON_PREVIOUS_LAP_TIME,   2);
  appendQuery(query, QUERY_SIZE, "fuel_level",      MON_FUEL_LEVEL,          1);
}


// One upload attempt. Only a 200 from update.php counts as delivered.
static boolean sendLap(const char* query) {
  snprintf(buff, BUFFSIZE, "%s?key=%s&%s", updateUrl, uploadKey, query);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(buff)) {
    return false;
  }
  const int code = http.GET();
  http.end();
  Serial.print(buff);
  Serial.print(" -> ");
  Serial.println(code);
  return code == HTTP_CODE_OK;
}


// Sends the oldest queued lap, if WiFi is up and a recent attempt did not just
// fail. One lap per call, so a long backlog does not stall the lap watching.
static void sendPendingLaps() {
  if (WiFi.status() != WL_CONNECTED) {
    // Auto reconnect gives up on some failures, for example when the hotspot
    // was not there at power on, so give it a nudge now and then.
    if (millis() - lastWifiRetry >= WIFI_RETRY_MS) {
      lastWifiRetry = millis();
      WiFi.reconnect();
    }
    return;
  }
  if (pendingCount == 0) {
    return;
  }
  if (uploadFailed && millis() - lastUploadFailure < UPLOAD_RETRY_MS) {
    return;
  }

  if (sendLap(pendingLaps[pendingFirst])) {
    pendingFirst = (pendingFirst + 1) % PENDING_LAPS_MAX;
    pendingCount--;
    uploadFailed = false;
  } else {
    uploadFailed = true;
    lastUploadFailure = millis();
  }
}


// Lap tracking. lastLapNumber is -1 until a lap number is first seen; a lap
// that had already finished at that point becomes the baseline instead of being
// logged. 0 means "no lap has finished yet", so the first one is uploaded.
static int32_t lastLapNumber = -1;
static int32_t lastLapTime = 0;
static int32_t candidateLapNumber = -1;
static int32_t candidateLapTime = 0;
static uint8_t candidateReads = 0;


// Watches for a newly finished lap and queues it. appReady is rcmonitor().
static void watchLaps(boolean appReady) {
  if (!appReady || !rcmonitorHasValue(MON_PREVIOUS_LAP_NUMBER)) {
    // No lap number yet; forget any half confirmed lap.
    candidateReads = 0;
    return;
  }

  const int32_t lapNumber = monitorValues[MON_PREVIOUS_LAP_NUMBER];

  // previous_lap_number is 0 until the first lap has been completed. Treat that
  // as the starting point, so the first lap of a fresh session is uploaded. The
  // baseline branch below only swallows a lap that had already finished before
  // the device started watching.
  if (lapNumber <= 0) {
    lastLapNumber  = 0;
    lastLapTime    = 0;
    candidateReads = 0;
    return;
  }

  if (lastLapNumber < 0) {
    lastLapNumber = lapNumber;
    lastLapTime   = rcmonitorHasValue(MON_PREVIOUS_LAP_TIME) ? monitorValues[MON_PREVIOUS_LAP_TIME] : INVALID_VALUE;
    return;
  }

  if (!rcmonitorHasValue(MON_PREVIOUS_LAP_TIME)) {
    candidateReads = 0;
    return;
  }

  const int32_t lapTime = monitorValues[MON_PREVIOUS_LAP_TIME];

  // The app can publish the new lap number a moment before the new lap time,
  // so a changed number on its own is not enough: the old time would then be
  // logged against the new lap. The time has to move as well. Consecutive laps
  // with exactly the same time to the centisecond are therefore skipped, which
  // is a far better trade than logging a wrong time.
  if (lapNumber != lastLapNumber && lapTime != lastLapTime) {
    if (candidateReads > 0 && candidateLapNumber == lapNumber && candidateLapTime == lapTime) {
      candidateReads++;
    } else {
      candidateLapNumber = lapNumber;
      candidateLapTime   = lapTime;
      candidateReads     = 1;
    }

    if (candidateReads >= LAP_CONFIRM_READS) {
      queueLap();
      lastLapNumber  = lapNumber;
      lastLapTime    = lapTime;
      candidateReads = 0;
    }
  } else {
    candidateReads = 0;
  }
}


#if TEST_MODE

// Test laps, see TEST_MODE in config.h. Numbered from 1, with the measured time
// since the previous test lap as the lap time.
static uint32_t testLapNumber = 0;
static boolean testLapTimerRunning = false;
static unsigned long testLapStart = 0;

static void printMonitor(const char* label, uint8_t monitorId, int decimals) {
  Serial.print(label);
  if (rcmonitorHasValue(monitorId)) {
    Serial.print(rcmonitorValue(monitorId), decimals);
  } else {
    Serial.print("-");
  }
}

static void watchTestLaps(boolean appReady) {
  if (!appReady) {
    // The first test lap comes one interval after the app is ready.
    testLapTimerRunning = false;
    return;
  }
  if (!testLapTimerRunning) {
    testLapTimerRunning = true;
    testLapStart = millis();
    return;
  }
  const unsigned long elapsed = millis() - testLapStart;
  if (elapsed < TEST_LAP_INTERVAL_MS) {
    return;
  }
  testLapStart += elapsed;
  testLapNumber++;

  char* query = newPendingLap();
  appendQuery(query, QUERY_SIZE, "engine_oil_temp", MON_ENGINE_OIL_TEMP, 1);
  appendQuery(query, QUERY_SIZE, "coolant_temp",    MON_COOLANT_TEMP,    1);
  appendQuery(query, QUERY_SIZE, "fuel_level",      MON_FUEL_LEVEL,      1);
  const size_t used = strlen(query);
  snprintf(query + used, QUERY_SIZE - used, "%sprev_lap_number=%lu&prev_lap_time=%.2f&test=1",
           used == 0 ? "" : "&", (unsigned long) testLapNumber, elapsed / 1000.0);

  // What has arrived from the app so far. "-" means nothing yet: check the OBD
  // connection for the temperatures and fuel; the lap values only come once
  // RaceChrono has timed a real lap.
  Serial.print("Test lap ");
  Serial.print(testLapNumber);
  Serial.print(" queued. From the app:");
  printMonitor(" oil=",     MON_ENGINE_OIL_TEMP,     1);
  printMonitor(" coolant=", MON_COOLANT_TEMP,        1);
  printMonitor(" fuel=",    MON_FUEL_LEVEL,          1);
  printMonitor(" lap=",     MON_PREVIOUS_LAP_NUMBER, 0);
  printMonitor(" laptime=", MON_PREVIOUS_LAP_TIME,   2);
  Serial.println();
}

#endif


void loop() {
  const boolean appReady = rcmonitor();
  watchLaps(appReady);
#if TEST_MODE
  watchTestLaps(appReady);
#endif
  sendPendingLaps();
  delay(POLL_INTERVAL_MS);
}
