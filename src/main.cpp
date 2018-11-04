#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPR121.h>
#include "MCP492X.h"

#define PIN_SPI_CHIP_SELECT_DAC 10

#define DEBUG false

int SCK_PIN = 13;
// int CS_PIN = 12;
int SDI_PIN = 11;

// CONSTANTS
int LENGTH = 8; // length of arrays

// You can have up to 4 on one i2c bus but one is enough for testing!
Adafruit_MPR121 cap = Adafruit_MPR121();
MCP492X dac(PIN_SPI_CHIP_SELECT_DAC); // SPI pin is digital pin 12 (atmega pin 18)


// Keeps track of the last pins touched
uint16_t lasttouched = 0;
uint16_t currtouched = 0;
int lastTouchedIndex = 0;  // Needed specifically for monophonic notes when toggleing tonic switches

// VOLTAGE INPUT/OUTPUT
float Vout = 0;       // quantized voltage output

int GATE_PIN = 12;

int LED_PINS[] = {2, 3, 4, 5, 6, 7, 8, 9}; // LED pins for MCP23017
bool ACTIVE_MONO_DEGREES[] = {0, 0, 0, 0, 0, 0, 0, 0}; // 1 == scale degree is active, 0 == scale degree is inactive

float quantizedVoltages[8][2] = {
    { 0, 0 }, // I
    { 136.5, 0 },      // maj2, n/a
    { 204.75, 273 },   // min3, maj3
    { 341.25, 409.5 }, // per4, aug4
    { 409.5, 477.75 }, // dim5, per5
    { 546, 614.25 },   // min6, maj6
    { 682.5, 750.75 }, // min7, maj7
    { 819, 0 } // VIII
  };

int OCTAVE_UP_PIN = 10;
int OCTAVE_DOWN_PIN = 9;
int OCTAVE = 0;
int OCTAVE_VALUES[] = {0, 819, 1638, 2457, 3276};

// Toggle Switches
int switchPins[] = {0, 0, A3, 0, A2, A1, A0, 0}; // Pin values are in no particular order.

// variables to hold bitmask values of switch states. These will essentially be the equivalent of an array containing
// 8 integers with the values of 0 or 1. Use bitRead(newSwitchStates, index) to get the value of a bit
byte newSwitchStates = 0;
byte oldSwitchStates = 0;

// -----------------------------------
// SET VOLTAGE OUT
//   - final stage determining which voltage to set the DAC too
// -----------------------------------
void setVoltageOut(int index) {
  uint8_t state = !bitRead(newSwitchStates, index); // NOTE: depending how switch is wired, HIGH/LOW may mean different things. Currently we need the opposite.
  Vout = quantizedVoltages[index][state] + OCTAVE_VALUES[OCTAVE];
  dac.analogWrite(Vout); // Set quantized voltage output
}


// -----------------------------------
// SET ACTIVE NOTES
// - based on current mode of quantizer, set the 'active' notes based on user selection via touch pads
// note: this is not setting the voltage!
// -----------------------------------
void setActiveNotes(int index) {
  // set last pressed note HIGH and reset all others to LOW
  for (int i=0; i<LENGTH; i++) {
    if (i == index) {
      ACTIVE_MONO_DEGREES[i] = HIGH;
      lastTouchedIndex = i;
      setVoltageOut(lastTouchedIndex);
    } else {
      ACTIVE_MONO_DEGREES[i] = LOW;
    }
    digitalWrite(LED_PINS[i], ACTIVE_MONO_DEGREES[i]);
  }

}


// -----------------------------------
// SET OCTAVE
// -----------------------------------

void setOctave(int button) {
  if (button == OCTAVE_UP_PIN) {
    if (OCTAVE < 4) {
      OCTAVE += 1;
    }
  }
  else if (button == OCTAVE_DOWN_PIN) {
    if (OCTAVE > 0) {
      OCTAVE -= 1;
    }
  }
}


// ==============================================================================================================
// SETUP
// ==============================================================================================================

void setup() {
  Serial.begin(9600);

  // Connect MPR121 touch sensors @ I2C address 0x5A (default)
  if (!cap.begin(0x5A)) {
    Serial.println("MPR121 not found, check wiring?");
    while (1);
  }
  Serial.println("MPR121 Found!");

  // Initialize MCP4921 DAC
  dac.begin();

  // Set SDI Bus Pins
  pinMode(SCK_PIN, OUTPUT);
  pinMode(PIN_SPI_CHIP_SELECT_DAC, OUTPUT);
  pinMode(SDI_PIN, OUTPUT);



  for(int p=0; p < 8; p++) {
    pinMode(LED_PINS[p], OUTPUT); // Set the mode to OUTPUT
  }

  // Set misc. pinouts (toggle switches, gate led, etc.)
  pinMode(A3, INPUT);
  pinMode(A2, INPUT);
  pinMode(A1, INPUT);
  pinMode(A0, INPUT);

  pinMode(GATE_PIN, OUTPUT);

  // Set initial state (read EEPROM here)
  digitalWrite(GATE_PIN, LOW);
}


// ==============================================================================================================
// LOOP
// ==============================================================================================================
void loop() {

  // Get the currently touched pads
  // cap.touched will return 16 bits (one byte), with each bit (from 0 - 12) representing the corrosponding touch pad
  currtouched = cap.touched();

  // get switch states of each scale step
  for (uint8_t i=0; i < 8; i++) {
    if (i == 2 || i == 4 || i == 5 || i == 6) {
      uint8_t state = digitalRead(switchPins[i]);
      bitWrite(newSwitchStates, i, state);
    }
  }

  if ( newSwitchStates != oldSwitchStates ) {
    oldSwitchStates = newSwitchStates;
    setVoltageOut(lastTouchedIndex);
  }



  // Iterate over touch sensors
  for (uint8_t i=0; i<12; i++) {

    // BUTTON TOUCHED
    // if it *is* touched and *wasnt* touched before, alert!
    if ( (currtouched & _BV(i) ) && !( lasttouched & _BV(i) ) ) {

      if (DEBUG == true) {
        Serial.print(i); Serial.print(" touched :: "); Serial.println(i, BIN);
      }

      if (i < LENGTH + 1 && i > 0) { // touch pads 1 to 8 (pads are zero indexed)
        digitalWrite(GATE_PIN, HIGH); // SET GATE HIGH
        setActiveNotes(i - 1); // since array of notes is indexed at zero, we need to subtract 1
      }
      else if (i > 8 && i < 11) {  // touch pads 9 and 10 set octaves
        setOctave(i);
      }

    }



    // BUTTON RELEASED
    //  if it *was* touched and now *isnt*, alert!
    if (!(currtouched & _BV(i)) && (lasttouched & _BV(i)) ) {

      digitalWrite(GATE_PIN, LOW); // SET GATE LOW

      if (DEBUG == true) {
        Serial.print(i); Serial.print(" released :: "); Serial.println(i, BIN);
        Serial.print(" .             Vout -->   ");   Serial.println(Vout);
        Serial.print(" .             OCTAVE -->   "); Serial.println(OCTAVE);
      }
    }
  }

  // reset touch sensors state
  lasttouched = currtouched;

}
