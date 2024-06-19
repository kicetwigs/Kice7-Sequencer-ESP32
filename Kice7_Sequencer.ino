/*
 * Author : Kevin Laurenson
 * Name : Kice7 Sequencer
 * Update : 2024-06-19
*/

#include <MIDI.h>
#include <RotaryEncoder.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Create a MIDI instance using UART2
HardwareSerial MIDI_SERIAL(2);
MIDI_CREATE_INSTANCE(HardwareSerial, MIDI_SERIAL, MIDI);

// OLED display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, -1);
bool refreshScreen = false;

const int numButtons = 3;
const int button[numButtons] = { 12, 14, 25 };
bool buttonPressed[numButtons] = { 0 };

RotaryEncoder encoder(33, 32, RotaryEncoder::LatchMode::FOUR3);

bool IS_EDITING = true;  // 0 = Play ; 1 = Record
bool IS_PLAYING = false;
unsigned long lastTick = 0;
unsigned long interval;

const int maxFingers = 16;
int polyphony = 0;
int buildingChord[maxFingers] = { 0 };

int bpm = 120;
int numSteps = 32;
int currentStep = 0;
int previousStep = 0;
int steps[64][maxFingers] = { { 0 } };  // 2D array to store notes for each step

char* noteNames[] = { "C.", "Db", "D.", "Eb", "E.", "F.", "F#", "G.", "Ab", "A.", "Bb", "B." };
String getNoteName(int note) {
  byte octave = (note / 12);
  byte noteInOctave = note % 12;
  return ((String)noteNames[noteInOctave] + (octave + 1));
}

void updateBpmInterval() {
  interval = 60000 / (bpm * 2);
}

void setup() {

  // ESP32 Lolin Midi on pins RX:16 TX:17
  MIDI_SERIAL.begin(31250, SERIAL_8N1, 16, 17);
  MIDI.begin(MIDI_CHANNEL_OMNI);

  // Initialize OLED display
  Wire1.begin(0, 4);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }

  for (int buttonPin = 0; buttonPin < numButtons; buttonPin++) {
    pinMode(button[buttonPin], INPUT_PULLUP);
  }
  encoder.setPosition(0);

  display.display();
  delay(2000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  updateBpmInterval();

  // Set up MIDI callback functions
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);

  refreshScreen = true;
}

void loop() {
  MIDI.read();

  updateButtons();
  updateEncoder();

  performSequencer();

  if (refreshScreen == true) {
    refreshScreen = false;
    handleDisplay();
  }
}


// ============================================================================
// Handle Buttons & Encoder
// ============================================================================
void updateButtons() {

  // Button Left
  if (digitalRead(button[0]) == LOW) {
    IS_PLAYING = !IS_PLAYING;

    if (!IS_PLAYING) {
      // Not playing anymore, then turn off all notes
      do_sendNoteOff_prevStep();
      do_sendNoteOff_currentStep();
    }

    refreshScreen = true;
    delay(300);
  }

  // Button Right
  if (digitalRead(button[1]) == LOW) {
    if (IS_EDITING) {
      do_eraseNotes_currentStep();
      if (currentStep == (numSteps - 1)) {
        goToNextStep(true);
      } else {
        goToNextStep(false);
      }
    } else {
    }
    refreshScreen = true;
    delay(300);
  }

  // Button Encoder
  if (digitalRead(button[2]) == LOW) {
    // --- do
    IS_EDITING = !IS_EDITING;
    // --- end
    refreshScreen = true;
    delay(300);
  }
}

void updateEncoder() {
  encoder.tick();
  static int pos = 0;
  int newPos = encoder.getPosition();

  if (pos != newPos) {
    if (IS_EDITING) {
      updateValueLoop(0, (numSteps - 1), (int)(encoder.getDirection()), currentStep);
      do_triggerNoteOn_currentStep();
    } else {
      updateValue(30, 240, (int)(encoder.getDirection()), bpm);
      updateBpmInterval();
    }

    refreshScreen = true;
    pos = newPos;
  }
}


// ============================================================================
// Handle Sequencer
// ============================================================================
void performSequencer() {
  if (IS_PLAYING && millis() - lastTick >= interval) {
    lastTick = millis();
    refreshScreen = true;

    do_sendNoteOff_prevStep();
    do_sendNoteOn_currentStep();

    previousStep = currentStep;
    currentStep = (currentStep + 1) % numSteps;
  }
}
void goToNextStep(bool exit) {
  currentStep++;
  if (currentStep > (numSteps - 1)) {
    currentStep = 0;
  }
  if (exit) {
    IS_EDITING = false;
    IS_PLAYING = false;
  }
  refreshScreen = true;  // Update the display when changing steps
}
void goToPrevStep() {
  currentStep--;
  if (currentStep < 0) {
    currentStep = (numSteps - 1);
  }
  refreshScreen = true;  // Update the display when changing steps
}


