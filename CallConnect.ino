// CallConnect
// Author: John B Damask
// Created: January 10, 2019
#include <Adafruit_NeoPixel.h>
#include "Adafruit_BluefruitLE_SPI.h"
#include "BluefruitConfig.h"

#define FACTORYRESET_ENABLE     1
#define PINforControl   6 // pin connected to the small NeoPixels strip
#define BUTTON          10
#define NUMPIXELS1      13 // number of LEDs on strip
#define BRIGHTNESS      30 // Max brightness of NeoPixels
#define BLE_CHECK_INTERVAL  300 // Time interval for checking ble messages
#define DEVICE_NAME     "AT+GAPDEVNAME=TouchLightsBle"
unsigned long patternInterval = 20 ; // time between steps in the pattern
unsigned long lastUpdate = 0 ; // for millis() when last update occurred
unsigned long lastBleCheck = 0; // for millis() when last ble check occurred
/* Each animation should have a value in this array */ 
unsigned long animationSpeed [] = { 100, 50, 2 } ; // speed for each animation (order counts!)
#define ANIMATIONS sizeof(animationSpeed) / sizeof(animationSpeed[0])
// Colors for sparkle
uint8_t myFavoriteColors[][3] = {{200,   0, 200},   // purple
                                 {200,   0,   0},   // red 
                                 {200, 200, 200},   // white
                               };
#define FAVCOLORS sizeof(myFavoriteColors) / 3


// BLE stuff
uint8_t len = 0;
bool printOnceBle = false; // BLE initialization 

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS1, PINforControl, NEO_GRB + NEO_KHZ800);
Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);

// function prototypes over in packetparser.cpp
uint8_t readPacket(Adafruit_BLE *ble, uint16_t timeout);
float parsefloat(uint8_t *buffer);
void printHex(const uint8_t * data, const uint32_t numBytes);

// the ble packet buffer
extern uint8_t packetbuffer[];
// the defined length of a color payload
int colorLength = 4;
uint16_t colLen = 3;
// Payload stuff
uint8_t xsum = 0;
uint8_t PAYLOAD_START = "!";
uint8_t COLOR_CODE = "C";
uint8_t BUTTON_CODE = "B";
// the ble payload, set to max buffer size
uint8_t payload[21];

/**************************************************************************/
/*!
    Setting everything up
*/
/**************************************************************************/
void setup() {
  delay(500);
  Serial.begin(9600);
  strip.setBrightness(BRIGHTNESS); // These things are bright!
  strip.begin(); // This initializes the NeoPixel library.
  wipe(); // wipes the LED buffers
  strip.show();
  pinMode(BUTTON, INPUT_PULLUP); // change pattern button
  /* Initialise the module */
  Serial.print(F("Initialising the Bluefruit LE module: "));
  if ( !ble.begin(VERBOSE_MODE) )
  {
    error(F("Couldn't find Bluefruit, make sure it's in CoMmanD mode & check wiring?"));
  }
  Serial.println( F("OK!") );

  if ( FACTORYRESET_ENABLE )
  {
    /* Perform a factory reset to make sure everything is in a known state */
    Serial.println(F("Performing a factory reset: "));
    if ( ! ble.factoryReset() ){
      error(F("Couldn't factory reset"));
    }
  }
  // Customize name. THIS IS IMPORTANT SO THAT MY PIHUBs AUTOMATICALLY CONNECT!
  ble.println(DEVICE_NAME);
  // Wait until bluetooth connected
  while (! ble.isConnected()) delay(500);
  ble.setMode(BLUEFRUIT_MODE_DATA);
}


/**************************************************************************/
/*!
    Action Jackson, baby
*/
/**************************************************************************/
void loop() {
  static int pattern = 0, lastReading;
  static bool gotBleMessage = false;
  int reading = digitalRead(BUTTON);
  bool buttonPushed = (lastReading == HIGH && reading == LOW);
  // ble checks are slow. Too many and LED animations won't look good
  // The BLE_READPACKET_TIMEOUT in BluefruitConfig.h is set to 50 ms by default. May need tweaking
  if(millis() - lastBleCheck > BLE_CHECK_INTERVAL) {
    (readPacket(&ble, BLE_READPACKET_TIMEOUT) != 0) ? 1 : 0;
    lastBleCheck = millis();
  }

  if( (buttonPushed || gotBleMessage) && !(buttonPushed && gotBleMessage) ){
    pattern++;
    if(pattern > ANIMATIONS-1) pattern = 0; // wrap round if too big
    patternInterval = animationSpeed[pattern]; // set speed for this animation
    Serial.println(patternInterval);
    wipe();
    resetBrightness();
    delay(50); // debounce delay      
  }
  
  lastReading = reading; // save for next time
  if(millis() - lastUpdate > patternInterval) { 
    updatePattern(pattern);
  }
}


// Update the animation
void  updatePattern(int pat){ 
  switch(pat) {
    case 0:
      wipe();
      strip.show();
      break;
    case 1: 
      wipe();
      sparkle(3);
      break;     
    case 2:
      breatheBlue();
      break;
  }  
}

// LED breathing. Used for when devices are connected to one another
void breatheBlue() { 
  float SpeedFactor = 0.008; 
  static int i = 0;
  // Make the lights breathe
  float intensity = BRIGHTNESS /2.0 * (1.0 + sin(SpeedFactor * i));
  strip.setBrightness(intensity);
  for (int j=0; j<strip.numPixels(); j++) {
    strip.setPixelColor(j, 0, 127, 127);
  }
  strip.show();
  i++;
  if(i >= 65535){
    i = 0;
  }
  lastUpdate = millis();
}

// LED sparkling. Used for when devices are "calling"
void sparkle(uint8_t howmany) {
  static int x = 0;
  static bool goingUp = true;
  
  for(uint16_t i=0; i<howmany; i++) {
    // pick a random favorite color!
    int c = random(FAVCOLORS);
    int red = myFavoriteColors[c][0];
    int green = myFavoriteColors[c][1];
    int blue = myFavoriteColors[c][2]; 

    // get a random pixel from the list
    int j = random(strip.numPixels());
    
    // now we will 'fade' it in 5 steps
    if(goingUp){
      if(x < 5) {
        x++;
      } else {
        goingUp = false;
      }
    } else {
      if(x>0){
        x--;
      } else {
        goingUp = true;
      }     
    }

    int r = red * (x+1); r /= 5;
    int g = green * (x+1); g /= 5;
    int b = blue * (x+1); b /= 5;      
    strip.setPixelColor(j, strip.Color(r,g,b));      
    strip.show();
  }
  lastUpdate = millis();
}


// clear all LEDs
void wipe(){ 
   for(int i=0;i<strip.numPixels();i++){
     strip.setPixelColor(i, strip.Color(0,0,0)); 
   }
}


void resetBrightness(){
  strip.setBrightness(BRIGHTNESS);
}

// A small helper
void error(const __FlashStringHelper*err) {
  Serial.println(err);
  while (1);
}


