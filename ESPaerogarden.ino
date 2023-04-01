#include <elapsedMillis.h>
elapsedMillis displayUpdate;
elapsedMillis ntpUpdate;
elapsedMillis deviceUpdate;
elapsedMillis fileUpdate;
elapsedMillis pumpUpdate;
elapsedMillis lightUpdate;

int ntpInterval = 3600000;
int displayInterval = 1000;
int deviceInterval = 5000;
int fileInterval = 5000;
int firstRun = 1;
long pumpInterval = 5;
long lightInterval = 5;

#include "LittleFS.h"
#define SPIFFS LittleFS

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RESET);

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESPAsyncTCP.h>
#include <Hash.h>
#include <FS.h>
#include <ESPAsyncWebServer.h>
AsyncWebServer server(80);
#include <ArduinoOTA.h>

const char* ssid = "hotcockmachine";
const char* password = "starbucks";

#include <NTPClient.h>
String timeString;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

char *weekdays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
String AMPM = "AM";

int pumpPin = 13;
int lightPIN = 15;

const char* PARAM_PUMPON = "pumpOn";
const char* PARAM_PUMPOFF = "pumpOff";
const char* PARAM_LIGHTON = "lightOn";
const char* PARAM_LIGHTOFF = "lightOff";


const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>Aerogarden Settings</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script>
    function submitMessage() {
      alert("Saved value to Aerogarden Controller");
      setTimeout(function(){ document.location.reload(false); }, 500);   
    }
  </script></head><body>
  <form action="/get" target="hidden-form">
  <h2>All times listed are in seconds</h2><br>
  Pump On (current value %pumpOn%): <input type="number" name="pumpOn">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form><br>
  <form action="/get" target="hidden-form">
    Pump Off (current value %pumpOff%): <input type="number" name="pumpOff">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form><br>
  <form action="/get" target="hidden-form">
    Light On (current value %lightOn%): <input type="number" name="lightOn">
    <input type="submit" value="Submit" onclick="submitMessage()">
</form><br>
  <form action="/get" target="hidden-form">
    Light Off (current value %lightOff%): <input type="number" name="lightOff">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form>
  <iframe style="display:none" name="hidden-form"></iframe>
</body></html>)rawliteral";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
    Serial.println("- empty file or failed to open file");
    return String();
  }
  Serial.println("- read from file:");
  String fileContent;
  while(file.available()){
    fileContent+=String((char)file.read());
  }
  file.close();
  Serial.println(fileContent);
  return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, "w");
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
  file.close();
}

// Replaces placeholder with stored values
String processor(const String& var){
  //Serial.println(var);
  if(var == "pumpOn"){
    return readFile(SPIFFS, "/pumpOn.txt");
  }
  else if(var == "pumpOff"){
    return readFile(SPIFFS, "/pumpOff.txt");
  }
  else if(var == "lightOn"){
    return readFile(SPIFFS, "/lightOn.txt");
  }  
  else if(var == "lightOff"){
    return readFile(SPIFFS, "/lightOff.txt");
  }
  return String();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("WiFiGarden");

  // No authentication by default
  ArduinoOTA.setPassword("guide7088");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.display();
  delay(2000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.print("IP: "); display.println(WiFi.localIP());
  display.display();

  if(!LittleFS.begin()){
            Serial.println("An Error has occurred while mounting LittleFS");
  }

  pinMode(13, OUTPUT);
  pinMode(15, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(15, HIGH);
  digitalWrite(13, HIGH);
  digitalWrite(LED_BUILTIN, LOW);
  delay(4000);
  digitalWrite(15, LOW);
  digitalWrite(13, LOW);
  digitalWrite(LED_BUILTIN, HIGH);

  timeClient.begin();
  timeClient.setTimeOffset(-18000);

  // Send web page with input fields to client
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html, processor);
  });

  // Send a GET request to <ESP_IP>/get?pumpOn=<inputMessage>
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    // GET pumpOn value on <ESP_IP>/get?pumpOn=<inputMessage>
    if (request->hasParam(PARAM_PUMPON)) {
      inputMessage = request->getParam(PARAM_PUMPON)->value();
      writeFile(SPIFFS, "/pumpOn.txt", inputMessage.c_str());
    }
    // GET pumpOff value on <ESP_IP>/get?pumpOff=<inputMessage>
    else if (request->hasParam(PARAM_PUMPOFF)) {
      inputMessage = request->getParam(PARAM_PUMPOFF)->value();
      writeFile(SPIFFS, "/pumpOff.txt", inputMessage.c_str());
    }
    // GET lightOn value on <ESP_IP>/get?lightOn=<inputMessage>
    else if (request->hasParam(PARAM_LIGHTON)) {
      inputMessage = request->getParam(PARAM_LIGHTON)->value();
      writeFile(SPIFFS, "/lightOn.txt", inputMessage.c_str());
    }
    // GET lightOff value on <ESP_IP>/get?lightOff=<inputMessage>
    else if (request->hasParam(PARAM_LIGHTOFF)) {
      inputMessage = request->getParam(PARAM_LIGHTOFF)->value();
      writeFile(SPIFFS, "/lightOff.txt", inputMessage.c_str());
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    //request->send(200, "text/text", inputMessage);
  });
  server.onNotFound(notFound);
  server.begin();
}

void updateNTP() {
  timeClient.update();
}

