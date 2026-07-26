//  Based on: Tymek49's RTI_control (https://github.com/TymEK49/RTI_control)
//  Ported for Arduino Mega 2560.
//
//
//  MEGA CHANGE: the RTI connector now goes on hardware Serial1 (pins 18/19)
//  instead of the main Serial port, since Serial (pins 0/1) is shared with
//  USB. This means the RTI display can stay connected AND you can still
//  use the Serial Monitor for debug/testing at the same time - not
//  possible on the original single-UART board this was written for.
//
//  RTI TX (display's RX, pin 5)  -> Mega pin 18 (TX1)
//  RTI GND (pin 7)               -> Mega GND

#include <EEPROM.h>
//EEPROM index: 0 RTI mode, 1 default brightness, 2 enter key brightness, 3 back key brightness, 4 last "open" mode index

#define RTI Serial1

// ---- New physical controls ----
const int OPEN_CLOSE_SWITCH_PIN = 5;  // toggle switch, other leg to GND
const int ENC_CLK_PIN = 2;            // encoder CLK (must be an interrupt pin)
const int ENC_DT_PIN  = 3;            // encoder DT
const int ENC_SW_PIN  = 4;            // encoder's built-in pushbutton, other leg to GND

// Some encoders give 4 quadrature transitions per physical detent/click,
// others give 2. If one click moves brightness by more than one step,
// change this to 2.
const int ENCODER_STEPS_PER_CLICK = 4;

volatile int encoderDelta = 0;   // accumulated raw transitions since last read
volatile int lastClkState;

bool switchIsOpen = false;       // debounced current state of the toggle switch
bool lastSwitchReading = HIGH;
unsigned long lastSwitchChangeTime = 0;
const long SWITCH_DEBOUNCE_MILS = 50;

int lastOpenModeIndex = 0;       // which of RGB/PAL/NTSC to return to when opened (0-2)

bool lastEncBtnReading = HIGH;
unsigned long lastEncBtnChangeTime = 0;
const long ENC_BTN_DEBOUNCE_MILS = 250;

// ---- Status LEDs ----
const int STATUS_LED_PIN     = 6;  // lit when display is open
const int MODE_LED_RGB_PIN   = 7;
const int MODE_LED_PAL_PIN   = 8;
const int MODE_LED_NTSC_PIN  = 9;
const int BRIGHTNESS_LED_PIN = 10; // PWM - its own brightness mirrors the setting

// ---- Ignition power relay ----
// Switches the +12V ignition wire to the RTI display through the relay's
// NO (normally-open) contacts. Relay ON = display powered = open.
// Relay OFF = ignition cut = display fully unpowered = closed.
const int RELAY_PIN = 11;
// Most relay modules trigger ON when the control pin is driven HIGH, but
// some (many cheap "active-low" boards) trigger ON when driven LOW.
// If the relay behaves backwards (on when it should be off), change this
// to false - no wiring change needed.
const bool RELAY_ACTIVE_HIGH = true;
// --------------------------------

String ab[10];
String data;
String sectionData;
int a;
int b;
int stringData;
int last_RTI_mode;
int last_RTI_brightness;
int last_enter_brightness;
int last_back_brightness;
int index_brightness;
int index_enter;
int index_back;
const int BUFFER_SIZE = 4;
const long DEBOUNCE_TIME_MILS = 750;
char remote_signal[BUFFER_SIZE];
char last_signal[BUFFER_SIZE];
long last_signal_time = 0;
char screen_state[]       = {0x40, 0x45, 0x4C, 0x46}; // RTI_RGB, RTI_PAL, RTI_NTSC, RTI_OFF
char codes_state[]        = {0x72, 0x70, 0x6E, 0x6F}; //keys r, p, n, o
char screen_brightness[]  = {0x20, 0x61, 0x62, 0x23, 0x64, 0x25, 0x26, 0x67, 0x68, 0x29, 0x2A, 0x6B, 0x2C, 0x6D, 0x6E, 0x2F}; // brightness from 0 to 15
char codes_brightness[]   = {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66}; //keys 0-9, a, b, c, d, e, f
char up1[BUFFER_SIZE]     = {0xED, 0xAF, 0xBD, 0xFD};
char up2[BUFFER_SIZE]     = {0xED, 0xAF, 0xFD, 0xFD};
char up3[BUFFER_SIZE]     = {0xED, 0xEF, 0xBD, 0xFD};
char down1[BUFFER_SIZE]   = {0xED, 0xAD, 0xAF, 0xFF};
char down2[BUFFER_SIZE]   = {0xED, 0xAD, 0xEF, 0xFF};
char left1[BUFFER_SIZE]   = {0xED, 0xBD, 0xBD, 0xFD};
char left2[BUFFER_SIZE]   = {0xED, 0xBD, 0xFD, 0xFD};
char left3[BUFFER_SIZE]   = {0xED, 0xFD, 0xBD, 0xFD};
char right1[BUFFER_SIZE]  = {0xED, 0xEF, 0xAF, 0xED};
char right2[BUFFER_SIZE]  = {0xED, 0xEF, 0xEF, 0xED};
char enter[BUFFER_SIZE]   = {0xED, 0xEF, 0xED, 0xED};
char back1[BUFFER_SIZE]   = {0xED, 0xBD, 0xEF, 0xED};
char back2[BUFFER_SIZE]   = {0xED, 0xFD, 0xEF, 0xED};
char signal_state;
char signal_brightness;


