/*
 *  ElGato Controller
 *
*/
 
#include <Arduino.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
WiFiManager gWifiManager;

char gHostName[64];
boolean gFirstBoot = true;
#define WL_MAC_ADDR_LENGTH 6

// #include <WebServer.h>
// WebServer gWebServer(80);

#include <Arduino_JSON.h>

#include "display.h"
#include "logos.h"
#include "version.h"

// Elgato lights discovered via mDNS (_elg._tcp)
#define MAX_LIGHTS 8
String gLights[MAX_LIGHTS];
int gLightCount = 0;
bool gMdnsStarted = false;
bool gLightsOnline = false;                             // last poll reached a light?

// Re-discover at least this often so stale DHCP IPs self-heal without a reboot
#define DISCOVER_INTERVAL 1800000UL                     // 30 minutes
unsigned long lastDiscoverMillis = 0;

// Hardware Pins
const int ledWifiPin = 4;                       // D3
const int ledOnOffPin = 2;                      // D2

const int buttonOnOffPin = 12;                  // SW2
const int buttonIncreaseBrightnessPin = 13;     // SW3
const int buttonDecreaseBrightnessPin = 15;     // SW5      
const int buttonIncreaseTemperaturePin = 16;    // SW6 (swapped: temp buttons were inverted)
const int buttonDecreaseTemperaturePin = 14;    // SW4 (swapped: temp buttons were inverted)

// Buttons states
int onOffState = HIGH;                          // the current reading from the input pin
int lastOnOffState = LOW;                       // the previous reading from the input pin

int increaseBrightnessState = HIGH;             // the current reading from the input pin
int lastIncreaseBrightnessState = LOW;          // the previous reading from the input pin
int decreaseBrightnessState = HIGH;             // the current reading from the input pin
int lastDecreaseBrightnessState = LOW;          // the previous reading from the input pin

int increaseTemperatureState = HIGH;            // the current reading from the input pin
int lastIncreaseTemperatureState = LOW;         // the previous reading from the input pin
int decreaseTemperatureState = HIGH;            // the current reading from the input pin
int lastDecreaseTemperatureState = LOW;         // the previous reading from the input pin

// Debounce timers
unsigned long lastOnOffDebounceTime = 0;                // the last time the output pin was toggled
unsigned long lastIncreaseBrightnessDebounceTime = 0;   // the last time the output pin was toggled
unsigned long lastDecreaseBrightnessDebounceTime = 0;   // the last time the output pin was toggled
unsigned long lastIncreaseTemperatureDebounceTime = 0;  // the last time the output pin was toggled
unsigned long lastDecreaseTemperatureDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;                       // the debounce time; increase if the output flickers

// Button-press latches: filled by buttonTask (core 0), drained by loop() (core 1).
// A dedicated task samples the buttons continuously so presses are never missed while
// loop() is blocked in network I/O (mDNS discovery / HTTP). Guarded by gButtonMux.
portMUX_TYPE gButtonMux = portMUX_INITIALIZER_UNLOCKED;
int gPendingOnOffToggles = 0;
int gPendingBrightnessDelta = 0;                        // accumulated, in brightness units
int gPendingTemperatureDelta = 0;                       // accumulated, in temperature units
bool gPendingInteraction = false;
TaskHandle_t gButtonTaskHandle = nullptr;
void buttonTask(void *);                                // defined below, started in setup()

unsigned long previousPollMillis = 0;                   // the last time poll was done
unsigned long pollDelay = 300000;

unsigned long previousActionMillis = 0;                 // the last time Action was sent
unsigned long actionDelay = 150;                        // batch rapid taps, but stay responsive

bool action = false;

// Display power management (anti burn-in): dim then blank the OLED when idle,
// wake instantly on any button press. The controller sits idle most of the day,
// so a mostly-static image lit 24/7 is what wears out the "always on" pixels.
#define DISPLAY_DIM_TIMEOUT 15000UL                     // dim after 15s idle
#define DISPLAY_OFF_TIMEOUT 60000UL                     // blank after 60s idle
unsigned long lastInteractionMillis = 0;                // last button press
bool gDisplayOn = true;
bool gDisplayDimmed = false;

