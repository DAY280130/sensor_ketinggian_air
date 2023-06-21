#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <NewPing.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#define GREEN_LED_PIN D0
#define YELLOW_LED_PIN D1
#define RED_LED_PIN D2
#define LCD_SCL_PIN D3
#define LCD_SDA_PIN D4
#define SONAR_TRIGGER_PIN D6
#define SONAR_ECHO_PIN D5
#define SONAR_MAX_DISTANCE 300 // Max Distance in cm

// LCD Initialization
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Distance Sensor Initialization
NewPing sonar(SONAR_TRIGGER_PIN, SONAR_ECHO_PIN, SONAR_MAX_DISTANCE);

// Sonar State
bool sonarInit = false; // true for initial medium depth checking
bool sonarActive = false; // true for active water level checking

// Water Level Breakpoint
float level1Breakpoint = 50.0;
float level2Breakpoint = 75.0;
float level3Breakpoint = 90.0;

// Medium Depth
int mediumDepth;

// Current Distance to Water Surface
int distance;

// Check Current Water Level
float getWaterLevel() {
  if (!mediumDepth || !distance) return 0.0;
  return ((mediumDepth - distance) / (float)mediumDepth) * 100;
}

// Network Credentials
const char* SSID     = "Kontrakan Deka";
const char* PASSWORD = "rahasia";

// Initiate Asynchronous Webserver on port 80
AsyncWebServer server(80);

// Json Buffers for Request and Response Body
DynamicJsonBuffer jsonRequestBuffer(JSON_OBJECT_SIZE(5));
DynamicJsonBuffer jsonResponseBuffer(JSON_OBJECT_SIZE(5));

