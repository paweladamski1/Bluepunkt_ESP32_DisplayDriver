// ESP32 OPEN-DRAIN DRIVER for OUTDOOR display (16-bit frames)
// Pins used: CLOCK -> GPIO4, LATCH -> GPIO2, DATA -> GPIO3
// Open-drain emulation: OUTPUT LOW to pull line low, INPUT to release (pull-up pulls HIGH)
// BIT_ON_HIGH = true means bit==1 -> LINE HIGH (LED ON), bit==0 -> LINE LOW (LED OFF)

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WebServer.h>

#include "wifi_pass.h"
#include "outdoor_symbols.h"

// Pin definitions
const int PIN_LATCH = 2; // green
const int PIN_DATA = 3;  // blue
const int PIN_CLOCK = 4; // yellow

// ===== HTTP server =====
WebServer server(80);

// ===== Temperature read interval =====
// actual temperature to display
float currentTemp = 0;

// main functions to set display
void setOutdoorDisplay(int num);
void setOutdoorDisplay(const String &data);
void setOutdoorDisplay_animate(int idx);

// www handlers
void server_handleRoot();
void server_handleSet();

// heldpers for open-drain signaling
inline void setPinLow(int pin);
void initPins();
inline void setDataBit(uint8_t bit);
inline void setPinHigh(int pin);
inline void pulseClock();
void pulseLatch();
bool every5Minuts(bool reset = false);
bool everySecond();
void WifiCheck();
void animateStartLCD();
bool validateTemp(float t);

void sendBitsArray(const bool *digit1, const bool *digit2, bool minus, bool celsius);
float getOutdoorTemperature(const String &url);

//-------------------------------------------------------------------------------------------------------

/// ===== Arduino setup / loop =====
/// @brief Arduino setup function
void setup()
{
  initPins();
  delay(2000);
  WiFi.setHostname("BLAUEPUNKT-DISPLAY");
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  server.on("/", server_handleRoot);
  server.on("/set", server_handleSet);
  server.begin();
}

void animateStartLCD()
{
  for (size_t i = 0; i < 12; i++)
  {
    setOutdoorDisplay_animate(i);
    delay(100);
  }
}

/// @brief Arduino main loop
void loop()
{
  static unsigned long lastAnimate = 0;
  static int animate_idx = 0;
  static bool isThermometerError = false;
  static bool firstRun = true;
  static unsigned int retryCount = 4;
  static bool wasWifiConnected = false;
  server.handleClient();

  WifiCheck();

  bool isWifiConnected = (WiFi.status() == WL_CONNECTED);
  if (!wasWifiConnected && isWifiConnected)
  {
    firstRun = true; // on reconnect, read temp immediately
    animateStartLCD();
  }
  wasWifiConnected = isWifiConnected;

  if (!isWifiConnected)
    return;

  if (every5Minuts() || firstRun)
  {
    firstRun = false;
    float t = 0;
    t = getOutdoorTemperature("http://temperatura_na_balkonie.local/json");

    if (validateTemp(t))
      isThermometerError = false;
    else
    {
      t = getOutdoorTemperature("http://192.168.1.35/json");
      if (validateTemp(t))
        isThermometerError = false;
      else
        isThermometerError = true;
    }

    if (!isThermometerError)
    {
      currentTemp = t;
      animate_idx = 0;
      retryCount = 0;
      setOutdoorDisplay(currentTemp);
    }
    else
      retryCount++;
    every5Minuts(true); // reset timer
  }

  // animate only if device error
  if (isThermometerError && everySecond() && retryCount > 3)
  {
    setOutdoorDisplay_animate(animate_idx);
    animate_idx = (animate_idx + 1) % 12;
  }
}

inline bool validateTemp(float t)
{
  return t >= -60.0f && t <= 99.0f;
}

/// @brief  Send 16 bits to display: two digits + minus + celsius
/// @param digit1   First digit segments array (7 bools)
/// @param digit2   Second digit segments array (7 bools)
/// @param minus    Minus sign segment (bool)
/// @param celsius  Celsius sign segment (bool)
void sendBitsArray(const bool *digit1, const bool *digit2, bool minus, bool celsius)
{
  // ensure latch idle low before starting
  setPinLow(PIN_LATCH);
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
  setPinHigh(PIN_DATA);
}

/// @brief  Set display to integer number (-99..99)
/// @param num   Integer number to display
void setOutdoorDisplay(int num)
{
  bool minus = false;
  if (num < 0)
  {
    minus = true;
    num = -num;
  }
  int digit1 = num / 10;
  int digit2 = num % 10;

  if (digit1 == 0)
    digit1 = 10; // NULL for leading zero

  if (minus && num > 0 && num < 10)
  {
    minus = false;
    digit1 = DISPLAY_SIGN_MINUS_IDX; // show minus on first digit if only one digit negative
  }
  sendBitsArray(DIGIT_1[digit1], DIGIT_2[digit2], minus, true);
}

