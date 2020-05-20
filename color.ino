#include <ArduinoJson.h>

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#include "wifisecrets.h"

// the number of the LED pin
const int blueLedPin = 2;
const int redLedPin = 0;
const int greenLedPin = 4;

// setting PWM properties
const int freq = 5000;
const int blueLedChannel = 10;
const int redLedChannel = 11;
const int greenLedChannel = 12;
const int resolution = 8;

// Tracks the current values to avoid changing needlessly
byte currentRed = 255;
byte currentGreen = 255;
byte currentBlue = 255;

#define MAXIMUM_PATTERN_COMPONENTS 20

// Our webserver running on HTTP
WebServer server(80);

// A class for specifying a color at a specific time (these are interpolated betwen)
class ColorAndTime
{
public:
  // Red, green, and blue values
  byte red;
  byte green;
  byte blue;
  // Time in microseconds
  unsigned long time;

public:
  // Constructor to create one (userful for array initialization)
  ColorAndTime(byte newRed, byte newGreen, byte newBlue, unsigned long newTime)
  {
    red = newRed;
    green = newGreen;
    blue = newBlue;
    time = newTime;
  }

  // Default empty constructor used when specifying an array without initializers
  ColorAndTime()
  {
    red = 0;
    green = 0;
    blue = 0;
    time = 0;
  }

};

// Pattern Array
ColorAndTime pattern[MAXIMUM_PATTERN_COMPONENTS];
size_t statesInPattern;


bool connectToWifi(const char* ssid, const char* password) {
  int waitTime = 0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    waitTime++;
    if (waitTime > 20)
    {
      WiFi.disconnect();
      return false;
    }

  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp32")) {
    Serial.println("MDNS responder started");
  }

  return true;
}

void handleRoot() {
  server.send(200, "text/plain", "hello from esp8266!");
}

void handlePattern() {
  Serial.println("In POST /pattern");
  Serial.println(server.args());
  Serial.println(server.argName(0));
  Serial.println(server.arg(0));

  String body = server.arg("plain");

  DynamicJsonDocument doc(ESP.getMaxAllocHeap());  
  auto error = deserializeJson(doc, body);

  if (error) {
    Serial.print(F("deserializeJson() failed with code "));
    Serial.println(error.c_str());
    server.send(400);
    return;
  }

  Serial.println("Deserialized JSON doc");

  JsonArray patternArray = doc.as<JsonArray>();
  if (!patternArray) {
    server.send(400, "text/plain", "Top level JSON object should be an array of pattern values");
    return;
  }

  if ((patternArray.size() < 2) || (patternArray.size() > MAXIMUM_PATTERN_COMPONENTS)) {
    // TODO: Get this error message to properly report MAXIMUM_PATTERN_COMPONENTS (sprintf)
    server.send(400, "text/plain", "Number of pattern elements should be between 2 and 20");
    return;
  }

  for (size_t i = 0; i < patternArray.size(); i++)
  {
    // TODO: Check for these components in the objects
    // Pull the components out of the objects in the array
    int r = patternArray[i]["r"];
    int g = patternArray[i]["g"];
    int b = patternArray[i]["b"];
    int t = patternArray[i]["t"];

    /*
    Serial.print("R:");
    Serial.print(r);
    Serial.print(" G:");
    Serial.print(g);
    Serial.print(" B:");
    Serial.print(b);
    Serial.print(" T:");
    Serial.println(t);
    */

    pattern[i].red = r;
    pattern[i].green = g;
    pattern[i].blue = b;
    pattern[i].time = t;
  }

  // Set the global to track how many components are actually in the array
  statesInPattern = patternArray.size();

  server.send(200);
}