#define BRIGHT_MIN 0
#define BRIGHT_MAX 100
#define BRIGHT_STEP 5

#define TEMP_MIN 143
#define TEMP_MAX 344
#define TEMP_STEP 20

// Initial Status
int onOffLightState = 0;                                // 0 - 1
int brightnessState = 0;                                // 0 - 100
int temperatureState = 344;                             // 143 - 344

void setupWifiManager() {

  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  
  sprintf(gHostName, "ElGato-%02X%02X", mac[WL_MAC_ADDR_LENGTH - 2], mac[WL_MAC_ADDR_LENGTH - 1]);
  Serial.printf("Wifi Manager Setup - Name: %s\n", gHostName );
  
  // reset settings - wipe credentials for testing
  // gWifiManager.resetSettings();
  gWifiManager.setConfigPortalBlocking(false);

  //automatically connect using saved credentials if they exist
  //If connection fails it starts an access point with the specified name
  if(gWifiManager.autoConnect(gHostName)){
    Serial.println("Wi-Fi connected using saved credentials");
    gFirstBoot = false;
  }
  else {
    Serial.println("Wi-Fi manager portal running");
  }
}

void WiFiEvent(WiFiEvent_t event)
{
  Serial.printf("[WiFi-event] event: %d -- ", event);

  switch (event) {
    case ARDUINO_EVENT_WIFI_READY: 
      Serial.println("WiFi interface ready");
      break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
      Serial.println("Completed scan for access points");
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("WiFi client started");
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      Serial.println("WiFi clients stopped");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("Connected to access point");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("Disconnected from WiFi access point");
      // previousWifiReconnectMillis = millis();
      break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
      Serial.println("Authentication mode of access point has changed");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("Obtained IP address: ");
      Serial.println(WiFi.localIP());
      previousPollMillis = 0;
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      Serial.println("Lost IP address and IP address is reset to 0");
      break;
    case ARDUINO_EVENT_WPS_ER_SUCCESS:
      Serial.println("WiFi Protected Setup (WPS): succeeded in enrollee mode");
      break;
    case ARDUINO_EVENT_WPS_ER_FAILED:
      Serial.println("WiFi Protected Setup (WPS): failed in enrollee mode");
      break;
    case ARDUINO_EVENT_WPS_ER_TIMEOUT:
      Serial.println("WiFi Protected Setup (WPS): timeout in enrollee mode");
      break;
    case ARDUINO_EVENT_WPS_ER_PIN:
      Serial.println("WiFi Protected Setup (WPS): pin code in enrollee mode");
      break;
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("WiFi access point started");
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      Serial.println("WiFi access point stopped");
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Serial.println("Client connected");
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.println("Client disconnected");
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      Serial.println("Assigned IP address to client");
      break;
    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
      Serial.println("Received probe request");
      break;
    case ARDUINO_EVENT_WIFI_AP_GOT_IP6:
      Serial.println("AP IPv6 is preferred");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
      Serial.println("STA IPv6 is preferred");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP6:
      Serial.println("Ethernet IPv6 is preferred");
      break;
    case ARDUINO_EVENT_ETH_START:
      Serial.println("Ethernet started");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("Ethernet stopped");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("Ethernet connected");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("Ethernet disconnected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.println("Obtained IP address");
      break;
    default: 
      break;
  }
}


// Turn the panel back to full brightness. The GDDRAM buffer keeps being
// refreshed by updateDisplay() even while blanked, so content is current.
void wakeDisplay() {
  if (!gDisplayOn) {
    display.ssd1306_command(SSD1306_DISPLAYON);
    gDisplayOn = true;
  }
  if (gDisplayDimmed) {
    display.dim(false);
    gDisplayDimmed = false;
  }
}

// Dim after a short idle, fully blank after a longer idle.
void manageDisplayPower(unsigned long now) {
  unsigned long idle = now - lastInteractionMillis;

  if (idle >= DISPLAY_OFF_TIMEOUT) {
    if (gDisplayOn) {
      display.ssd1306_command(SSD1306_DISPLAYOFF);
      gDisplayOn = false;
    }
  } else if (idle >= DISPLAY_DIM_TIMEOUT) {
    if (!gDisplayDimmed) {
      display.dim(true);
      gDisplayDimmed = true;
    }
  }
}

void updateDisplay() {

  display.clearDisplay();
  
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(60,2);             // Start at top-left corner
  display.print("ElGato");

  display.setCursor(56,21);
  display.printf("%3d%%", brightnessState);

  long temp = 7000 - ((temperatureState - TEMP_MIN) * (7000-2900) / (TEMP_MAX - TEMP_MIN));
  display.setCursor(95,21);
  display.printf("%dK", temp);

  #define BAR_LEN 36  
  #define BAR_SPACE 5  

  int brightBar = (brightnessState * (BAR_LEN) / 100);
  display.drawLine(127 - BAR_LEN - BAR_SPACE, 30, 127 - BAR_LEN - BAR_SPACE, 31, SSD1306_WHITE);
  display.drawLine(127 - BAR_LEN - BAR_SPACE - BAR_LEN, 30, 127 - BAR_LEN - BAR_SPACE - BAR_LEN, 31, SSD1306_WHITE);
  display.drawLine(127 - BAR_LEN - BAR_SPACE - BAR_LEN, 30, 127 - BAR_LEN - BAR_SPACE - BAR_LEN + brightBar, 30, SSD1306_WHITE);
  display.drawLine(127 - BAR_LEN - BAR_SPACE - BAR_LEN, 31, 127 - BAR_LEN - BAR_SPACE - BAR_LEN + brightBar, 31, SSD1306_WHITE);

  // Kelvin is inverse to the raw Elgato value, so invert the bar to match the
  // displayed K number (low K -> short bar, high K -> long bar).
  int tempBar = BAR_LEN - ((temperatureState - TEMP_MIN) * (BAR_LEN) / (TEMP_MAX - TEMP_MIN));
  display.drawLine(127, 30, 127, 31, SSD1306_WHITE);
  display.drawLine(127 - BAR_LEN, 30, 127 - BAR_LEN , 31, SSD1306_WHITE);
  display.drawLine(127 - BAR_LEN, 30, 127 - BAR_LEN + tempBar, 30, SSD1306_WHITE);
  display.drawLine(127 - BAR_LEN, 31, 127 - BAR_LEN + tempBar, 31, SSD1306_WHITE);

  if (WiFi.status() == WL_CONNECTED) {
    display.drawBitmap(127 - WIFI_WIDTH, 0, wifi_logo, WIFI_WIDTH, WIFI_HEIGHT, SSD1306_WHITE);
    digitalWrite(ledWifiPin, HIGH);
  } else{
    digitalWrite(ledWifiPin, LOW);
  }

  if (onOffLightState == 0) {
    display.drawBitmap(10, 4, cat_off_logo, CAT_WIDTH, CAT_HEIGHT, SSD1306_WHITE);
    digitalWrite(ledOnOffPin, LOW);
  } else {
    display.drawBitmap(10, 4, cat_on_logo, CAT_WIDTH, CAT_HEIGHT, SSD1306_WHITE);
    digitalWrite(ledOnOffPin, HIGH);
  }

  // Lights connectivity indicator (top-left): filled dot = reachable,
  // hollow circle with a slash = unreachable (stale IP / light offline).
  if (gLightsOnline) {
    display.fillCircle(3, 4, 2, SSD1306_WHITE);
  } else {
    display.drawCircle(3, 4, 3, SSD1306_WHITE);
    display.drawLine(1, 6, 5, 2, SSD1306_WHITE);
  }

  display.display();
}

// Brief power-on splash: ElGato (cat) logo on the left, firmware/build/network
// info on the right. Shown once at boot.
void showSplashScreen() {
  display.clearDisplay();

  // ElGato logo
  display.drawBitmap(0, 2, cat_on_logo, CAT_WIDTH, CAT_HEIGHT, SSD1306_WHITE);

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  const int x = 30;  // text column, to the right of the logo
  display.setCursor(x, 0);
  display.print("ElGato");
  display.setCursor(x, 8);
  display.printf("v%s %s", FW_VERSION, GIT_REV);
  display.setCursor(x, 16);
  if (WiFi.status() == WL_CONNECTED) {
    display.print(WiFi.localIP().toString());
  } else {
    display.print("no IP yet");
  }
  display.setCursor(x, 24);
  display.print(__DATE__);

  display.display();
  delay(4000);
}


void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  Serial.printf("ElGato Controller v%s (git %s, built %s %s)\n",
                FW_VERSION, GIT_REV, __DATE__, __TIME__);

  // Setup hardware oins
  pinMode(ledWifiPin, OUTPUT);
  digitalWrite(ledWifiPin, LOW);

  pinMode(ledOnOffPin, OUTPUT);
  digitalWrite(ledOnOffPin, LOW);

  pinMode(buttonOnOffPin, INPUT_PULLUP);
  pinMode(buttonIncreaseBrightnessPin, INPUT_PULLUP);
  pinMode(buttonDecreaseBrightnessPin, INPUT_PULLUP);
  pinMode(buttonIncreaseTemperaturePin, INPUT_PULLUP);
  pinMode(buttonDecreaseTemperaturePin, INPUT_PULLUP);

  // Sample buttons in a dedicated task on core 0 so presses are never missed
  // while loop() (core 1) is blocked in network I/O.
  xTaskCreatePinnedToCore(buttonTask, "buttonTask", 2048, nullptr, 2, &gButtonTaskHandle, 0);

  // Setup Wifi
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  // Disable modem power save: it drops multicast packets, which makes mDNS
  // discovery flaky and intermittent. This board is mains-powered, so the
  // extra radio-on current is a non-issue.
  WiFi.setSleep(false);
  WiFi.onEvent(WiFiEvent);

  // Reset Wifi setting if OnOff pressed for 5 seconds
  if (digitalRead(buttonOnOffPin) == LOW) {
    delay(5000);
    if (digitalRead(buttonOnOffPin) == LOW) {     
      gWifiManager.resetSettings();
      Serial.println("Wifi reset!");
    }
  }
  setupWifiManager();

  // gWebServer.begin();

  setupDisplay();
  showSplashScreen();
  lastInteractionMillis = millis();
  updateDisplay();
}