void setup()
{
  Serial.begin(115200);  // USB - free for debug / typed test commands
  RTI.begin(2400);       // physical RTI connector on pins 18(TX1)/19(RX1)
  checkPowerState();
  checkBrightnessState();
  checkEnterState();
  checkBackState();

  // Restore which video mode to return to when the switch is flipped open
  int savedOpenMode = EEPROM.read(4);
  lastOpenModeIndex = (savedOpenMode >= 0 && savedOpenMode <= 2) ? savedOpenMode : 1; // default PAL (EU)

  pinMode(OPEN_CLOSE_SWITCH_PIN, INPUT_PULLUP);
  pinMode(ENC_CLK_PIN, INPUT_PULLUP);
  pinMode(ENC_DT_PIN, INPUT_PULLUP);
  pinMode(ENC_SW_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);

  lastClkState = digitalRead(ENC_CLK_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), updateEncoder, CHANGE);

  // Set initial display state to match the switch's physical position at boot
  lastSwitchReading = digitalRead(OPEN_CLOSE_SWITCH_PIN);
  switchIsOpen = (lastSwitchReading == LOW); // LOW = switch closed to GND = "open" position
  applyOpenCloseState();

  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(MODE_LED_RGB_PIN, OUTPUT);
  pinMode(MODE_LED_PAL_PIN, OUTPUT);
  pinMode(MODE_LED_NTSC_PIN, OUTPUT);
  pinMode(BRIGHTNESS_LED_PIN, OUTPUT);
  updateLEDs();
}

void loop()
{
  RTI.print(signal_state);
  delay(50);
  RTI.print(signal_brightness); // {0x20, 0x61, 0x62, 0x23, 0x64, 0x25, 0x26, 0x67, 0x68, 0x29, 0x2A, 0x2C, 0x6B, 0x6D, 0x6E, 0x2F};
  delay(50);
  RTI.print((char)0x83); // must have, volvo things
  delay(50);

  // Real button/remote codes arrive from the RTI connector itself
  if (RTI.available() > 0) {
      int rlen = RTI.readBytesUntil('\n', remote_signal, sizeof(remote_signal));
      debounceSignal(remote_signal);
  }

  // Typed test commands from the USB Serial Monitor (dev/testing use only)
  if (Serial.available() > 0) {
      int rlen = Serial.readBytesUntil('\n', remote_signal, sizeof(remote_signal));
      debounceSignal(remote_signal);
  }

  checkOpenCloseSwitch();
  checkEncoderRotation();
  checkEncoderButton();
  updateLEDs();
}

void checkPowerState() {
  // check if EEPROM memory is in 10 - 14 range
  last_RTI_mode = EEPROM.read(0);
  for (b = 10; b < 14; b++){
    if (last_RTI_mode == b){
      last_RTI_mode = b - 10;
      signal_state = screen_state[last_RTI_mode];
      return;
    }
  }
  last_RTI_mode = 1; // default to PAL (EU)
  signal_state = screen_state[last_RTI_mode];
  EEPROM.update(0, last_RTI_mode + 10);
}

void checkBrightnessState() {
  // check if EEPROM memory is in 10 - 25 range
  last_RTI_brightness = EEPROM.read(1);
  for (b = 10; b < 26; b++){
    if (last_RTI_brightness == b){
      index_brightness = b - 10;
      signal_brightness = screen_brightness[index_brightness];
      return;
    }
  }
  index_brightness = 15;
  signal_brightness = screen_brightness[index_brightness];
  EEPROM.update(1, index_brightness + 10);
}