void initializeDefaultPattern()
{
  // Create a pattern that goes from "black" thru the "rainbow" and back to "black" over 8 seconds
  pattern[0] = {0, 0, 0, 0};
  pattern[1] = {0, 0, 0, 1000000L};
  pattern[2] = {255, 0, 0, 2000000L};
  pattern[3] = {255, 255, 0, 3000000L};
  pattern[4] = {0, 255, 0, 4000000L};
  pattern[5] = {0, 255, 255, 5000000L};
  pattern[6] = {0, 0, 255, 6000000L};
  pattern[7] = {255, 0, 255, 7000000L};
  pattern[8] = {0, 0, 0, 8000000L};

  statesInPattern = 9;
}

void setup()
{
  // Start the serial in case we need to see some output during debugging.
  Serial.begin(115200);

  // Try to connect multiple times due to this being slightly unreliable
  bool connected = false;
  while (!connected)
  {
    connected = connectToWifi(ssid, password);
  }

  // Setup the channels
  ledcSetup(redLedChannel, freq, resolution);
  ledcSetup(greenLedChannel, freq, resolution);
  ledcSetup(blueLedChannel, freq, resolution);

  // Attach the pins to the channels
  ledcAttachPin(redLedPin, redLedChannel);
  ledcAttachPin(greenLedPin, greenLedChannel);
  ledcAttachPin(blueLedPin, blueLedChannel);

  // Write out an initial state
  ledcWrite(redLedChannel, currentRed);
  ledcWrite(greenLedChannel, currentGreen);
  ledcWrite(blueLedChannel, currentBlue);

  // Fill the pattern with our default pattern
  initializeDefaultPattern();

  // Eventually this should be a page with Javascipt to call the REST API for testing
  server.on("/", handleRoot);
  // This page handles RESTful JSON POSTs to update the pattern
  server.on("/pattern", HTTP_POST, handlePattern);

  // Start the server
  server.begin();
}

void updateColorChannel(byte& currentValue, byte newValue, int channel)
{
  // Let's only update if the new value is different from the current value
  if (currentValue != newValue)
  {
    // Update the current value
    currentValue = newValue;
    ledcWrite(channel, newValue);
  }
}

void updateColorsWithPattern(unsigned long currentTime, bool invertValues)
{
  // Figure out which bracket we're in (which two pattern sections are we between)
  size_t colorBracketStart = 0;
  for (size_t index = 0; index < (statesInPattern - 1); index++)
  {
    if (currentTime >= (pattern[index].time) && (currentTime < pattern[index + 1].time))
    {
      colorBracketStart = index;
      break;
    }
  }

  // Figure out how long (in time) the bracket is, and how far into it we are
  float bracketLength = float(pattern[colorBracketStart + 1].time - pattern[colorBracketStart].time);
  float timeIntoBracket = float(currentTime - pattern[colorBracketStart].time);

  // Figure out the linearly interpolated weights of the colors on each end of the bracket
  float percentageColorA = 1.0 - (timeIntoBracket / bracketLength);
  float percentageColorB = 1.0 - percentageColorA;

  byte redValue = byte(percentageColorA * float(pattern[colorBracketStart].red) + percentageColorB * float(pattern[colorBracketStart + 1].red));
  byte greenValue = byte(percentageColorA * float(pattern[colorBracketStart].green) + percentageColorB * float(pattern[colorBracketStart + 1].green));
  byte blueValue = byte(percentageColorA * float(pattern[colorBracketStart].blue) + percentageColorB * float(pattern[colorBracketStart + 1].blue));

  // See if we need to invert the values (good for common anode vs cathode)
  if (invertValues) {
    redValue = 255 - redValue;
    greenValue = 255 - greenValue;
    blueValue = 255 - blueValue;
  }

  // Potentially update the channels 
  updateColorChannel(currentRed, redValue, redLedChannel);
  updateColorChannel(currentGreen, greenValue, greenLedChannel);
  updateColorChannel(currentBlue, blueValue, blueLedChannel);
}

void loop()
{
  // Update colors
  unsigned long currentTime = micros() % pattern[statesInPattern - 1].time;
  updateColorsWithPattern(currentTime, false);

  // Check for REST activity
  server.handleClient();
}
