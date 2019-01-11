// CallConnect
// Author: John B Damask
// Created: January 10, 2019
#include <Adafruit_NeoPixel.h>
#include "Adafruit_BluefruitLE_SPI.h"
#include "BluefruitConfig.h"
#include "SimpleTimer.h"

#define FACTORYRESET_ENABLE     1
#define PINforControl   6 // pin connected to the small NeoPixels strip
#define BUTTON          10
#define NUMPIXELS1      13 // number of LEDs on strip
#define BRIGHTNESS      30 // Max brightness of NeoPixels
#define BLE_CHECK_INTERVAL  300 // Time interval for checking ble messages
#define DEVICE_NAME     "AT+GAPDEVNAME=TouchLightsBle_upstairs"
#define PAYLOAD_LENGTH  4   // Array size of BLE payload
#define IDLE_TIMEOUT    5000   // Milliseconds that there can be no touch or ble input before reverting to idle state
unsigned long patternInterval = 20 ; // time between steps in the pattern
unsigned long lastUpdate = 0, idleTimer = 0; // for millis() when last update occurred
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

// Connection state (0 = idle; 1 = calling; 2 = connected)
uint8_t state = 0;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUMPIXELS1, PINforControl, NEO_GRB + NEO_KHZ800);
Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);

// function prototypes over in packetparser.cpp
uint8_t readPacket(Adafruit_BLE *ble, uint16_t timeout);
float parsefloat(uint8_t *buffer);
void printHex(const uint8_t * data, const uint32_t numBytes);

// the ble packet buffer
extern uint8_t packetbuffer[];
uint16_t colLen = 3;
// Payload stuff
uint8_t xsum = 0;
uint8_t PAYLOAD_START = "!";
uint8_t COLOR_CODE = "C";
uint8_t BUTTON_CODE = "B";
// the ble payload, set to max buffer size
uint8_t payload[21];

// Main timer object (we can have multiple timers within)
SimpleTimer timer;
int connectionTimerId;
// Manages connection state
bool isConnected = false;

// Button reading
//int reading;

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

  // Create timer that periodically checks connection state
  connectionTimerId = timer.setInterval(10000, checkConnection);
//  bleWrite(0); // Set initial state to 0 <--------- 1/11/19 this isn't working
}

/**************************************************************************/
/*!
    Action Jackson, baby
*/
/**************************************************************************/
void loop() {
 // static int pattern = 0;
  static uint8_t previousState = -1, previousBleState = 0;
  static bool makingCall = false; // When in state 1, we're either making or receiving a call
  static bool previouslyTouched = false;
  static bool justWokeUp = true;
  static bool bleReceived = false;
  int blePacketLength;
  static long countDown = 0;

  // Check for BLE message
 // if(millis() - lastBleCheck > BLE_CHECK_INTERVAL) {
    blePacketLength = readPacket(&ble, BLE_READPACKET_TIMEOUT); // Read a packet into the buffer
    if(justWokeUp){ // If device just came online, then ignore the first ble notice since this is likely stale
      justWokeUp = false;
      return; // Since we get the BLE message on the first pass (assuming PiHub is online and state exists), we ignore and move to next iteration
    } 
 //   lastBleCheck = millis();
 // }

  if(previousState != state) {
    Serial.print("State: "); Serial.println(state);
    previousState = state;
  }

  // The various cases we can face
  switch(state){
    case 0: // Idle
      if(isTouched()) {
        state = 1;
        bleWrite(1);
        previouslyTouched = true;
        makingCall = true;
      } else if (blePacketLength != 0){
        Serial.println("Got a new ble packet");
        if(payload[2] == 1){
          state = 1;
          previousBleState = 1; // is this needed????
        }else if(payload[2] == 0){
          //ignore
          return;
        }else {
          Serial.print("Expected payload 1 but got "); Serial.println(payload[2]);
        }
      }
      break;
    case 1: // Calling
      if(makingCall){
        if(blePacketLength != 0 && previousBleState != 1){ // Our call has been answered. We're now connected
          if(payload[2] == 2){
            state = 2;
          }else{
            Serial.print("Expected payload 2 but got "); Serial.println(payload[2]);
          }
        }
      } else if(isTouched()){  // If we're receiving a call, are now are touching the local device, then we're connected
        state = 2;
        bleWrite(2);
        previouslyTouched = true;
      }
      break;
    case 2:
      if(!isTouched()){
        state = 3;
        bleWrite(3);
        previouslyTouched = false;
      } else if( blePacketLength != 0 ) {
        if(payload[2] == 3){
          state = 3;
          previousBleState = 3; // is this needed?????
        }else{
            Serial.print("Expected payload 3 but got "); Serial.println(payload[2]);
        }
      }
      if(state == 3) countDown = millis();   // Start the timer
      break;
    case 3:
      if(millis() - countDown > IDLE_TIMEOUT) {
        state = 0;
        bleWrite(0);
        // Reset
        previouslyTouched = false; 
        makingCall = false; 
        bleReceived = false; 
        previousState = 0; 
        previousBleState = 0;
      }
      if(isTouched() && previouslyTouched == false){  // If we took our hand off but put it back on in under the time limit, re-connect
        state = 2;
        bleWrite(2);
        previouslyTouched = true;
      }
      break;
   
  }

  
  // Update animation frame
  if(millis() - lastUpdate > patternInterval) { 
    //updatePattern(pattern);
    updatePattern(state);
  }
}