void discoverLights() {

  if (WiFi.status() != WL_CONNECTED) return;

  // mDNS responder must be running before we can query
  if (!gMdnsStarted) {
    if (MDNS.begin(gHostName)) {
      gMdnsStarted = true;
    } else {
      Serial.println("discoverLights - MDNS.begin() failed");
      return;
    }
  }

  // Elgato Key Lights advertise the service _elg._tcp. mDNS answers trickle in
  // across queries and not every light replies to the first one, so run a couple
  // of passes and merge unique IPs rather than trusting a single query. Each pass
  // blocks ~3s, so 2 passes caps the blocking window at ~6s (the buttonTask keeps
  // sampling throughout regardless).
  gLightCount = 0;
  for (int pass = 1; pass <= 2 && gLightCount < MAX_LIGHTS; pass++) {
    Serial.printf("discoverLights - querying _elg._tcp (pass %d/2) ...\n", pass);
    int n = MDNS.queryService("elg", "tcp");

    for (int i = 0; i < n && gLightCount < MAX_LIGHTS; i++) {
      String host = MDNS.hostname(i);
      IPAddress ip = MDNS.IP(i);

      // The A record sometimes lags the SRV/PTR answer, leaving IP as 0.0.0.0.
      // Resolve the hostname directly as a fallback.
      if (ip == IPAddress((uint32_t)0)) {
        ip = MDNS.queryHost(host.c_str());
      }
      if (ip == IPAddress((uint32_t)0)) {
        Serial.printf("discoverLights - skipping %s (could not resolve IP)\n", host.c_str());
        continue;
      }

      // Skip IPs we already recorded in an earlier pass.
      String ipStr = ip.toString();
      bool known = false;
      for (int j = 0; j < gLightCount; j++) {
        if (gLights[j] == ipStr) { known = true; break; }
      }
      if (known) continue;

      gLights[gLightCount] = ipStr;
      Serial.printf("discoverLights - found light %d: %s (%s:%d)\n",
                    gLightCount,
                    host.c_str(),
                    ipStr.c_str(),
                    MDNS.port(i));
      gLightCount++;
    }
  }

  // mDNS can come back empty when multicast is filtered on the lights' VLAN
  // (seen on this network: the ESP32 is on the lights' subnet yet gets no
  // responses). The lights' IPs are stable, so fall back to a known list rather
  // than leaving the controller with nothing to talk to. mDNS still wins
  // whenever it does return results.
  if (gLightCount == 0) {
    static const char *kFallbackLights[] = { "10.30.1.22", "10.30.1.221" };
    for (unsigned i = 0; i < sizeof(kFallbackLights) / sizeof(kFallbackLights[0]) && gLightCount < MAX_LIGHTS; i++) {
      gLights[gLightCount++] = kFallbackLights[i];
    }
    Serial.printf("discoverLights - mDNS found 0; using %d fallback IP(s)\n", gLightCount);
  }

  Serial.printf("discoverLights - %d light(s) found\n", gLightCount);
}

