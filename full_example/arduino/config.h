#ifndef config_h
#define config_h

// Settings for your own setup. Fill these in before uploading the sketch.

// WiFi the device uploads through, for example a phone hotspot.
const char* ssid = "xxxxxxxxxx";
const char* password = "xxxxxxxxxxxxxxx";

// Address of update.php on your web server.
const char* updateUrl = "http://www.mywebserversomewhere.com/somepath/update.php";

// Must match $secretKey in config.php on the server. Letters and digits only,
// it is sent as is in the URL.
const char* uploadKey = "xxxxxxxxxxxxxxxx";

// Test mode, for checking the whole chain (RaceChrono -> ESP32 -> update.php ->
// web page) without driving. While the app is connected and has accepted the
// monitors, a test lap is made every TEST_LAP_INTERVAL_MS with the live
// temperatures, and the web page tags it as a test. Real laps are still logged
// as usual. 1 = on, 0 = off (normal use).
#define TEST_MODE 0
#define TEST_LAP_INTERVAL_MS 20000

#endif
