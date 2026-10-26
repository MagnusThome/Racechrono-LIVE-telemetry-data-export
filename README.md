# Racechrono-LIVE-telemetry-data-export  

An example how to use the "Monitor" API in Racechrono to get a live feed of any of all data Racechrono holds using <a target=_blank href=https://github.com/MagnusThome/RejsaCAN-ESP32>a RejsaCAN board</a> running on 12V from the car (or any board based on an ESP32 or NRF52 MCU but normally running on 5V) to get a live feed of data from Racechrono over BLE/Bluetooth.
  
# Data to display   

The list of data that you can get live from Racechrono is huge! Any OBD2 data, any laptiming data, GPS, CAN, yeah ANY data Racechrono collects is available to collect live over BLE Bluetooth while out on the track. This specific example code will upload data to a web server. It's easy to add any of all the data available. Then add your code for what to do with the data/information. Show for example drive train vitals and the current sector times on a large screen in the pits? Or issue an alarm that goes of in the car or in the pits if the oil or tire temps are too high? An automatic email sent to your friends each time you set a new track PB hehehe. The possibilities are endless.  

# How it works  

RaceChrono (phone) → Bluetooth LE → ESP32 → WiFi → `update.php` on your web server → `datalog.csv` → `index.php` web page.  

The ESP32 asks RaceChrono for engine oil temperature, coolant temperature and the number and time of the last completed lap. Each time a lap is completed, it uploads one row. Laps are queued on the device and retried until the server has accepted them, so a WiFi dropout only delays them. The web page shows the laps, newest first, and refreshes itself every few seconds.  

# Install  

These steps are for `full_example`: the sketch is in `full_example/arduino` and the web files in `full_example/web`.  

You need:  
- An ESP32 board, for example a RejsaCAN board.  
- RaceChrono on your phone.  
- WiFi for the ESP32 at the track, for example a phone hotspot.  
- A web server with PHP 7.4 or newer.  

## 1. Choose a secret key  

Pick a key of letters and digits only. The device sends it with every lap, and the web page asks for it before clearing the log. The same key goes in both config files below.  

## 2. Web server  

1. Copy `update.php`, `index.php` and `config.php` from `full_example/web` to a folder on your web server. The web server only needs to read these files, so don't give it write access to that folder.  
2. Create a separate folder for the log file and make it writable by the web server. By default `config.php` uses a folder named `data` inside the folder with the PHP files (`__DIR__ . '/data'`), so the simplest setup is to create that subfolder:  
   ```
   mkdir /var/www/html/racechrono/data
   sudo chown www-data /var/www/html/racechrono/data
   ```
   (here the PHP files are in `/var/www/html/racechrono`, and Apache or nginx runs as `www-data`). A folder outside the web root is safer, because the log then can't be downloaded directly, for example `/home/me/racechrono-data`. In that case set `$dataDir` to it in the next step.  
3. Edit `config.php`:  
   - `$secretKey`: your key. Nothing is accepted while it is still `xxxxxxxxxxxxxxxx`.  
   - `$dataDir`: the server path of the log folder. Leave it as `__DIR__ . '/data'` for the default `data` subfolder, or set a full path such as `/home/me/racechrono-data`. This is a path on the server's disk, not a URL.  
   - `$timezone`: the timezone for the timestamps in the log, for example `Europe/Stockholm`.  
4. Open `index.php` in a browser. It should say that no laps have been logged yet.  

## 3. ESP32  

1. In the Arduino IDE, install the ESP32 boards (Boards Manager → "esp32" by Espressif) and the library **NimBLE-Arduino version 2.x** (Library Manager). Version 1.x will not compile.  
2. Open `full_example/arduino/arduino.ino` and edit `full_example/arduino/config.h`:  
   - `ssid` and `password`: the WiFi the ESP32 should use.  
   - `updateUrl`: the web address of `update.php`, for example `http://www.example.com/racechrono/update.php`. The key is sent as part of this address, so use `https://` if your server supports it.  
   - `uploadKey`: your key, the same as `$secretKey` in `config.php`.  
3. Select your board under Tools → Board, for example **ESP32S3 Dev Module** or **ESP32 Dev Module**. WiFi and Bluetooth together fill almost all of the default program space (about 95% on ESP32-S3, over 100% on a plain ESP32), so also choose Tools → Partition Scheme → **Huge APP**.  
4. Upload, and open the Serial Monitor at 115200 baud to follow what the device does.  

### ESP32 core 4.0.0-RC1 on ESP32-S3: NimBLE fix  

With ESP32 core 4.0.0-RC1, ESP32-S3 builds stop with this error inside NimBLE-Arduino (2.5.2 and earlier):  

```
ble_hs_periodic_sync.c:58:24: error: 'struct ble_npl_event' has no member named 'event'
```

The error is in the library, not in this project: RC1 turns on Bluetooth 5 periodic sync for the S3, and that code in NimBLE-Arduino does not compile for the S3. Until a NimBLE-Arduino release fixes it, edit `NimBLE-Arduino/src/nimble/nimble/host/src/ble_hs_periodic_sync.c` in your Arduino libraries folder and replace these two lines near line 58:  

```c
    if((psync->lost_ev).event != NULL)
        ble_npl_event_deinit(&psync->lost_ev);
```

with:  

```c
#if CONFIG_BT_LE_CONTROLLER_NPL_OS_PORTING_SUPPORT
    if((psync->lost_ev).event != NULL)
        ble_npl_event_deinit(&psync->lost_ev);
#else
    ble_npl_event_deinit(&psync->lost_ev);
#endif
```

Updating NimBLE-Arduino in the Library Manager overwrites this change, so check again after an update. Plain ESP32 boards and older cores do not need it.  

## 4. RaceChrono  

Add the ESP32 as a DIY Bluetooth device in RaceChrono's device settings. It shows up as **RC DIY #xxxx**, where xxxx is the end of the board's Bluetooth address. When RaceChrono connects, the Serial Monitor prints `Configuring... Done`.  

The temperatures come from RaceChrono's OBD-II connection, so RaceChrono also needs an OBD-II reader connected to the car. Lap numbers and times need lap timing on a track in RaceChrono.  

## 5. Test without driving  

Set `#define TEST_MODE 1` in `full_example/arduino/config.h` and upload again. While RaceChrono is connected, the device then makes a test lap every 20 seconds, with the live temperatures and a made-up lap number and time, and uploads it like a real lap. The Serial Monitor shows each test lap and the server's answer:  
- **200**: stored. The row appears on the web page with a TEST tag.  
- **403**: the keys in `config.h` and `config.php` don't match.  
- **500**: the server can't write to `$dataDir`.  
- **-1** or another negative number: no connection to the server. Check the WiFi details and `updateUrl`.  

Afterwards, set `TEST_MODE` back to `0`, upload again, and remove the test rows with **Clear log** on the web page.  

##  

More info here:
https://racechrono.com/forum/