// Returns true if the light responded; also updates gLightsOnline so the
// display can show whether the lights are reachable.
bool getLightState() {

  JSONVar myJSONPayload;

  if (gLightCount == 0) {
    Serial.println("getLightState - no lights discovered yet");
    gLightsOnline = false;
    return false;
  }

  WiFiClient client;
  HTTPClient httpClient;
  int httpReturnCode;

  // Get status from the first discovered light
  Serial.printf("getLightState - GET http://%s:9123/elgato/lights (RSSI %d dBm)\n", gLights[0].c_str(), WiFi.RSSI());
  httpClient.begin(client, "http://" + gLights[0] + ":9123/elgato/lights"); //HTTP
  httpClient.setConnectTimeout(5000);  // weak-signal links can take ~2s to respond
  httpClient.setTimeout(5000);
  httpClient.addHeader("Content-Type", "application/json");
  httpReturnCode = httpClient.GET();

  bool ok = false;
  if (httpReturnCode == HTTP_CODE_OK) {
    const String& payload = httpClient.getString();
    Serial.print("getLightState - Status: ");
    Serial.print(HTTP_CODE_OK);
    Serial.print(" - ");
    Serial.println(payload);
    myJSONPayload = JSON.parse(payload);

    onOffLightState = (int)myJSONPayload["lights"][0]["on"];
    brightnessState = (int)myJSONPayload["lights"][0]["brightness"];
    temperatureState = (int)myJSONPayload["lights"][0]["temperature"];

    ok = true;
  } else {
    Serial.printf("getLightState - GET... failed, error: %s\n", httpClient.errorToString(httpReturnCode).c_str());
  }
  httpClient.end();

  gLightsOnline = ok;
  return ok;
}