// Dummy Methods for Fulfilling Method Overload Requirement
void dummyOnRequestHandler(AsyncWebServerRequest *request) {}
void dummyOnFileUploadHandler(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {}

// Class For Multi Sub-Routes Handling
// Usage : server.addRewrite( new OneParamRewrite("/radio/{frequence}", "/radio?f={frequence}") );
class OneParamRewrite : public AsyncWebRewrite {

  protected:
    String _urlPrefix;
    int _paramIndex;
    String _paramsBackup;

  public:
  OneParamRewrite(const char* from, const char* to)
    : AsyncWebRewrite(from, to) {

      _paramIndex = _from.indexOf('{');

      if( _paramIndex >=0 && _from.endsWith("}")) {
        _urlPrefix = _from.substring(0, _paramIndex);
        int index = _params.indexOf('{');
        if(index >= 0) {
          _params = _params.substring(0, index);
        }
      } else {
        _urlPrefix = _from;
      }
      _paramsBackup = _params;
  }

  bool match(AsyncWebServerRequest *request) override {
    if(request->url().startsWith(_urlPrefix)) {
      if(_paramIndex >= 0) {
        _params = _paramsBackup + request->url().substring(_paramIndex);
      } else {
        _params = _paramsBackup;
      }
    return true;

    } else {
      return false;
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(10);

  // LED pin initialization
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);

  // LCD setup
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  lcd.init();
  lcd.backlight();

  // Connect to WiFi
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);
  lcd.setCursor(0, 0);
  lcd.print("Connecting to :");
  lcd.setCursor(0, 1);
  lcd.print(SSID);

  // Setting up WiFi 
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
    
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  // Displaying Connected Status
  Serial.println("");
  Serial.println("WiFi connected");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connected!");
  delay(500);

  // Showing IP Address
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IP:");
  lcd.setCursor(3, 0);
  lcd.print(WiFi.localIP());

  // Base route
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonObject &resBody = jsonResponseBuffer.createObject();

    resBody["message"] = "All Available Datas";
    if(!mediumDepth) {
      resBody["depthStatus"] = "unchecked";
      resBody["depth"] = 0;
    } else {
      resBody["depthStatus"] = "checked";
      resBody["depth"] = mediumDepth;
    }
    resBody["level1Breakpoint"] = level1Breakpoint;
    resBody["level2Breakpoint"] = level2Breakpoint;
    resBody["level3Breakpoint"] = level3Breakpoint;
    resBody["sonarStatus"] = sonarActive ? "active" : "inactive";
    if(!sonarActive) {
      resBody["waterStatus"] = "unchecked";
      resBody["waterLevel"] = 0;
    } else {
      if(getWaterLevel() >= level3Breakpoint) resBody["waterStatus"] = "dangerous";
      else if(getWaterLevel() >= level2Breakpoint) resBody["waterStatus"] = "be warned";
      else if(getWaterLevel() >= level1Breakpoint) resBody["waterStatus"] = "safe";
      else resBody["waterStatus"] = "low";
      resBody["waterLevel"] = getWaterLevel();
    }

    resBody.printTo(*response);
    response->setCode(200);
    request->send(response);
  });

  // Depth Control Routes
  server.on("/depth", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonObject &resBody = jsonResponseBuffer.createObject();

    if(!mediumDepth) {
      resBody["depthStatus"] = "unchecked";
      resBody["depth"] = 0;
    } else {
      resBody["depthStatus"] = "checked";
      resBody["depth"] = mediumDepth;
    }

    resBody.printTo(*response);
    response->setCode(200);
    request->send(response);
  });

  server.on("/depth/check", HTTP_POST, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonObject &resBody = jsonResponseBuffer.createObject();
    
    sonarInit = true;
    resBody["message"] = "successfully set sonarInit to true, checking depth on next loop cycle";

    resBody.printTo(*response);
    response->setCode(200);
    request->send(response);
  });

  // Level Control Routes
  server.addRewrite(new OneParamRewrite("/level/{specified_level}", "/level?l={specified_level}"));
  
  server.on("/level", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonObject &resBody = jsonResponseBuffer.createObject();
    response->setCode(400);

    if(!request->hasParam("l")) {
      resBody["level1Breakpoint"] = level1Breakpoint;
      resBody["level2Breakpoint"] = level2Breakpoint;
      resBody["level3Breakpoint"] = level3Breakpoint;
      resBody.printTo(*response);
      response->setCode(200);
      request->send(response);
      return;
    }

    int specifiedLevel = request->getParam("l")->value().toInt();
    switch (specifiedLevel) {
    case 1:
      resBody["level1Breakpoint"] = level1Breakpoint;
      break;
    case 2:
      resBody["level2Breakpoint"] = level2Breakpoint;
      break;
    case 3:
      resBody["level3Breakpoint"] = level3Breakpoint;
      break;
    default:
      resBody["message"] = "level must be integer from 1 to 3";
      resBody.printTo(*response);
      request->send(response);
      return;
      break;
    }

    resBody.printTo(*response);
    response->setCode(200);
    request->send(response);
  });

  server.on(
    "/level",
    HTTP_POST,
    dummyOnRequestHandler,
    dummyOnFileUploadHandler,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      AsyncResponseStream *response = request->beginResponseStream("application/json");
      JsonObject &reqBody = jsonRequestBuffer.parseObject((char*)data);
      JsonObject &resBody = jsonResponseBuffer.createObject();
      response->setCode(400);

      // Checks Level Parameter
      if(!request->hasParam("l")) {
        resBody["message"] = "please specifiy level";
        resBody.printTo(*response);
        request->send(response);
        return;
      }
      
      // Checks Request Body
      if(!reqBody.success() || !reqBody["value"]) {
        resBody["message"] = "failed parsing json, invalid json format";
        resBody.printTo(*response);
        request->send(response);
        return;
      }

      // Checks Value
      float value = atof(reqBody["value"]);
      if(value < 1.0 || value > 100.0) {
        resBody["message"] = "value must be a valid float from 1 to 100";
        resBody.printTo(*response);
        request->send(response);
        return;
      }

      // Compares Value and Changes Specified Level's Breakpoint Value
      int specifiedLevel = request->getParam("l")->value().toInt();
      switch (specifiedLevel) {
      case 1:
        if(value >= level2Breakpoint) {
          resBody["message"] = "value of level 1 breakpoint must be lower than level 2";
        } else {
          level1Breakpoint = value;
          resBody["message"] = "success changing level 1 value";
          response->setCode(200);
        }
        break;

      case 2:
        if(value <= level1Breakpoint || value >= level3Breakpoint) {
          resBody["message"] = "value of level 1 breakpoint must be between level 1 and 3";
        } else {
          level2Breakpoint = value;
          resBody["message"] = "success changing level 2 value";
          response->setCode(200);
        }
        break;

      case 3:
        if(value <= level2Breakpoint) {
          resBody["message"] = "value of level 1 breakpoint must be higher than level 2";
        } else {
          level3Breakpoint = value;
          resBody["message"] = "success changing level 3 value";
          response->setCode(200);
        }
        break;
      
      default:
        resBody["message"] = "level must be integer from 1 to 3";
        return;
        break;
      }
      
      resBody.printTo(*response);
      request->send(response);
    }
  );

  // Sonar Control Route
  server.addRewrite(new OneParamRewrite("/sonar/{command}", "/sonar?c={command}"));

  server.on("/sonar", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonObject &resBody = jsonResponseBuffer.createObject();

    resBody["sonarStatus"] = sonarActive ? "active" : "inactive";

    resBody.printTo(*response);
    response->setCode(200);
    request->send(response);
  });

  server.on("/sonar", HTTP_POST, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonObject &resBody = jsonResponseBuffer.createObject();
    response->setCode(400);

    if(!request->hasParam("c")) {
      resBody["message"] = "must specify command parameter";
      request->send(response);
      return;
    }

    const String command = request->getParam("c")->value();
    if(command == "on") {
      sonarActive = true;
      resBody["message"] = "sonar activated";
      response->setCode(200);
    } else if (command == "off") {
      sonarActive = false;
      resBody["message"] = "sonar deactivated";
      response->setCode(200);
    } else {
      resBody["message"] = "command parameter can only be 'on' or 'off'";
    }

    resBody.printTo(*response);
    request->send(response);
  });

  // Water Status Route
  server.on("/water", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonObject &resBody = jsonResponseBuffer.createObject();

    if(!sonarActive) {
      resBody["waterStatus"] = "unchecked";
      resBody["waterLevel"] = 0;
    } else {
      if(getWaterLevel() >= level3Breakpoint) resBody["waterStatus"] = "dangerous";
      else if(getWaterLevel() >= level2Breakpoint) resBody["waterStatus"] = "be warned";
      else if(getWaterLevel() >= level1Breakpoint) resBody["waterStatus"] = "safe";
      else resBody["waterStatus"] = "low";
      resBody["waterLevel"] = getWaterLevel();
      resBody["depth"] = mediumDepth;
      resBody["distance"] = distance;
    }

    resBody.printTo(*response);
    response->setCode(200);
    request->send(response);
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("application/json");
    JsonObject &resBody = jsonResponseBuffer.createObject();

    resBody["message"] = "there is no such endpoint";

    resBody.printTo(*response);
    response->setCode(404);
    request->send(response);
  });

  server.begin();
  Serial.println("Webserver Running...");
}

float currentWaterLevel; // reusable in loop function for memory efficiency

void loop() {
  lcd.setCursor(0, 1);
  if (sonarActive) {
    delay(50);
    distance = sonar.ping_cm();
    currentWaterLevel = getWaterLevel(); // for resource efficiency, only checks and assign once
    lcd.printf("%2.2f%%", currentWaterLevel);
    digitalWrite(
      GREEN_LED_PIN,
      currentWaterLevel >= level1Breakpoint ? HIGH : LOW
    );
    digitalWrite(
      YELLOW_LED_PIN,
      currentWaterLevel >= level2Breakpoint ? HIGH : LOW
    );
    digitalWrite(
      RED_LED_PIN,
      currentWaterLevel >= level3Breakpoint ? HIGH : LOW
    );
  } else {
    lcd.print("------");
    // turn off led once sonar stop monitoring
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
  }
  lcd.setCursor(6, 1);
  lcd.print(" of ");
  lcd.setCursor(10, 1);
  // lcd.print("      ");
  if (sonarInit) {
    delay(50);
    mediumDepth = sonar.ping_cm();
    sonarInit = false; // prevent sonar for keep checking depth
  }
  mediumDepth ? lcd.printf("%3i CM", mediumDepth) : lcd.print("------");
}