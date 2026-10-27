# Racechrono-LIVE-telemetry-data-export  

An example how to use the "Monitor" API in Racechrono to get a live feed of data by using a cheap ESP32 board. 
  
# Data to display   

The list of data that you can get live from Racechrono is huge! Any OBD2 data, any laptiming data, GPS, CAN, yeah ANY data Racechrono collects is available to collect live over BLE Bluetooth while out on the track. This specific code example will upload data to a web server which then can be viewed in the pits. Or you can add your own code to locally drive a display or issue an alarm that goes of in the car or in the pits if the oil or tire temps are too high? An automatic email sent to your friends each time you set a new track PB ;-) The possibilities are endless.  

# How this example works  

RaceChrono (phone) → Bluetooth LE → ESP32 → WiFi (phone hotspot) → `update.php` on your web server → `datalog.csv` → `index.php` web page.  

The ESP32 asks RaceChrono for engine oil temperature, coolant temperature, fuel tank level (in %), the lap number, lap time and time of the last completed lap. Each time a lap is completed, it uploads one row. The web page shows the laps, newest first, and refreshes itself every few seconds.  

<img width="725" height="734" alt="image" src="https://github.com/user-attachments/assets/fdcbd169-c9ba-46d8-ac19-62d8b3bb9471" />

# Install  

These steps are for `full_example`: the sketch is in `full_example/arduino` and the web files in `full_example/web`.  

You need:  
- An ESP32 or ESPS32-S3 board 
- RaceChrono on your phone.  
- WiFi with internet access for the ESP32. Normally this is the WiFi hotspot on the phone that runs RaceChrono. The phone rides along in the car, so the ESP32 has a stable WiFi connection and internet access the whole time you drive.  
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
   - `ssid` and `password`: the WiFi the ESP32 should use, normally your phone's hotspot. Turn the hotspot on before you drive.  
   - `updateUrl`: the web address of `update.php`, for example `http://www.example.com/racechrono/update.php`. The key is sent as part of this address, so use `https://` if your server supports it.  
   - `uploadKey`: your key, the same as `$secretKey` in `config.php`.  
3. Select your board under Tools → Board, for example **ESP32S3 Dev Module** or **ESP32 Dev Module**. WiFi and Bluetooth together fill almost all of the default program space (over 90% on ESP32-S3, about 100% on a plain ESP32), so also choose Tools → Partition Scheme → **Huge APP**.  
4. Compile and upload  

### ESP32 board library version  

Use the stable **3.3.x** release of the ESP32 boards. Do not use **4.0.0-RC1** (or the 4.0 alphas) on an ESP32-S3 for now, with NimBLE-Arduino 2.5.2 the board crashes and reboots over and over. Updated later 4.x versions might fix this. 

## 4. RaceChrono  

Add the ESP32 as a DIY Bluetooth device in RaceChrono's device settings. It shows up as **RC DIY #xxxx**, where xxxx is the end of the board's Bluetooth address. When RaceChrono connects, the Serial Monitor prints `Configuring... Done`.  

The temperatures and the fuel level come from RaceChrono's OBD-II connection, so RaceChrono also needs an OBD-II reader connected to the car. Not every car reports the fuel level over OBD-II; if yours doesn't, the fuel field stays empty.  

**Don't forget:** RaceChrono only reads the OBD-II channels you have turned on. In the settings for your OBD-II reader in RaceChrono, add the channels for engine oil temperature, coolant temperature and fuel level, if your car supports them. A channel that isn't turned on never reaches the ESP32, and that field stays empty on the web page.  

Lap numbers and times need lap timing on a track in RaceChrono.  

## 5. Test without driving  

Set `#define TEST_MODE 1` in `full_example/arduino/config.h` and upload again. While RaceChrono is connected, the device then makes a test lap every 20 seconds, with the live temperatures and fuel level and a made-up lap number and time, and uploads it like a real lap. The Serial Monitor shows each test lap and the server's answer:  
- **200**: stored. The row appears on the web page with a TEST tag.  
- **403**: the keys in `config.h` and `config.php` don't match.  
- **500**: the server can't write to the `data` folder.  
- **-1** or another negative number: no connection to the server. Check the WiFi details and `updateUrl`.  

Afterwards, set `TEST_MODE` back to `0`, upload again, and remove the test rows with **Clear log** on the web page.  

##  

More info here:
https://racechrono.com/forum/