void updateDisplay() {
  String timeMinFixed;
  String timeSecFixed;
  String pumpState;
  String lightState;
  int timeHour=(timeClient.getHours());
  if (timeHour > 12) {
    timeHour = timeHour - 12;
    AMPM = "PM";
  } else {
    AMPM = "AM";
  }
  int timeMin=(timeClient.getMinutes());
  if (timeMin < 10) {
    timeMinFixed = "0" + timeMin;
  } else {
    timeMinFixed = timeMin;
  }
  int timeSec=(timeClient.getSeconds());
  if (timeSec < 10) {
    timeSecFixed = "0" + timeSec;
  } else {
    timeSecFixed = timeSec;
  }
  String timeDay = weekdays[timeClient.getDay()];
  timeString =  timeDay + " " + timeHour + ":" + timeMinFixed + " " + AMPM;

  if (digitalRead(pumpPin)) {
    pumpState = "On";
  } else {
    pumpState = "Off";
  }
  if (digitalRead(lightPIN)) {
    lightState = "On";
  } else {
    lightState = "Off";
  }
  long pumpTme = pumpInterval - (pumpUpdate/1000);
  int pumpHr = pumpTme/3600;
  int pumpMin = (pumpTme-pumpHr*3600)/60;                                              //Remove the number of hours and calculate the minutes.
  int pumpSec = pumpTme-pumpHr*3600-pumpMin*60;                                            //Remove the number of hours and minutes, leaving only seconds.
  String pumpHrMinSec = (String(pumpHr) + ":" + String(pumpMin) + ":" + String(pumpSec));  //Converts to HH:MM:SS string. This can be returned to the calling function.

  long lightTme = lightInterval - (lightUpdate/1000);
  int lightHr = lightTme/3600;                                                        //Number of seconds in an hour
  int lightMin = (lightTme-lightHr*3600)/60;                                              //Remove the number of hours and calculate the minutes.
  int lightSec = lightTme-lightHr*3600-lightMin*60;                                            //Remove the number of hours and minutes, leaving only seconds.
  String lightHrMinSec = (String(lightHr) + ":" + String(lightMin) + ":" + String(lightSec));  //Converts to HH:MM:SS string. This can be returned to the calling function.
  
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(timeString);
  display.println();
  display.print("H2O: "); display.print(pumpState); display.print(" - "); display.println(pumpHrMinSec);
  display.println();
  display.print("LED: "); display.print(lightState); display.print(" - "); display.println(lightHrMinSec);
  display.println();
  display.print("IP: "); display.println(WiFi.localIP());  
  display.display();
}

void updateDevice() {
  if (digitalRead(pumpPin)) {           // pump is on
    if (pumpUpdate >= (pumpInterval * 1000)) {   // turn it off
      digitalWrite(pumpPin, 0);
      pumpUpdate = 0;
      pumpInterval = readFile(SPIFFS, "/pumpOff.txt").toInt();
    }
  } else {                              // pump is off
    if (pumpUpdate >= (pumpInterval * 1000)) {   // turn it on
      digitalWrite(pumpPin, 1);
      pumpUpdate = 0;
      pumpInterval = readFile(SPIFFS, "/pumpOn.txt").toInt();
    }
  }
  if (digitalRead(lightPIN)) {          // light is on
    if (lightUpdate >= (lightInterval * 1000)) {
      digitalWrite(lightPIN, 0);          // turn it off
      lightUpdate = 0;
      lightInterval = readFile(SPIFFS, "/lightOff.txt").toInt();
    }
  } else {                              // light is off
    if (lightUpdate >= (lightInterval * 1000)) {
      digitalWrite(lightPIN, 1);          // turn it on
      lightUpdate = 0;
      lightInterval = readFile(SPIFFS, "/lightOn.txt").toInt();
    }
  }
//  digitalWrite(pumpPin, !digitalRead(pumpPin));
//  digitalWrite(lightPIN, !digitalRead(lightPIN));
}

void updateFile() {

}

void loop() {
  ArduinoOTA.handle();

  if (firstRun == 1) {
    updateNTP();
    firstRun = 0;
    digitalWrite(pumpPin, LOW);
    digitalWrite(lightPIN, LOW);
  }
  
  if (ntpUpdate >= ntpInterval) {
    updateNTP();
    ntpUpdate = 0;
  }

  if (displayUpdate >= displayInterval) {
    updateDisplay();
    displayUpdate = 0;
  }

  if (deviceUpdate >= deviceInterval) {
    updateDevice();
    deviceUpdate = 0;
  }

  if (fileUpdate >= fileInterval) {
    // To access your stored values on pumpOn, pumpOff, lightOn
    int yourpumpOn = readFile(SPIFFS, "/pumpOn.txt").toInt();
    Serial.print("*** Your pumpOn: ");
    Serial.println(yourpumpOn);
    
    int yourpumpOff = readFile(SPIFFS, "/pumpOff.txt").toInt();
    Serial.print("*** Your pumpOff: ");
    Serial.println(yourpumpOff);
    
    int yourlightOn = readFile(SPIFFS, "/lightOn.txt").toInt();
    Serial.print("*** Your lightOn: ");
    Serial.println(yourlightOn);

    int yourlightOff = readFile(SPIFFS, "/lightOff.txt").toInt();
    Serial.print("*** Your lightOff: ");
    Serial.println(yourlightOff);
    fileUpdate = 0;
  }
}