void setState(){
 // static int timerId;
  switch(state){
    case 0: // If idle state but we're touched, set to "calling"
      state = isTouched() ? 1 : 0;
      break;
    case 1: // If "calling"
      state = isTouched() ? 2 : 1;
      break;
    case 2:
      timer.setTimeout(10000, checkConnection);
      break;
    case 3:
      // something here about disconnecting
    default:
      state = 0;
      break;
  }
}

//void loop() {
//  static int pattern = 0, lastReading;
//  static bool beenTouched = false, beenBled = false, gotBleMessage = false, buttonPushed = false;
//  int reading = digitalRead(BUTTON);
//  
//  if(!buttonPushed) {
//    if(lastReading == HIGH && reading == LOW) buttonPushed = true;
//    delay(50); // debounce delay      
//  }
//  
//  // ble checks are slow. Too many and LED animations won't look good
//  // The BLE_READPACKET_TIMEOUT in BluefruitConfig.h is set to 50 ms by default. May need tweaking
//  if(!gotBleMessage){
//    if(millis() - lastBleCheck > BLE_CHECK_INTERVAL) {
//      gotBleMessage = (readPacket(&ble, BLE_READPACKET_TIMEOUT) != 0) ? 1 : 0;
//      if(gotBleMessage) Serial.println("ble packet received");
//      lastBleCheck = millis();
//    }      
//  }
//
//  if(( buttonPushed && !beenTouched)  ) {
//    beenTouched = true;
//    // write to ble so that device on other end is called/connected    
//    bleWrite(pattern);    
//  }
//  
//  if(gotBleMessage && !beenBled) beenBled = true;
//
//  if((beenTouched && beenBled) && pattern < 2) {
//    pattern = 2;
//    patternInterval = animationSpeed[pattern]; // set speed for this animation
//    wipe();
//    resetBrightness();
//    idleTimer = millis();
//    Serial.println("CONNECTED!");
//  } else if ((beenTouched || beenBled) && !(beenTouched && beenBled) ) {
//    pattern = 1;
//    patternInterval = animationSpeed[pattern]; // set speed for this animation
//    wipe();
//    resetBrightness();
//    Serial.println("Calling...");
//  }
//
//  if(idleTimer > 0 && (millis() - idleTimer > IDLE_TIMEOUT*1000)) { // no action so reset to idle
//    Serial.println("Timeout!");
//    pattern = 0;
//    patternInterval = animationSpeed[pattern]; // set speed for this animation
//    wipe();    
//    idleTimer = 0;
//    beenTouched = false;
//    buttonPushed = false;
//    beenBled = false;
//    gotBleMessage = false;
//  }
//  
//  lastReading = reading; // save for next time
//  if(millis() - lastUpdate > patternInterval) { 
//    updatePattern(pattern);
//  }
//}

// Called by SimpleTimer to see if we're still connected
// This will be called once at the end of a timeout period.
// If not touched we switch to a "disconnecting" animation
// (note that I need to change to state 3 if the other person stops touhing too....is this really different logic????
void checkConnection(){  
    if(!isTouched()) {
      state = 3; 
    }
}
  
// Check if button is being pushed
bool isTouched(){
    static bool oneTouch = false;
    delay(50); // debounce delay  
    if(!oneTouch){
      oneTouch = (digitalRead(BUTTON) == LOW) ? 1 : 0;
    }
    return oneTouch;
    //return (digitalRead(BUTTON) == LOW) ? 1 : 0;
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
      breathe(1); // Breath blue
      break;
    case 3:
      breathe(2); // Breathe red
      break;
  }  
}

// LED breathing. Used for when devices are connected to one another
void breathe(int x) { 
  float SpeedFactor = 0.008; 
  static int i = 0;
  static int r,g,b;
  switch(x){
    case 1:
      r = 0; g = 127; b = 127;
      break;
    case 2:
      r = 255; g = 0; b = 0;
      break;
  }
  // Make the lights breathe
  float intensity = BRIGHTNESS /2.0 * (1.0 + sin(SpeedFactor * i));
  strip.setBrightness(intensity);
  for (int j=0; j<strip.numPixels(); j++) {
    strip.setPixelColor(j, r, g, b);
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

// Sends Bluetooth Low Energy payload
void bleWrite(uint8_t state){
  payload[0] = 0x21;
  payload[1] = 0x42;  
  payload[2] = state;
  uint8_t xsum = 0;
  uint16_t colLen = 3;
  for (uint8_t i=0; i<colLen; i++) {
    xsum += payload[i];
  }
  xsum = ~xsum;    
  payload[3] = xsum;
  ble.write(payload, PAYLOAD_LENGTH);
}

// A small helper
void error(const __FlashStringHelper*err) {
  Serial.println(err);
  while (1);
}