void setLightState(String ip, JSONVar payload) {

  int httpReturnCode;

  WiFiClient client;
  HTTPClient httpClient;

  Serial.print ("PUT: http://" + ip + ":9123/elgato/lights - ");
  Serial.println(JSON.stringify(payload));

  bool exitloop = false;
  int counter = 0;

  while (exitloop == false) {
    httpClient.begin(client, "http://" + ip + ":9123/elgato/lights");
    httpClient.setConnectTimeout(5000);  // weak-signal links can take ~2s to respond
    httpClient.setTimeout(5000);
    httpClient.addHeader("Content-Type", "application/json");
    httpReturnCode = httpClient.PUT(JSON.stringify(payload));

    // httpReturnCode will be negative on error
    if (httpReturnCode > 0) {
      if (httpReturnCode == HTTP_CODE_OK) {
        const String& payload = httpClient.getString();
        Serial.print("Status: ");
        Serial.println(HTTP_CODE_OK);

        exitloop = true;
      }
    } else {
      Serial.printf("[HTTP] PUT... failed, error: %s\n", httpClient.errorToString(httpReturnCode).c_str());
    }
    counter++;
    // One retry at most: with the 2s timeout, a dead light now blocks ~4s, not ~20s.
    if (counter > 1) exitloop = true;

    httpClient.end();
  }
}

