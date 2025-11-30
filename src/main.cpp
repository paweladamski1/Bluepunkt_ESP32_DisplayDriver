#include <Arduino.h>

// ESP32 OPEN-DRAIN DRIVER for OUTDOOR display (16-bit frames)
// Pins used: CLOCK -> GPIO26, LATCH -> GPIO25, DATA -> GPIO33
// Open-drain emulation: OUTPUT LOW to pull line low, INPUT to release (pull-up pulls HIGH)
// Adjust BIT_ON_HIGH if logic is inverted (1 means LED ON when line is HIGH).

const int PIN_CLOCK = 26;
const int PIN_DATA = 33;
const int PIN_LATCH = 25;

// timing (tunable)
const unsigned int T_HALF_US = 5; // half clock period in microseconds (5 -> ~100 kHz)

// If true: bit==1 => LINE HIGH (release), bit==0 => LINE LOW (drive low).
// If false: invert (bit==1 => drive low).
bool BIT_ON_HIGH = true;

// example frames for digits 0-9
bool frame1[11][7] = {
    {1, 1, 1, 1, 1, 0, 1}, // 0
    {0, 0, 0, 0, 1, 0, 1}, // 1
    {1, 1, 0, 1, 1, 1, 0}, // 2 ok
    {1, 0, 0, 1, 1, 1, 1}, // 3 ok
    {0, 0, 1, 0, 1, 1, 1}, // 4 ok
    {1, 0, 1, 1, 0, 1, 1}, // 5 ok
    {1, 1, 1, 1, 0, 1, 1}, // 6 ok
    {0, 0, 0, 1, 1, 0, 1}, // 7 ok
    {1, 1, 1, 1, 1, 1, 1}, // 8
    {1, 0, 1, 1, 1, 1, 1}, // 9   
    {0, 0, 0, 0, 0, 0, 0}  // NULL

 };

 bool frame2[11][7] = {
    {1, 1, 1, 1, 0, 1, 1}, // 0
    {0, 0, 0, 0, 0, 1, 1}, // 1
    {1, 0, 1, 1, 1, 1, 0}, // 2 ok
    {0, 0, 1, 1, 1, 1, 1}, // 3 
    {0, 1, 0, 0, 1, 1, 1}, // 4 ok
    {0, 1, 1, 1, 1, 0, 1}, // 5 
    {1, 1, 1, 1, 1, 0, 1}, // 6 
    {0, 0, 1, 0, 0, 1, 1}, // 7 ok
    {1, 1, 1, 1, 1, 1, 1}, // 8
    {0, 1, 1, 1, 1, 1, 1}, // 9   
    {0, 0, 0, 0, 0, 0, 0}  // NULL


 };
 
// ----- open-drain helpers -----
inline void pinLow(int pin)
{
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}
inline void pinHigh(int pin)
{
  pinMode(pin, INPUT); // high-Z, pull-up will pull line HIGH
}

// set data line according to logical bit and BIT_ON_HIGH polarity
inline void setDataBit(uint8_t bit)
{
  bool wantHigh = (bit != 0) ? BIT_ON_HIGH : !BIT_ON_HIGH;
  if (wantHigh)
    pinHigh(PIN_DATA);
  else
    pinLow(PIN_DATA);
}

// Pulse clock once (open-drain)
inline void pulseClock()
{
  pinHigh(PIN_CLOCK);
  delayMicroseconds(T_HALF_US);
  pinLow(PIN_CLOCK);
  delayMicroseconds(T_HALF_US);
}

// Latch pulse: observed as a short HIGH pulse (we emulate by releasing latch briefly)
void pulseLatch()
{
  // ensure latch idle = LOW
  pinHigh(PIN_LATCH);
  delayMicroseconds(2);

  // release -> goes HIGH (via pull-up)
  pinLow(PIN_LATCH);
  delayMicroseconds(8); // hold HIGH briefly to latch
  // drive low again to return to idle
  pinHigh(PIN_LATCH);
  delayMicroseconds(4);
  // release to leave in high-Z (optional)
  pinLow(PIN_LATCH);
}



void sendBitsArray(const bool *digit1, const bool *digit2, bool minus, bool celsius)
{
  // ensure latch idle low before starting
  pinLow(PIN_LATCH);
  delayMicroseconds(4);

  for (int i = 0; i < 7; ++i)
  {
    setDataBit(digit1[i]);
    // small setup time before clock
    delayMicroseconds(1);
    pulseClock();
  }

  for (int i = 0; i < 7; ++i)
  {
    setDataBit(digit2[i]);
    // small setup time before clock
    delayMicroseconds(1);
    pulseClock();
  }
  setDataBit(minus);
  delayMicroseconds(1);
  pulseClock();

  setDataBit(celsius);  
  delayMicroseconds(1);
  pulseClock();

  // after bits sent, pulse latch to update display
  pulseLatch();

  // release data line
  pinHigh(PIN_DATA);
}

void sendNumber(int num)
{
  bool minus = false;
  if (num < 0)
  {
    minus = true;
    num = -num;
  }
  int digit1 = num / 10;
  int digit2 = num % 10;

  if(digit1 == 0)
    digit1 = 10; // NULL for leading zero

  sendBitsArray(frame1[digit1], frame2[digit2], minus, true);
}

// initialize pins to safe released state
void initPins()
{
  pinHigh(PIN_CLOCK);
  pinHigh(PIN_DATA);
  pinHigh(PIN_LATCH);

  // optionally, ensure CLOCK/LATCH idle low - if you'd prefer idle low:
  // pinDriveLow(PIN_CLOCK);
  // pinDriveLow(PIN_LATCH);
}

void setup()
{
  Serial.begin(115200);
  initPins();
  Serial.println();
  Serial.println("ESP32 OUTDOOR DRIVER (open-drain) started");
  Serial.print("BIT_ON_HIGH = ");
  Serial.println(BIT_ON_HIGH ? "true (1 -> HIGH -> LED ON)" : "false (1 -> LOW -> LED ON)");
  Serial.println("Sending example frame for 24°C every 500 ms");
  delay(200);
}


unsigned long lastSend = 0;
const unsigned long SEND_INTERVAL_MS = 100;
int TestNumber = -99;


void loop()
{

  unsigned long now = millis();
  if (now - lastSend >= SEND_INTERVAL_MS)
  {
    lastSend = now;
    //sendBitsArray(frame24[IDX], 16);
    //sendBitsArray(frame1[IDX], frame2[IDX], false, true);
    sendNumber(TestNumber);

    Serial.print("Sent number (");
    Serial.print(TestNumber);
    Serial.print(")");
    Serial.println();
    TestNumber++;
    if (TestNumber > 99)
      TestNumber = -99;
  }
}
