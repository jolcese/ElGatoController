/*
 *  ElGato Controller
 *
*/

// To Do:
//
//  NTP
//  ~Wifi Portal~
//  Dynamic lights config/ Web Server
//  Shortcut reboot
//  Brightness
//  ~OLED Status~
//  Faster REST
//  FIx BAR size
 
#include <Arduino.h>

#include <WiFi.h>
#include <HTTPClient.h>

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

const String sLight1 = "192.168.30.137";
const String sLight2 = "192.168.30.161";

// Hardware Pins
const int ledWifiPin = 4;                       // D3
const int ledOnOffPin = 2;                      // D2

const int buttonOnOffPin = 12;                  // SW2
const int buttonIncreaseBrightnessPin = 13;     // SW3
const int buttonDecreaseBrightnessPin = 15;     // SW5      
const int buttonIncreaseTemperaturePin = 14;    // SW4
const int buttonDecreaseTemperaturePin = 16;    // SW6  

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

unsigned long previousPollMillis = 0;                   // the last time poll was done
unsigned long pollDelay = 300000;

unsigned long previousActionMillis = 0;                 // the last time Action was sent
unsigned long actionDelay = 600;

bool action = false;

bool actionOnOff = false;
int actionBrightnessDelta = 0;
int actionTemperatureDelta = 0;

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

  int tempBar = ((temperatureState - TEMP_MIN) * (BAR_LEN) / (TEMP_MAX - TEMP_MIN));
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

  display.display();
}


void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println();

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

  // Setup Wifi
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP   
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
  updateDisplay();
}

void getLightState() {
  
  JSONVar myJSONPayload;

  WiFiClient client;
  HTTPClient httpClient;
  int httpReturnCode;

  // Get status from Left Light
  Serial.println("getLightState - GET: http://" + sLight1 + ":9123/elgato/lights");
  httpClient.begin(client, "http://" + sLight1 + ":9123/elgato/lights"); //HTTP
  httpClient.addHeader("Content-Type", "application/json");
  httpReturnCode = httpClient.GET();

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

  } else {
    Serial.printf("getLightState - GET... failed, error: %s\n", httpClient.errorToString(httpReturnCode).c_str());
  }
  httpClient.end();
}

void setLightState(String ip, JSONVar payload) {

  int httpReturnCode;

  WiFiClient client;
  HTTPClient httpClient;

  Serial.print ("PUT: http://" + ip + ":9123/elgato/lights - ");
  Serial.println(JSON.stringify(payload));
  httpClient.begin(client, "http://" + ip + ":9123/elgato/lights");
  httpClient.addHeader("Content-Type", "application/json");
  httpReturnCode = httpClient.PUT(JSON.stringify(payload));

  // httpReturnCode will be negative on error
  if (httpReturnCode > 0) {
    if (httpReturnCode == HTTP_CODE_OK) {
      const String& payload = httpClient.getString();
      Serial.print("Status: ");
      Serial.println(HTTP_CODE_OK);
    }
  } else {
    Serial.printf("[HTTP] PUT... failed, error: %s\n", httpClient.errorToString(httpReturnCode).c_str());
  }
  httpClient.end();
}

void actionLight() {
  JSONVar myJSONPayload;

  getLightState();

  // Fill payload with all data
  myJSONPayload["lights"][0]["on"] = onOffLightState;
  myJSONPayload["lights"][0]["brightness"] = brightnessState;
  myJSONPayload["lights"][0]["temperature"] = temperatureState;

  // On Off
  if (actionOnOff == true) {
    // Toggle state
    if (onOffLightState == 0) {
      onOffLightState = 1;
      myJSONPayload["lights"][0]["on"] = 1;
    } else {
      onOffLightState = 0;
      myJSONPayload["lights"][0]["on"] = 0;
    }
  }
  actionOnOff = false;

  // Brightness
  if (actionBrightnessDelta != 0) {
    brightnessState += actionBrightnessDelta;

    if (brightnessState < BRIGHT_MIN) {
      brightnessState = BRIGHT_MIN;
    }
    if (brightnessState > BRIGHT_MAX) {
      brightnessState = BRIGHT_MAX;
    }      
    myJSONPayload["lights"][0]["brightness"] = brightnessState;
  }
  actionBrightnessDelta = 0;

  // Temperature
  if (actionTemperatureDelta != 0) {
    temperatureState += actionTemperatureDelta;

    if (temperatureState < TEMP_MIN) {
      temperatureState = TEMP_MIN;
    }
    if (temperatureState > TEMP_MAX) {
      temperatureState = TEMP_MAX;
    }      
    myJSONPayload["lights"][0]["temperature"] = temperatureState;
  }
  actionTemperatureDelta = 0;

  setLightState(sLight1, myJSONPayload);
  setLightState(sLight2, myJSONPayload);

}

int evaluateButton(int pin, unsigned long millis, int * currentButtonState, int * lastButtonState, unsigned long * debounceTime, int actionResult) {

  int result = 0;
  int pinState = digitalRead(pin);
  if (pinState != *lastButtonState) {
    *debounceTime = millis;
  }
  if ((millis - *debounceTime) > debounceDelay) {
    if (pinState != *currentButtonState) {
      *currentButtonState = pinState;
      if (*currentButtonState == HIGH) {
        action = true;
        previousActionMillis = millis;
        result = actionResult;
      }
    }
  }
  *lastButtonState = pinState;
  return result;
}

void loop()
{
  unsigned long localMillis = millis();

  // OnOff Button
  actionOnOff = actionOnOff || evaluateButton(buttonOnOffPin,
                                              localMillis, 
                                              & onOffState, 
                                              & lastOnOffState, 
                                              & lastOnOffDebounceTime, 
                                              1);

  // IncreaseBrightness Button
  actionBrightnessDelta += evaluateButton(buttonIncreaseBrightnessPin,
                                          localMillis, 
                                          & increaseBrightnessState, 
                                          & lastIncreaseBrightnessState, 
                                          & lastIncreaseBrightnessDebounceTime, 
                                          BRIGHT_STEP);

  // DecreaseBrightness Button
  actionBrightnessDelta -= evaluateButton(buttonDecreaseBrightnessPin,
                                          localMillis, 
                                          & decreaseBrightnessState, 
                                          & lastDecreaseBrightnessState, 
                                          & lastDecreaseBrightnessDebounceTime, 
                                          BRIGHT_STEP);

  // IncreaseTemperature Button
  actionTemperatureDelta += evaluateButton(buttonIncreaseTemperaturePin,
                                          localMillis, 
                                          & increaseTemperatureState, 
                                          & lastIncreaseTemperatureState, 
                                          & lastIncreaseTemperatureDebounceTime, 
                                          TEMP_STEP);

  // DecreaseTemperature Button
  actionTemperatureDelta -= evaluateButton(buttonDecreaseTemperaturePin,
                                          localMillis, 
                                          & decreaseTemperatureState, 
                                          & lastDecreaseTemperatureState, 
                                          & lastDecreaseTemperatureDebounceTime, 
                                          TEMP_STEP);

  updateDisplay();

  // Do we need to send action? 
  if (((localMillis - previousActionMillis) > actionDelay && action == true) && WiFi.status() == WL_CONNECTED) {
    previousActionMillis = localMillis; 
    action = false;    
    
    Serial.println("loop - calling actionLight()...");
    actionLight();
  }

  // Polling light 
  if (((localMillis - previousPollMillis) > pollDelay || previousPollMillis == 0 ) && WiFi.status() == WL_CONNECTED) {
    previousPollMillis = localMillis;    

    Serial.println("loop - calling getLightState()...");
    getLightState();
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