/// @brief  Set display to NULL (all segments off)
/// @param data  String data ( "NULL" , "--" )
void setOutdoorDisplay(const String &data)
{
  // send NULL display
  if (data == "NULL")
  {
    sendBitsArray(DIGIT_1[DISPLAY_NULL_IDX], DIGIT_2[DISPLAY_NULL_IDX], false, true);
    return;
  }
  // send -- display
  if (data == "--")
  {
    sendBitsArray(DIGIT_1[DISPLAY_SIGN_MINUS_IDX], DIGIT_2[DISPLAY_SIGN_MINUS_IDX], false, true);
    return;
  }

  if (data == "01")
  {
    sendBitsArray(DIGIT_1[0], DIGIT_2[1], false, false);
    return;
  }

  if (data == "02")
  {
    sendBitsArray(DIGIT_1[0], DIGIT_2[2], false, false);
    return;
  }

  if (data == "03")
  {
    sendBitsArray(DIGIT_1[0], DIGIT_2[3], false, false);
    return;
  }

  if (data == "04")
  {
    sendBitsArray(DIGIT_1[0], DIGIT_2[4], false, false);
    return;
  }
  if (data == "05")
  {
    sendBitsArray(DIGIT_1[0], DIGIT_2[5], false, false);
    return;
  }

  if (data == "06")
  {
    sendBitsArray(DIGIT_1[0], DIGIT_2[6], false, false);
    return;
  }

  if (data == "99")
  {
    sendBitsArray(DIGIT_1[9], DIGIT_2[9], false, false);
    return;
  }

}

/// @brief  Animate display with index
/// @param idx  Index of animation frame (0..12)
void setOutdoorDisplay_animate(int idx)
{
  sendBitsArray(DIGIT_1[idx + 12], DIGIT_2[idx + 12], false, false);
}

/// @brief Handle root (/) request
void server_handleRoot()
{
  String html =
      "<!DOCTYPE html><html><head>"
      "<meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
      "<title>Outdoor Temp</title>"

      "<style>"
      "body{font-family:Arial,sans-serif;background:#f2f2f2;margin:0;padding:0;}"
      ".card{max-width:360px;margin:40px auto;background:#fff;"
      "padding:20px;border-radius:12px;box-shadow:0 4px 10px rgba(0,0,0,.1);}"
      "h2{text-align:center;margin-top:0;}"
      ".temp{font-size:48px;text-align:center;margin:20px 0;}"
      "form{display:flex;flex-direction:column;gap:15px;}"
      "input[type=number]{font-size:20px;padding:12px;border-radius:8px;border:1px solid #ccc;}"
      "input[type=submit]{font-size:20px;padding:12px;border-radius:8px;"
      "border:none;background:#007bff;color:white;cursor:pointer;}"
      "input[type=submit]:active{background:#0056b3;}"
      "</style>"

      "</head><body>"

      "<div class='card'>"
      "<h2>Outdoor Temperature</h2>"
      "<div class='temp'>" +
      String(currentTemp) + " &deg;C</div>"

                            "<form action='/set'>"
                            "<input type='number' name='temp' min='-99' max='99' placeholder='Enter temperature' required>"
                            "<input type='submit' value='Set temperature'>"
                            "</form>"
                            "</div>"

                            "</body></html>";

  server.send(200, "text/html", html);
}

/// @brief Handle /set request to set temperature (for test)
void server_handleSet()
{
  if (!server.hasArg("temp"))
  {
    server.send(400, "text/plain", "Missing temp");
    return;
  }

  int temp = server.arg("temp").toInt();

  if (temp < -99 || temp > 99)
  {
    server.send(400, "text/plain", "Out of range");
    return;
  }

  currentTemp = temp;
  setOutdoorDisplay(currentTemp);

  server.sendHeader("Location", "/");
  server.send(302);
}

/// @brief Set a pin to LOW (drive line low)
/// @param pin  Pin number
inline void setPinLow(int pin)
{
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}
/// @brief Set a pin to high-Z (pull-up will pull line HIGH)
/// @param pin  Pin number
inline void setPinHigh(int pin)
{
  pinMode(pin, INPUT); // high-Z, pull-up will pull line HIGH
}

