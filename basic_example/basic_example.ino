// Built and tested with ESP32 Arduino core 3.3.x and NimBLE-Arduino 2.5.2.
// Do not use core 4.0.0-RC1 on ESP32-S3: it crashes at startup in the
// Bluetooth controller (see the README).

#include <Arduino.h>
#include "rcmonitor.h"            // https://github.com/aollin/racechrono-ble-diy-device
#include <WiFi.h>
#include <HTTPClient.h>



const char* ssid = "xxxxxxxxxx";
const char* password = "xxxxxxxxxxxxxxx";


struct strmon monitors[] = {
    {"engine_oil_temp", "channel(device(obd), engine_oil_temp)", 1.0},
    {"coolant_temp", "channel(device(obd), coolant_temp)", 1.0},
};

#define BUFFSIZE 500
char buff[BUFFSIZE];
HTTPClient http;


void setup() {
  Serial.begin(115200);   
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  rcmonitorstart();
}


void loop() {
  static float engine_oil_temp;
  static float coolant_temp;
  // rcmonitor() is true once the app has accepted the monitors; the values
  // themselves may take a moment longer to arrive.
  if( rcmonitor() && rcmonitorHasValue(0) && rcmonitorHasValue(1) ) {
    engine_oil_temp   = rcmonitorValue(0);
    coolant_temp      = rcmonitorValue(1);
    snprintf( buff, BUFFSIZE, "http://www.mywebserversomewhere.com/somepath/update.php?engine_oil_temp=%f&coolant_temp=%f", engine_oil_temp, coolant_temp ); 
    http.begin(buff); 
    http.GET();
    http.end();
    Serial.println(buff);   
  }
  delay(2000);
}


/*

The webpage to send updates to:

<?php
  if(!empty($_GET["engine_oil_temp"]) && !empty($_GET["coolant_temp"])) {
    $engine_oil_temp = preg_replace('/[^0-9.]/', '', $_GET['engine_oil_temp']);
    $coolant_temp = preg_replace('/[^0-9.]/', '', $_GET['coolant_temp']);
    $csvData = array(date("Ymd H:i:s"), $engine_oil_temp, $coolant_temp);
    $fp = fopen("datalog.csv","a"); 
    if($fp)    {  
        fputcsv($fp,$csvData); 
        fclose($fp); 
    }
  }
?>

Then add a web page that reads the datalog file and displays it

*/