// ============================================================================
// Handle Notes
// ============================================================================
void handleNoteOn(byte channel, byte note, byte velocity) {
  // This note is pressed
  if (IS_EDITING) {
    polyphony++;
    buildingChord[polyphony] = note;
  }
}
void handleNoteOff(byte channel, byte note, byte velocity) {
  if (IS_EDITING) {
    polyphony--;

    if (polyphony > 0) {
      // Some notes are still pushed, wait for all to be released
    } else {
      do_eraseNotes_currentStep();
      // Store the chord or single note
      for (int i = 0; i < maxFingers; i++) {
        if (buildingChord[i] > 0) {
          steps[currentStep][i] = buildingChord[i];
        } else {
          steps[currentStep][i] = false;
        }
      }
      // Reset input builder
      polyphony = 0;
      for (int i = 0; i < maxFingers; i++) {
        buildingChord[i] = 0;
      }

      if (IS_PLAYING) {
        // Do not while playing
      } else {
        if (currentStep == (numSteps - 1)) {
          goToNextStep(true);
        } else {
          goToNextStep(false);
        }
      }
    }
  }
}
void do_sendNoteOff_prevStep() {
  for (int i = 0; i < maxFingers; i++) {
    if (steps[previousStep][i] != 0) {
      MIDI.sendNoteOff(steps[previousStep][i], 127, 1);
    }
  }
}
void do_sendNoteOff_currentStep() {
  for (int i = 0; i < maxFingers; i++) {
    if (steps[previousStep][i] != 0) {
      MIDI.sendNoteOff(steps[currentStep][i], 127, 1);
    }
  }
}
void do_sendNoteOn_currentStep() {
  for (int i = 0; i < maxFingers; i++) {
    if (steps[currentStep][i] != 0) {
      MIDI.sendNoteOn(steps[currentStep][i], 127, 1);
    }
  }
}
void do_triggerNoteOn_currentStep() {
  for (int i = 0; i < maxFingers; i++) {
    if (steps[currentStep][i] != 0) {
      MIDI.sendNoteOn(steps[currentStep][i], 127, 1);
      delay(100);
      MIDI.sendNoteOff(steps[currentStep][i], 127, 1);
    }
  }
}
void do_eraseNotes_currentStep() {
  for (int i = 0; i < maxFingers; i++) {
    steps[currentStep][i] = 0;
  }
}


// ============================================================================
// Handle Display
// ============================================================================
void handleDisplay() {

  // Display update
  display.clearDisplay();

  if (IS_EDITING) {
    drawRectangle(0, 0, 128, 11, 0);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(2, 2);

    if (IS_PLAYING) {
      display.print("Live Record");
    } else {
      display.print("Recording");
    }
  } else {
    drawRectangle(0, 0, 128, 11, 11);

    display.setTextColor(SSD1306_BLACK);
    display.setCursor(2, 2);

    if (IS_PLAYING) {
      display.print("Playing");
    } else {
      display.print("Ready");
    }
  }
  display.setCursor(96, 2);
  display.print((String) "B:" + bpm);

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(34, 20);
  display.setTextSize(2);
  if (currentStep < 9) {
    display.println((String) "0" + (currentStep + 1) + "/" + numSteps);
  } else {
    display.println((String) "" + (currentStep + 1) + "/" + numSteps);
  }

  display.setCursor(0, 40);
  display.setTextSize(1);
  for (int i = 0; i < 16; i++) {
    if (steps[currentStep][i] != 0) {
      display.print(getNoteName(steps[currentStep][i]));
      display.print(" ");
    }
  }

  display.display();
}


// ============================================================================
// Tools
// ============================================================================
void drawRectangle(int x, int y, int width, int height, int innerFill) {
  display.drawRect(x, y, width, height, SSD1306_WHITE);  // Draw border
  if (innerFill > 0) {
    int fillY = y + (height - innerFill);
    display.fillRect(x, fillY, width, innerFill, SSD1306_WHITE);  // Fill rectangle if requested
  }
}
void updateValue(int minVal, int maxVal, int dir, int& value) {
  value += dir;
  // Stays within the bounds
  if (value < minVal) {
    value = minVal;
  } else if (value > maxVal) {
    value = maxVal;
  }
}
void updateValueLoop(int minVal, int maxVal, int dir, int& value) {
  value += dir;
  // Stays within the bounds
  if (value < minVal) {
    value = maxVal;
  } else if (value > maxVal) {
    value = minVal;
  }
}
