// CallConnect
// Author: John B Damask
// Created: January 10, 2019
#include <AceButton.h>
using namespace ace_button;
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
#define BUTTON_DEBOUNCE 50  // Removes button noise
#define DEVICE_NAME     "AT+GAPDEVNAME=TouchLightsBle_upstairs"
//#define DEVICE_NAME     "AT+GAPDEVNAME=TouchLightsBle"
#define PAYLOAD_LENGTH  4   // Array size of BLE payload
bool makingCall = false;
#define IDLE_TIMEOUT    5000   // Milliseconds that there can be no touch or ble input before reverting to idle state
unsigned long patternInterval = 20 ; // time between steps in the pattern
unsigned long lastUpdate = 0, idleTimer = 0; // for millis() when last update occurred
unsigned long lastBleCheck = 0; // for millis() when last ble check occurred
//unsigned long buttonTimer = 0;  // Used to debounce
/* Each animation should have a value in this array */ 
unsigned long animationSpeed [] = { 100, 50, 2, 2 } ; // speed for each animation (order counts!)
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
uint8_t previousBleState = 0;

// Connection state (0 = idle; 1 = calling; 2 = connected)
uint8_t state = 0, previousState = 0;

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
//// the ble payload, set to max buffer size
//uint8_t payload[21];

// Main timer object (we can have multiple timers within)
SimpleTimer timer;
int connectionTimerId;
// Manages connection state
bool isConnected = false;

/* Button stuff */
AceButton button(BUTTON);
void handleEvent(AceButton*, uint8_t, uint8_t);
bool isTouched = false;
bool previouslyTouched = false;

/**************************************************************************/
/*!
    Setting everything up
*/
/**************************************************************************/
void setup() {
  delay(500);
  Serial.begin(9600);
  button.setEventHandler(handleEvent);    
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
  resetState(); // Clear state
  Serial.println("Ending setup...moving on to main loop");
}

/**************************************************************************/
/*!
    Action Jackson, baby
*/
/**************************************************************************/
void loop() {
//  static uint8_t previousState = -1, previousBleState = 0;
  static bool justWokeUp = true;
  static bool bleReceived = false;
  int blePacketLength;
  static long countDown = 0;
  bool static toldUs = false; // When in state 1, we're either making or receiving a call  

  isTouched = false; // I think we reset this on each iteration. It's global because ActionButton event needs to access it

  // Check if button was clicked. If so, a global variable will be set
  button.check();

  // Check for BLE message
  if(millis() - lastBleCheck > BLE_CHECK_INTERVAL) {
    blePacketLength = readPacket(&ble, BLE_READPACKET_TIMEOUT); // Read a packet into the buffer
    if(justWokeUp){ // If device just came online, then ignore the first ble notice since this is likely stale
      justWokeUp = false;
      return; // Since we get the BLE message on the first pass (assuming PiHub is online and state exists), we ignore and move to next iteration
    } 
    lastBleCheck = millis();
  }

/* Change animation speed if state changed */ 
  if(previousState != state) {
    Serial.print(DEVICE_NAME); Serial.println(": State change");
    wipe();
    resetBrightness();
    patternInterval = animationSpeed[state]; // set speed for this animation    
    previousState = state;
    Serial.print("Animation speed for state: "); Serial.print(state); Serial.print(" is "); Serial.println(patternInterval);
  }



  // The various cases we can face
  switch(state){
    case 0: // Idle
      if(isTouched) {
        Serial.println("State 0. Button pushed. Moving to State 1");
        state = 1;
        bleWrite(1);
        previouslyTouched = true;
        makingCall = true;
        idleTimer = millis();
      } else if (blePacketLength != 0){
        Serial.print("State 0. Received BLE. Moving to State "); Serial.println(packetbuffer[2]);
        if(packetbuffer[2] == 1){
          state = 1;
          previousBleState = 1; // is this needed????
          idleTimer = millis();
        }else if(packetbuffer[2] == 0){
          //ignore
          return;
        } else {
          Serial.print("Expected payload 1 but got "); Serial.println(packetbuffer[2]);
          resetState();
        }
      }
      break;
    case 1: // Calling
      if(makingCall){
        if(!toldUs) { // This is used to print once to the console
          Serial.println("I'm making the call");
          toldUs = true;          
        }
        if(millis() - idleTimer > IDLE_TIMEOUT){
          resetState();       // If no answer, we reset
          Serial.println("No one answered :-(");
        }
        if(blePacketLength != 0 && previousBleState != 1){ // Our call has been answered. We're now connected
          if(packetbuffer[2] == 2){
            Serial.print("State 1: Received BLE. Moving to State "); Serial.println(packetbuffer[2]);
            state = 2;       
            previousBleState = 3; // Is this needed?
          }else{
            Serial.print("Expected payload 2 but got "); Serial.println(packetbuffer[2]);
            Serial.println("Resetting state to 0");
            resetState();
          }
        }
      } else if(isTouched){  // If we're receiving a call, are now are touching the local device, then we're connected
        Serial.println("State 1. Button pushed. Moving to State 2");        
        state = 2;      
        bleWrite(2);
        previouslyTouched = true;
      } else if(blePacketLength != 0) {
        Serial.println("Receiving call");
        if(packetbuffer[2] == 0){ // This device didn't answer in time so we check to see if we got a timeout signal
          state = 0;  
        }
      }
      break;
    case 2:
      if(isTouched){    // Touch again to disconnect
        Serial.println("State 2. Button pushed. Moving to State 3");
        state = 3;
        bleWrite(3);
        previouslyTouched = false;               
      } else if( blePacketLength != 0 ) {
        if(packetbuffer[2] == 3){
          Serial.print("State 2. Received BLE. Moving to State "); Serial.println(packetbuffer[2]);
          state = 3;
          previousBleState = 3; // is this needed?????                
        }else{
            Serial.print("Expected payload 3 but got "); Serial.println(packetbuffer[2]);
            resetState();
        }
      }
      if(state == 3) countDown = millis();   // Start the timer
      break;
    case 3:
      if(millis() - countDown > IDLE_TIMEOUT) {
        Serial.println("State 3. Timed out. Moving to State 0");
       resetState();
        // Reset
        previouslyTouched = false; 
        makingCall = false; 
        bleReceived = false; 
        previousState = 0; 
        previousBleState = 0;       
      }
      if(isTouched && previouslyTouched == false){  // If we took our hand off but put it back on in under the time limit, re-connect
        Serial.println("Reconnecting...");
        Serial.println("State 3. Button pushed. The device that initiated a disconnection has reconnected. Moving to State 2");
        state = 2;
        bleWrite(2);
        previouslyTouched = true;            
      } else if( blePacketLength != 0 ) {
        if(packetbuffer[2] == 2){
          Serial.print("State 3. Received BLE. Moving to State "); Serial.println(packetbuffer[2]);
          state = 2;
          previousBleState = 2; // is this needed?????                
        }else{
            Serial.print("Expected payload 2 but got "); Serial.println(packetbuffer[2]);
            resetState();
        }
      } 
      break; 
    default:
      resetState();
      break;
  }

  
  // Update animation frame
  if(millis() - lastUpdate > patternInterval) { 
    //updatePattern(pattern);
    updatePattern(state);
  }
}