// Pushes the current (already-updated) state to every light. Button presses
// apply their delta to the local state immediately in loop() for instant
// on-screen feedback; this just sends that state over the network, batched.
void actionLight() {
  JSONVar myJSONPayload;

  myJSONPayload["lights"][0]["on"] = onOffLightState;
  myJSONPayload["lights"][0]["brightness"] = brightnessState;
  myJSONPayload["lights"][0]["temperature"] = temperatureState;

  for (int i = 0; i < gLightCount; i++) {
    setLightState(gLights[i], myJSONPayload);
  }

}

// Debounced edge detector. Returns actionResult once per press (on the release
// edge, preserving the original feel), else 0. Runs inside buttonTask on core 0;
// the state/debounce pointers it mutates are owned exclusively by that task, and
// it has no side effects on loop()/core-1 globals.
int sampleButton(int pin, unsigned long now, int * currentButtonState, int * lastButtonState, unsigned long * debounceTime, int actionResult) {

  int result = 0;
  int pinState = digitalRead(pin);
  if (pinState != *lastButtonState) {
    *debounceTime = now;
  }
  if ((now - *debounceTime) > debounceDelay) {
    if (pinState != *currentButtonState) {
      *currentButtonState = pinState;
      if (*currentButtonState == HIGH) {
        result = actionResult;
      }
    }
  }
  *lastButtonState = pinState;
  return result;
}

