# Racechrono-LIVE-telemetry-data-export  

An example how to use the "Monitor" API in Racechrono to get a live feed of any of all data Racechrono holds using <a target=_blank href=https://github.com/MagnusThome/RejsaCAN-ESP32>a RejsaCAN board</a> (or any board based on an ESP32 or NRF52 MCU) to get a live feed of data from Racechrono over BLE/Bluetooth.

This example code is made for a commonly available ILI9341 based display simply connected via SPI

![138956181-5c421461-7e94-4a66-8d21-c0f54506565e](https://user-images.githubusercontent.com/32169384/139599606-2ad71cbc-2545-4b49-b409-847e96763dfc.png)  
  
# Data to display   

The list of data that you can get live from Racechrono is huge! Any OBD2 data, any laptiming data, GPS, CAN, yeah ANY data Racechrono collects is available to collect live over BLE Bluetooth while out on the track. This specific example code will show on the attached display if you have a faster speed than your PB speed at any point along the track plus the same with time gain along the track vs your PB. In red and green depending on how you are doing of course. This is exactly what you can have Racechrono display on the mobile phone so nothing new there in itself, but fetched over to the RejsaCAN you can display it on a larger screen in the car or in the pits(!) or send it over internet to a web server. It's easy to add any of all the data available. Then add your code for what to do with the data/information. Show for example drive train vitals and the current sector times on a large screen in the pits? Or issue an alarm that goes of in the car or in the pits if the oil or tire temps are too high? An automatic email sent to your friends each time you set a new track PB hehehe. The possibilities are endless.  

##  

<a href="https://www.youtube.com/watch?v=f61Pw1ZjPyw"><img src=https://user-images.githubusercontent.com/32169384/139599643-c06efdc9-39f7-4c51-9084-7b14ee37d968.png></a>


More info here:
https://racechrono.com/forum/