void handleEvent(AceButton* /* button */, uint8_t eventType,
    uint8_t /* buttonState */) {
  switch (eventType) {
    case AceButton::kEventPressed:
      isTouched = true;
      Serial.println("Button pushed");
      break;
  }
}


// Clean house
void resetState(){
  state = 0;
  previousBleState = 0;
  previouslyTouched = false;
  makingCall = false;
  bleWrite(state);
}

// Called by SimpleTimer to see if we're still connected
// This will be called once at the end of a timeout period.
// If not touched we switch to a "disconnecting" animation
// (note that I need to change to state 3 if the other person stops touhing too....is this really different logic????
void checkConnection(){  
    if(!isTouched) {
      state = 3; 
    }
}

// Check if button is pushed. Toggle on and off for better control while debugging
// Reworked to remove delay()
//bool isTouched(){
//  static unsigned long buttonTimer = 0;
//  static bool realClick = true; // Default to true. 
//  static int lastReading;
//  int reading = digitalRead(BUTTON);
//
//  if(lastReading == HIGH && reading == LOW){
//    buttonTimer = millis();
//    realClick = false;
//  }
//  if(!realClick && (millis() - buttonTimer > BUTTON_DEBOUNCE)){
//    realClick = true;
//    return true;
//  }
//  lastReading = reading;
//  return false;

//}

  
//// Check if button is pushed. Toggle on and off for better control while debugging
//// NOTE THIS LOGIC ISN"T RIGHT
//bool isTouched(){
//  static bool oneTouch = false;
//  static bool buttonPushed = false;  
//  static int lastReading;
//  int reading = digitalRead(BUTTON);
//
//  if(lastReading != reading) {
//    Serial.println("Button state changed");
//    buttonPushed = !buttonPushed; // toggles true and false
//    delay(50); // debounce delay   
//  }
//
//  lastReading = reading;
//  return buttonPushed;
//
//}

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
  Serial.print("Writing state to ble: "); Serial.println(state);
  delay(10);
  // the ble payload, set to max buffer size
  uint8_t payload[21];  
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