void checkEnterState() {
  // check if EEPROM memory is in 10 - 25 range
  last_enter_brightness = EEPROM.read(2);
  for (b = 10; b < 26; b++){
    if (b == last_enter_brightness){
      index_enter = b - 10;
      // Serial.println(index_enter);
      return;
    }
  }
  index_enter = 15;
  EEPROM.update(2, index_enter + 10);
}

void checkBackState() {
  // check if EEPROM memory is in 10 - 25 range
  last_back_brightness = EEPROM.read(3);
  for (b = 10; b < 26; b++){
    if (b == last_back_brightness){
      index_back = b - 10;
      return;
    }
  }
  index_back = 3;
  EEPROM.update(3, index_back + 10);
}

void debounceSignal(char signal[4]) {
    if (millis() - last_signal_time < DEBOUNCE_TIME_MILS)
        return;
    memcpy(last_signal, signal, 4);
    last_signal_time = millis();
    handleSignal(remote_signal);
}

void handleSignal(char signal[4]) {
    if (memcmp(signal, up1, 4) == 0 || memcmp(signal, up2, 4) == 0 || memcmp(signal, up3, 4) == 0){
      signal_state = screen_state[1]; // PAL (EU)
      EEPROM.update(0, 11);
    }
    if (memcmp(signal, down1, 4) == 0 || memcmp(signal, down2, 4) == 0) {
      signal_state = screen_state[3];
      EEPROM.update(0, 13);
    }
    if (memcmp(signal, left1, 4) == 0 || memcmp(signal, left2, 4) == 0 || memcmp(signal, left3, 4) == 0){
      if (index_brightness > 0 && index_brightness < 16){
        index_brightness = index_brightness - 1;
        signal_brightness = screen_brightness[index_brightness];
        EEPROM.update(1, index_brightness + 10);
      }
    }
    if (memcmp(signal, right1, 4) == 0 || memcmp(signal, right2, 4) == 0) {
      if (index_brightness >= 0 && index_brightness < 15){
        index_brightness = index_brightness + 1;
        signal_brightness = screen_brightness[index_brightness];
        EEPROM.update(1, index_brightness + 10);
      }
    }
    if (memcmp(signal, enter, 4) == 0) {
      signal_brightness = screen_brightness[index_enter];
      index_brightness = index_enter;
      EEPROM.update(1, index_brightness + 10);
    }
    if (memcmp(signal, back1, 4) == 0 || memcmp(signal, back2, 4) == 0) {
      signal_brightness = screen_brightness[index_back];
      index_brightness = index_back;
      EEPROM.update(1, index_brightness + 10);
    }
    else{
      commandsUSB(signal);
    }
}

void commandsUSB(char signal[4]) {
  for (b = 0; b < 4; b++){
    if (signal[0] == codes_state[b]){
      signal_state = screen_state[b];
      EEPROM.update(0, b + 10);
    }
  }
  for (b = 0; b < 16; b++){
      if (signal[0] == codes_brightness[b]){
        signal_brightness = screen_brightness[b];
        index_brightness = b;
        EEPROM.update(1, b + 10);
      }
    }
  if (signal[0] == char(0x78)){
  // sending 'x' key will set RTI in off after next boot, this is for android development purpose
    EEPROM.update(0, 3);
  }
  if (signal[0] == char(0x2B)){
  // sending '+' key will set current brightness as ENTER key
    index_enter = index_brightness;
    EEPROM.update(2, index_enter + 10);
  }
  if (signal[0] == char(0x2D)){
  // sending '-' key will set current brightness as BACK key
    index_back = index_brightness;
    EEPROM.update(3, index_back + 10);
  }
}

// ---------------- New physical controls ----------------

// Quadrature decode ISR - fires on any change of CLK
void updateEncoder() {
  int clkState = digitalRead(ENC_CLK_PIN);
  if (clkState != lastClkState) {
    int dtState = digitalRead(ENC_DT_PIN);
    if (dtState != clkState) {
      encoderDelta++;  // clockwise
    } else {
      encoderDelta--;  // counterclockwise
    }
  }
  lastClkState = clkState;
}

