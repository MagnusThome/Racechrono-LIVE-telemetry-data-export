#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"     // https://github.com/adafruit/Adafruit_ILI9341
#include "rcmonitor.h"            // https://github.com/aollin/racechrono-ble-diy-device

//#define DUMMYTEST   // For testing, fetch magnetometer data instead of time and speed gain


// ESP32 SPI: 
// MISO  GPIO19
// MOSI  GPIO19
// CLK   GPIO18
// SS    GPIO5
#define TFT_CS 5
#define TFT_DS 22
#define TFT_RST 21

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DS, TFT_RST); 


// Data to fecth from Racechrono
struct strmon monitors[] = {
#ifdef DUMMYTEST
    {"x_magnetic_field", "channel(device(magn), x_magnetic_field)", 1.0},
    {"y_magnetic_field", "channel(device(magn), y_magnetic_field)", 1.0}
#else
    {"Time Delta",        "channel(device(lap), delta_lap_time)*10.0", 0.1},
    {"Speed Gain",       "channel(device(gps), delta_speed)*10.0", 0.1}
#endif
};


#define DIGITSIZE 7
uint16_t totwidth;
uint16_t totheight;


void timegainPrint(float gain);
void speedgainPrint(int16_t gain);
void printFloat(float gain, uint16_t txcolor, uint16_t bgcolor);
void printInt(uint16_t gain, uint16_t txcolor, uint16_t bgcolor);
void initscreen(void);
void bootscreensequence(void);



void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nBooting...");
  initscreen();
  bootscreensequence();
  rcmonitorstart();
}


void loop() {
  static float timegain;
  static int16_t speedgain;
  if( rcmonitor() ) {
    timegain = monitorValues[0] * monitorMultipliers[0];
    speedgain = (int16_t) (monitorValues[1] * monitorMultipliers[1]);
    timegainPrint(timegain);
    speedgainPrint(speedgain);
  }
  delay(900);
}


void timegainPrint(float gain) {
    static uint16_t txcolor;
    static uint16_t bgcolor;
    if(gain>=0) { 
        txcolor = ILI9341_WHITE;
        bgcolor = ILI9341_GREEN; 
    }
    else if(gain>-2) { 
        txcolor = ILI9341_RED;
        bgcolor = ILI9341_BLACK; 
    } 
    else { 
        txcolor = ILI9341_WHITE;
        bgcolor = ILI9341_RED; 
    }
    printFloat(gain, txcolor, bgcolor);
}


void speedgainPrint(int16_t gain) {
    static uint16_t txcolor;
    static uint16_t bgcolor;
    if(gain>=0) { 
        txcolor = ILI9341_WHITE;
        bgcolor = ILI9341_GREEN; 
    }
    else if(gain>-10) { 
        txcolor = ILI9341_RED;
        bgcolor = ILI9341_BLACK; 
    } 
    else { 
        txcolor = ILI9341_WHITE;
        bgcolor = ILI9341_RED; 
    }
    printInt(gain, txcolor, bgcolor);
}


void printFloat(float gain, uint16_t txcolor, uint16_t bgcolor) {
    const uint8_t XPOS = 34;
    const uint8_t YPOS = 38;
    static uint16_t oldcolor;
    Serial.println(gain, 1);
    tft.setTextSize(DIGITSIZE);
    tft.setTextColor(txcolor, bgcolor);  
    if(oldcolor != bgcolor) {
      tft.fillRect(0, 0, totwidth, (totheight/2)-1, bgcolor);
      yield();
      oldcolor = bgcolor;
    }
    tft.setCursor(XPOS, YPOS);
    if ( gain <= -100 || gain >= 1000  ) {
      tft.print("over  ");
      return;
    }
    tft.printf("%5.1f",gain);
    tft.fillRect(XPOS+126, YPOS+48, 30, -30, bgcolor);    // erase dot
    tft.fillRect(XPOS+126, YPOS+48, 8, -8, txcolor);      // create new dot
    tft.setCursor(XPOS+143, YPOS);                        // prepare to overwrite decimal digit
    tft.print((int16_t)abs(((gain-(int16_t)gain)*10)));   // overwrite decimal digit with same number but slightly to the left
    tft.print(" ");                                       // clear any leftover parts of old decimal digit 
}                                                         // All this to get a tighter kerning between the numbers on screen. Looks better :-)


void printInt(int16_t gain, uint16_t txcolor, uint16_t bgcolor) {
    const uint8_t XPOS = 38;
    const uint8_t YPOS = 155;
    static uint16_t oldcolor;
    Serial.println(gain);
    tft.setTextSize(DIGITSIZE);
    tft.setTextColor(txcolor, bgcolor);  
    if(oldcolor != bgcolor) {
      tft.fillRect(0, (totheight/2)+1, totwidth, totheight, bgcolor);
      yield();
      oldcolor = bgcolor;
    }
    tft.setCursor(XPOS, YPOS);
    if ( gain <= -100 || gain >= 1000  ) {
      tft.print("over  ");
      return;
    }
    tft.printf("%4d", gain);
}


void initscreen(void) {
  tft.begin();
  tft.setRotation(3);
  totwidth = tft.width();
  totheight = tft.height();
  Serial.print("totwidth: ");
  Serial.println(totwidth);
  Serial.print("totheight: ");
  Serial.println(totheight);
}


void bootscreensequence(void) {
  tft.setTextSize(2);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);  
  tft.setCursor( 103, 90 );
  tft.println("Racechrono");
  tft.setCursor( 86, 130 );
  tft.println("with RejsaCAN");
  delay(3000);
  tft.fillScreen(ILI9341_WHITE);
  tft.fillRect(0, 0, totwidth, (totheight/2)-1, ILI9341_BLACK);
  tft.fillRect(0, (totheight/2), totwidth, totheight, ILI9341_BLACK);
  tft.setTextSize(4);
  tft.setTextColor(ILI9341_RED);  
  tft.setCursor( 52, 45 );
  tft.println("TIME GAIN");
  tft.setCursor( 40, 160 );
  tft.println("SPEED GAIN");
  delay(1600);
  tft.setTextColor(ILI9341_GREEN);  
  tft.setCursor( 40, 160 );
  tft.println("SPEED GAIN");
  delay(1200);
  tft.setCursor( 52, 45 );
  tft.println("TIME GAIN");
  delay(2300);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_RED);  
  tft.setTextSize(2);
  tft.setCursor( 26, 90 );
  tft.println("Waiting for connection");
  tft.setCursor( 60, 130 );
  tft.println("from Racechrono");
}


// end
