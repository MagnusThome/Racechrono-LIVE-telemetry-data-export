# Racechrono-LIVE-telemetry-data-export  

An example how to use the "Monitor" API in Racechrono to get a live feed of any of all data Racechrono holds using <a target=_blank href=https://github.com/MagnusThome/RejsaCAN-ESP32>a RejsaCAN board</a> running on 12V from the car (or any board based on an ESP32 or NRF52 MCU but normally running on 5V) to get a live feed of data from Racechrono over BLE/Bluetooth.
  
# Data to display   

The list of data that you can get live from Racechrono is huge! Any OBD2 data, any laptiming data, GPS, CAN, yeah ANY data Racechrono collects is available to collect live over BLE Bluetooth while out on the track. This specific example code will upload data to a web server. It's easy to add any of all the data available. Then add your code for what to do with the data/information. Show for example drive train vitals and the current sector times on a large screen in the pits? Or issue an alarm that goes of in the car or in the pits if the oil or tire temps are too high? An automatic email sent to your friends each time you set a new track PB hehehe. The possibilities are endless.  

# How it works  

RaceChrono (phone) → Bluetooth LE → ESP32 → WiFi → `update.php` on your web server → `datalog.csv` → `index.php` web page.  

The ESP32 asks RaceChrono for engine oil temperature, coolant temperature and the number and time of the last completed lap. Each time a lap is completed, it uploads one row. Laps are queued on the device and retried until the server has accepted them, so a WiFi dropout only delays them. The web page shows the laps, newest first, and refreshes itself every few seconds.  

<img width="500" height="825" alt="image" src="https://github.com/user-attachments/assets/1d703923-10c7-408e-b2a5-afd69288ff40" />


# Install  

These steps are for `full_example`: the sketch is in `full_example/arduino` and the web files in `full_example/web`.  

You need:  
- An ESP32 board, for example a RejsaCAN board.  
- RaceChrono on your phone.  
- WiFi for the ESP32 at the track, preferably a phone hotspot on the phone you are running Racechrono on in the car.  
- A web server with PHP 7.4 or newer.  

## 1. Choose a secret key  

Pick a key of letters and digits only. The device sends it with every lap, and the web page asks for it before clearing the log. The same key goes in both config files below.  

## 2. Web server  

1. Copy `update.php`, `index.php` and `config.php` from `full_example/web` to a folder on your web server. Only `config.php` holds settings; the other two need no editing. The web server only needs to read these files, so don't give it write access to that folder.  
2. Next to the PHP files, create a subfolder named `data` for the log file, and make it writable by the web server. For example, with the PHP files in `/var/www/html/racechrono` and Apache or nginx running as `www-data`:  
   ```
   mkdir /var/www/html/racechrono/data
   sudo chown www-data /var/www/html/racechrono/data
   ```
3. Edit `config.php`:  
   - `$secretKey`: your key.  
   - `$timezone`: the timezone for the timestamps in the log, for example `Europe/Stockholm`.  
4. Open `index.php` in a browser. It should say that no laps have been logged yet.  

## 3. ESP32  

1. In the Arduino IDE, install the ESP32 boards, version 3.3.x (Boards Manager → "esp32" by Espressif, see [ESP32 core version](#esp32-core-version) below) and the library **NimBLE-Arduino version 2.x** (Library Manager). Version 1.x will not compile.  
2. Open `full_example/arduino/arduino.ino` and edit `full_example/arduino/config.h`:  
   - `ssid` and `password`: the WiFi the ESP32 should use.  
   - `updateUrl`: the web address of `update.php`, for example `http://www.example.com/racechrono/update.php`. The key is sent as part of this address, so use `https://` if your server supports it.  
   - `uploadKey`: your key, the same as `$secretKey` in `config.php`.  
3. Select your board under Tools → Board, for example **ESP32S3 Dev Module** or **ESP32 Dev Module**. WiFi and Bluetooth together fill almost all of the default program space (over 90% on ESP32-S3, about 100% on a plain ESP32), so also choose Tools → Partition Scheme → **Huge APP**.  
4. Upload, and open the Serial Monitor at 115200 baud to follow what the device does.  

### ESP32 core version  

Use the stable **3.3.x** release of the ESP32 boards. Do not use **4.0.0-RC1** (or the 4.0 alphas) on an ESP32-S3 for now: with NimBLE-Arduino 2.5.2 the board crashes and reboots over and over, about a second after power-on, inside the Bluetooth controller's own startup code:  

```
Guru Meditation Error: Core  / panic'ed (Cache error).
MMU entry fault error
```

The backtrace goes `setup()` → `NimBLEDevice::init()` → `esp_bt_controller_init()`, so it happens before this project's code does anything. With RC1, S3 builds also fail to compile in NimBLE-Arduino (`'struct ble_npl_event' has no member named 'event'` in `ble_hs_periodic_sync.c`). Fixing that line by hand only gets it to build; the crash remains. Both are version mismatches between the 4.0 core and NimBLE-Arduino 2.x, which is made for the 3.x cores. Check again once NimBLE-Arduino supports core 4.0.  

## 4. RaceChrono  

Add the ESP32 as a DIY Bluetooth device in RaceChrono's device settings. It shows up as **RC DIY #xxxx**, where xxxx is the end of the board's Bluetooth address. When RaceChrono connects, the Serial Monitor prints `Configuring... Done`.  

The temperatures come from RaceChrono's OBD-II connection, so RaceChrono also needs an OBD-II reader connected to the car. Lap numbers and times need lap timing on a track in RaceChrono.  

## 5. Test without driving  

Set `#define TEST_MODE 1` in `full_example/arduino/config.h` and upload again. While RaceChrono is connected, the device then makes a test lap every 20 seconds, with the live temperatures and a made-up lap number and time, and uploads it like a real lap. The Serial Monitor shows each test lap and the server's answer:  
- **200**: stored. The row appears on the web page with a TEST tag.  
- **403**: the keys in `config.h` and `config.php` don't match.  
- **500**: the server can't write to the `data` folder.  
- **-1** or another negative number: no connection to the server. Check the WiFi details and `updateUrl`.  

Afterwards, set `TEST_MODE` back to `0`, upload again, and remove the test rows with **Clear log** on the web page.  

##  

More info here:
https://racechrono.com/forum/