/// @brief Set data line according to logical bit and BIT_ON_HIGH polarity
/// @param bit  Logical bit to send (0/1)
inline void setDataBit(uint8_t bit)
{
  bool wantHigh = (bit != 0) ? true : false;
  if (wantHigh)
    setPinHigh(PIN_DATA);
  else
    setPinLow(PIN_DATA);
}

/// @brief  Pulse clock line (LOW->HIGH->LOW)
inline void pulseClock()
{
  const unsigned int T_HALF_US = 5; // half clock period in microseconds (5 -> ~100 kHz)
  setPinHigh(PIN_CLOCK);
  delayMicroseconds(T_HALF_US);
  setPinLow(PIN_CLOCK);
  delayMicroseconds(T_HALF_US);
}

/// @brief  Pulse latch line to update display
void pulseLatch()
{
  // ensure latch idle = LOW
  setPinHigh(PIN_LATCH);
  delayMicroseconds(2);

  // release -> goes HIGH (via pull-up)
  setPinLow(PIN_LATCH);
  delayMicroseconds(8); // hold HIGH briefly to latch
  // drive low again to return to idle
  setPinHigh(PIN_LATCH);
  delayMicroseconds(4);
  // release to leave in high-Z (optional)
  setPinLow(PIN_LATCH);
}

// initialize pins to safe released state
void initPins()
{
  setPinHigh(PIN_CLOCK);
  setPinHigh(PIN_DATA);
  setPinHigh(PIN_LATCH);
}

/// @brief Get outdoor temperature from HTTP server
/// @return Temperature in Celsius or negative error code <= -100
float getOutdoorTemperature(const String &url)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return -102.0f; // error: no WiFi
  }

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != 200)
  {
    http.end();
    return -101.0f; // error: HTTP fail
  }

  String payload = http.getString();
  http.end();

  int tPos = payload.indexOf("\"temperature\":");
  if (tPos < 0)
    return -101.0f; // error: no temperature field

  int valueStart = tPos + strlen("\"temperature\":");
  int valueEnd = payload.indexOf(",", valueStart);
  if (valueEnd == -1)
    valueEnd = payload.indexOf("}", valueStart);

  String tempStr = payload.substring(valueStart, valueEnd);
  return tempStr.toFloat();
}

bool every5Minuts(bool reset)
{
  static unsigned long lastMillis = 0;
  unsigned long now = millis();
  if (reset)
  {
    lastMillis = now;
    return false;
  }

  if (now - lastMillis >= 300000UL) // 5 minuts
  {
    lastMillis = now;
    return true;
  }
  return false;
}

bool everySecond()
{
  static unsigned long lastMillis = 0;
  unsigned long now = millis();

  if (now - lastMillis >= 1000UL)
  {
    lastMillis = now;
    return true;
  }
  return false;
}

void WifiCheck()
{
  static unsigned long lastWifiCheck = 0;
  static unsigned long lastReconnectAttempt = 0;
  static unsigned long lastAnimToggle = 0;
  static bool animState = false;

  const unsigned long WIFI_CHECK_INTERVAL = 2000;
  const unsigned long WIFI_RECONNECT_INTERVAL = 15000;
  const unsigned long WIFI_ANIM_INTERVAL = 500;

  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED)
  {
    if (now - lastAnimToggle >= WIFI_ANIM_INTERVAL)
    {
      lastAnimToggle = now;
      animState = !animState;

      if (animState)
        setOutdoorDisplay("--");
      else
        setOutdoorDisplay("NULL");
    }
  }

  if (now - lastWifiCheck < WIFI_CHECK_INTERVAL)
    return;

  lastWifiCheck = now;

  if (WiFi.status() != WL_CONNECTED)
  {
    if (now - lastReconnectAttempt >= WIFI_RECONNECT_INTERVAL)
    {
      lastReconnectAttempt = now;

      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_OFF);
      animateStartLCD();
      setOutdoorDisplay("NULL");
      delay(1500);
      WiFi.mode(WIFI_STA);
      WiFi.begin(SSID, PASSWORD);
      delay(500);
      wl_status_t st = WiFi.status();
      if (st != WL_CONNECTED)
      {
        switch (st)
        {
        case WL_NO_SSID_AVAIL:
          setOutdoorDisplay("01");
          break;
        case WL_CONNECT_FAILED:
          setOutdoorDisplay("02");
          break;
        case WL_CONNECTION_LOST:
          setOutdoorDisplay("03");
          break;
        case WL_DISCONNECTED:
          setOutdoorDisplay("04");
          break;
        case WL_IDLE_STATUS:
          setOutdoorDisplay("05");
          break;
        default:
          setOutdoorDisplay("99");
          break;
        }
        delay(2000);
      }
    }
  }
}