// Turns accumulated encoder pulses into brightness steps, reusing the
// same left/right brightness logic as the original remote codes.
void checkEncoderRotation() {
  int delta;
  noInterrupts();
  delta = encoderDelta;
  interrupts();

  if (delta >= ENCODER_STEPS_PER_CLICK) {
    noInterrupts();
    encoderDelta -= ENCODER_STEPS_PER_CLICK;
    interrupts();
    if (index_brightness < 15) {
      index_brightness++;
      signal_brightness = screen_brightness[index_brightness];
      EEPROM.update(1, index_brightness + 10);
    }
  } else if (delta <= -ENCODER_STEPS_PER_CLICK) {
    noInterrupts();
    encoderDelta += ENCODER_STEPS_PER_CLICK;
    interrupts();
    if (index_brightness > 0) {
      index_brightness--;
      signal_brightness = screen_brightness[index_brightness];
      EEPROM.update(1, index_brightness + 10);
    }
  }
}

// Encoder's built-in pushbutton: short press cycles RGB -> PAL -> NTSC,
// only while the display is open (no point cycling video mode when off).
void checkEncoderButton() {
  bool reading = digitalRead(ENC_SW_PIN);
  if (reading != lastEncBtnReading) {
    lastEncBtnChangeTime = millis();
  }
  if ((millis() - lastEncBtnChangeTime) > ENC_BTN_DEBOUNCE_MILS) {
    if (reading == LOW && lastEncBtnReading == HIGH) {
      // fresh press detected
      if (switchIsOpen) {
        lastOpenModeIndex = (lastOpenModeIndex + 1) % 3; // cycle RGB/PAL/NTSC
        signal_state = screen_state[lastOpenModeIndex];
        EEPROM.update(0, lastOpenModeIndex + 10);
        EEPROM.update(4, lastOpenModeIndex);
      }
    }
  }
  lastEncBtnReading = reading;
}

// Drives the relay coil, accounting for whether the relay module is
// active-high or active-low.
void setRelay(bool wantOn) {
  bool pinState = RELAY_ACTIVE_HIGH ? wantOn : !wantOn;
  digitalWrite(RELAY_PIN, pinState ? HIGH : LOW);
}

// Applies the current switchIsOpen state: relay controls actual power to
// the display now, instead of trying to fake "off" with a serial command.
void applyOpenCloseState() {
  setRelay(switchIsOpen);
  if (switchIsOpen) {
    signal_state = screen_state[lastOpenModeIndex]; // restore last video mode
    EEPROM.update(0, lastOpenModeIndex + 10);
  }
  // When closed, we deliberately don't touch signal_state/send an OFF
  // code anymore - the relay has physically cut ignition power to the
  // display instead, which is the actual reliable way to turn it off.
}

// Debounced read of the open/close toggle switch.
// LOW = switch closed to GND = treated as "open" position.
// Swap the comparison below if your switch is wired the other way round.
void checkOpenCloseSwitch() {
  bool reading = digitalRead(OPEN_CLOSE_SWITCH_PIN);
  if (reading != lastSwitchReading) {
    lastSwitchChangeTime = millis();
  }
  if ((millis() - lastSwitchChangeTime) > SWITCH_DEBOUNCE_MILS) {
    bool wantOpen = (reading == LOW);
    if (wantOpen != switchIsOpen) {
      switchIsOpen = wantOpen;
      applyOpenCloseState();
    }
  }
  lastSwitchReading = reading;
}

// Reads the current display state/brightness and drives the LEDs to match.
// Called once per loop, so it stays in sync no matter what changed it
// (encoder, RTI connector remote codes, or USB test commands).
void updateLEDs() {
  bool isOpenNow = switchIsOpen; // relay is now the actual source of truth for open/closed

  digitalWrite(STATUS_LED_PIN, isOpenNow ? HIGH : LOW);

  digitalWrite(MODE_LED_RGB_PIN,  (isOpenNow && signal_state == screen_state[0]) ? HIGH : LOW);
  digitalWrite(MODE_LED_PAL_PIN,  (isOpenNow && signal_state == screen_state[1]) ? HIGH : LOW);
  digitalWrite(MODE_LED_NTSC_PIN, (isOpenNow && signal_state == screen_state[2]) ? HIGH : LOW);

  int pwmVal = map(index_brightness, 0, 15, 0, 255);
  analogWrite(BRIGHTNESS_LED_PIN, pwmVal);
}