// Dedicated input task (pinned to core 0). Samples all five buttons every ~5 ms
// and latches detected presses into shared counters that loop() (core 1) drains.
// Because this runs independently of loop(), presses are captured even while
// loop() is blocked for seconds in mDNS discovery or HTTP calls.
void buttonTask(void *) {
  for (;;) {
    unsigned long now = millis();

    int on   = sampleButton(buttonOnOffPin, now, &onOffState, &lastOnOffState, &lastOnOffDebounceTime, 1);
    int bUp  = sampleButton(buttonIncreaseBrightnessPin, now, &increaseBrightnessState, &lastIncreaseBrightnessState, &lastIncreaseBrightnessDebounceTime, BRIGHT_STEP);
    int bDn  = sampleButton(buttonDecreaseBrightnessPin, now, &decreaseBrightnessState, &lastDecreaseBrightnessState, &lastDecreaseBrightnessDebounceTime, BRIGHT_STEP);
    int tUp  = sampleButton(buttonIncreaseTemperaturePin, now, &increaseTemperatureState, &lastIncreaseTemperatureState, &lastIncreaseTemperatureDebounceTime, TEMP_STEP);
    int tDn  = sampleButton(buttonDecreaseTemperaturePin, now, &decreaseTemperatureState, &lastDecreaseTemperatureState, &lastDecreaseTemperatureDebounceTime, TEMP_STEP);

    if (on || bUp || bDn || tUp || tDn) {
      taskENTER_CRITICAL(&gButtonMux);
      gPendingOnOffToggles     += on;
      gPendingBrightnessDelta  += (bUp - bDn);
      gPendingTemperatureDelta += (tUp - tDn);
      gPendingInteraction = true;
      taskEXIT_CRITICAL(&gButtonMux);
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void loop()
{
  unsigned long localMillis = millis();

  // Drain the button presses latched by buttonTask (core 0). Because that task
  // samples continuously, presses are captured even when a previous loop
  // iteration was blocked for seconds in network I/O.
  int onOffToggles, brightnessDelta, temperatureDelta;
  bool interaction;
  taskENTER_CRITICAL(&gButtonMux);
  onOffToggles     = gPendingOnOffToggles;     gPendingOnOffToggles = 0;
  brightnessDelta  = gPendingBrightnessDelta;  gPendingBrightnessDelta = 0;
  temperatureDelta = gPendingTemperatureDelta; gPendingTemperatureDelta = 0;
  interaction      = gPendingInteraction;      gPendingInteraction = false;
  taskEXIT_CRITICAL(&gButtonMux);

  // Apply presses to the local state immediately so the display reacts instantly;
  // the actual light PUT is sent (batched) a moment later.
  bool changed = false;

  // An odd number of toggles captured during the window = one net toggle.
  if (onOffToggles % 2 != 0) {
    onOffLightState = (onOffLightState == 0) ? 1 : 0;
    // Turning on at 0% brightness is invisible; bump to a usable minimum.
    if (onOffLightState == 1 && brightnessState == 0) {
      brightnessState = 10;
    }
    changed = true;
  }

  if (brightnessDelta != 0) {
    brightnessState += brightnessDelta;
    if (brightnessState < BRIGHT_MIN) brightnessState = BRIGHT_MIN;
    if (brightnessState > BRIGHT_MAX) brightnessState = BRIGHT_MAX;
    changed = true;
  }

  if (temperatureDelta != 0) {
    temperatureState += temperatureDelta;
    if (temperatureState < TEMP_MIN) temperatureState = TEMP_MIN;
    if (temperatureState > TEMP_MAX) temperatureState = TEMP_MAX;
    changed = true;
  }

  // Trigger the batched network send (previously set inside evaluateButton).
  if (changed) {
    action = true;
  }

  // Any button press wakes the display and resets the idle timer (anti burn-in)
  if (interaction) {
    lastInteractionMillis = localMillis;
    wakeDisplay();
  }
  manageDisplayPower(localMillis);

  updateDisplay();

  // Do we need to send action? 
  if (((localMillis - previousActionMillis) > actionDelay && action == true) && WiFi.status() == WL_CONNECTED) {
    previousActionMillis = localMillis; 
    action = false;    
    
    Serial.println("loop - calling actionLight()...");
    actionLight();
  }

  // Polling light. While no lights are known (e.g. a transient discovery miss at
  // startup) retry sooner than the full poll interval, but not so aggressively
  // that the ~6s discovery dominates the loop.
  unsigned long effectivePollDelay = (gLightCount == 0) ? 90000UL : pollDelay;
  if (((localMillis - previousPollMillis) > effectivePollDelay || previousPollMillis == 0 ) && WiFi.status() == WL_CONNECTED) {
    previousPollMillis = localMillis;

    // (Re)discover lights if we don't know any yet, or on a periodic interval
    // so a light that changed IP (DHCP) is picked up without a reboot.
    bool discoveredThisPoll = false;
    if (gLightCount == 0 || (localMillis - lastDiscoverMillis) > DISCOVER_INTERVAL) {
      discoverLights();
      lastDiscoverMillis = localMillis;
      discoveredThisPoll = true;
    }

    Serial.println("loop - calling getLightState()...");
    if (!getLightState() && gLightCount > 0 && !discoveredThisPoll) {
      // A known light stopped answering - most likely a stale IP. Force a fresh
      // discovery and retry once so it self-heals. Skipped when we already
      // discovered this poll, to avoid two ~6s discovery blocks back-to-back.
      Serial.println("loop - light unreachable, re-discovering...");
      discoverLights();
      lastDiscoverMillis = localMillis;
      getLightState();
    }
  }

  while (WiFi.status() != WL_CONNECTED && gFirstBoot == false )
  {
    Serial.println("Reconnect Wifi!");
    setupWifiManager();
  }

  gWifiManager.process();
  // gWebServer.handleClient();

  updateDisplay();
  
  delay(10);

}
