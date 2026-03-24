#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <SDI12.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

char wifi_ssid[32] = "hygrow";     // Default SSID
char wifi_password[64] = ""; // Default Password
char mqtt_server[16] = "192.168.10.1";     // Default MQTT IP

WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80); // Web server on port 80

#define SDI12_DATA_PIN D1
SDI12 mySDI12(SDI12_DATA_PIN);
String sensorAddress = "0";

unsigned long previousMillis = 0;
const long interval = 5000;

#define EEPROM_SIZE 112 // 16 (IP) + 32 (SSID) + 64 (Password)
#define EEPROM_MQTT_ADDR 0    // 0–15
#define EEPROM_SSID_ADDR 16   // 16–47
#define EEPROM_PASS_ADDR 48   // 48–111

#define AP_SSID "Teros12-AP"
#define AP_PASSWORD "teros1234"
#define WIFI_TIMEOUT 20000 // 20s timeout

bool apMode = false;

void setup_wifi();
void startAPMode();
void handleRoot();
void handleConfig();
void setUpOverTheAirProgramming();
void reconnect();
void loadConfigFromEEPROM();
void saveConfigToEEPROM(const char* ssid, const char* pass, const char* ip);

void setup() {
  Serial.begin(115200);

  EEPROM.begin(EEPROM_SIZE);
  loadConfigFromEEPROM();

  setup_wifi();

  if (!apMode) {
    setUpOverTheAirProgramming();
    client.setServer(mqtt_server, 1883);

    mySDI12.begin();
    delay(3000);

    Serial.println("Sending ?!");
    mySDI12.clearBuffer();
    mySDI12.sendCommand("?!");
    delay(300);
    String response = "";
    while (mySDI12.available()) {
      response += (char)mySDI12.read();
    }
    Serial.print("Address: ");
    Serial.println(response.length() > 0 ? response : "No response");

    Serial.println("Sending 0I!");
    mySDI12.clearBuffer();
    mySDI12.sendCommand("0I!");
    delay(300);
    response = "";
    while (mySDI12.available()) {
      response += (char)mySDI12.read();
    }
    Serial.print("ID: ");
    Serial.println(response.length() > 0 ? response : "No response");
  } else {
    server.on("/", handleRoot);        // Main page
    server.on("/config", handleConfig); // Form submission
    server.begin();
    Serial.println("Web server started");
  }

  Serial.println("Current Configuration:");
  Serial.println("WiFi SSID: " + String(wifi_ssid));
  Serial.println("WiFi Password: " + String(wifi_password));
  Serial.println("MQTT Server IP: " + String(mqtt_server));
  if (apMode) {
    Serial.println("Running in AP mode. Connect to " AP_SSID " with password " AP_PASSWORD);
    Serial.println("Visit http://" + WiFi.softAPIP().toString() + " to configure");
  }
  Serial.println("To update via Serial, type: CONFIG=ssid,password,ip");
}

void loop() {
  if (apMode) {
    server.handleClient(); // Handle web requests

    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      if (input.startsWith("CONFIG=")) {
        String config = input.substring(7);
        int comma1 = config.indexOf(',');
        int comma2 = config.indexOf(',', comma1 + 1);

        if (comma1 != -1 && comma2 != -1) {
          String newSSID = config.substring(0, comma1);
          String newPass = config.substring(comma1 + 1, comma2);
          String newIP = config.substring(comma2 + 1);

          if (newSSID.length() > 0 && newSSID.length() < 32 &&
              newPass.length() > 0 && newPass.length() < 64 &&
              newIP.length() > 0 && newIP.length() < 16) {
            newSSID.toCharArray(wifi_ssid, 32);
            newPass.toCharArray(wifi_password, 64);
            newIP.toCharArray(mqtt_server, 16);

            saveConfigToEEPROM(wifi_ssid, wifi_password, mqtt_server);
            Serial.println("Configuration updated via Serial. Rebooting...");
            delay(1000);
            ESP.restart();
          } else {
            Serial.println("Error: Invalid length (SSID < 32, Pass < 64, IP < 16)");
          }
        } else {
          Serial.println("Error: Use format CONFIG=ssid,password,ip");
        }
      }
    }
  } else {
    ArduinoOTA.handle();
    yield();
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
    yield();

    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      if (input.startsWith("CONFIG=")) {
        String config = input.substring(7);
        int comma1 = config.indexOf(',');
        int comma2 = config.indexOf(',', comma1 + 1);

        if (comma1 != -1 && comma2 != -1) {
          String newSSID = config.substring(0, comma1);
          String newPass = config.substring(comma1 + 1, comma2);
          String newIP = config.substring(comma2 + 1);

          if (newSSID.length() > 0 && newSSID.length() < 32 &&
              newPass.length() > 0 && newPass.length() < 64 &&
              newIP.length() > 0 && newIP.length() < 16) {
            newSSID.toCharArray(wifi_ssid, 32);
            newPass.toCharArray(wifi_password, 64);
            newIP.toCharArray(mqtt_server, 16);

            saveConfigToEEPROM(wifi_ssid, wifi_password, mqtt_server);
            WiFi.begin(wifi_ssid, wifi_password);
            client.setServer(mqtt_server, 1883);
            Serial.println("Configuration updated:");
            Serial.println("WiFi SSID: " + String(wifi_ssid));
            Serial.println("WiFi Password: " + String(wifi_password));
            Serial.println("MQTT Server IP: " + String(mqtt_server));
          } else {
            Serial.println("Error: Invalid length (SSID < 32, Pass < 64, IP < 16)");
          }
        } else {
          Serial.println("Error: Use format CONFIG=ssid,password,ip");
        }
      }
    }

    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis;

      Serial.println("Sending 0M!");
      mySDI12.clearBuffer();
      mySDI12.sendCommand("0M!");
      delay(100);
      String mResponse = "";
      mResponse.reserve(16);
      while (mySDI12.available()) {
        char c = mySDI12.read();
        mResponse += c;
        Serial.print(c);
      }
      Serial.print("\n0M! response: ");
      Serial.println(mResponse);

      int waitTime = 1000;
      if (mResponse.length() >= 4) {
        waitTime = mResponse.substring(1, 4).toInt() * 1000;
      }
      Serial.print("Waiting ");
      Serial.print(waitTime);
      Serial.println(" ms");
      delay(waitTime);

      Serial.println("Sending 0D0!");
      mySDI12.clearBuffer();
      mySDI12.sendCommand("0D0!");
      delay(300);
      String sensorData = "";
      sensorData.reserve(32);
      while (mySDI12.available()) {
        char c = mySDI12.read();
        sensorData += c;
        Serial.print(c);
      }
      Serial.print("\nRaw sensor data: ");
      Serial.println(sensorData);

      sensorData.trim();
      if (sensorData.length() > 0 && sensorData.startsWith(sensorAddress) && sensorData.indexOf('+') != -1) {
        int plusIndex1 = sensorData.indexOf('+');
        int plusIndex2 = sensorData.indexOf('+', plusIndex1 + 1);
        int plusIndex3 = sensorData.indexOf('+', plusIndex2 + 1);

        String vwc = (plusIndex1 != -1 && plusIndex2 != -1) ? sensorData.substring(plusIndex1 + 1, plusIndex2) : "N/A";
        String temperature = (plusIndex2 != -1 && plusIndex3 != -1) ? sensorData.substring(plusIndex2 + 1, plusIndex3) : "N/A";
        String ec = (plusIndex3 != -1) ? sensorData.substring(plusIndex3 + 1) : "N/A";

        Serial.print("VWC: "); Serial.println(vwc);
        Serial.print("Temperature: "); Serial.println(temperature);
        Serial.print("EC: "); Serial.println(ec);

        if (vwc != "N/A") client.publish("sensor/vwc", vwc.c_str());
        if (temperature != "N/A") client.publish("sensor/temperature", temperature.c_str());
        if (ec != "N/A") client.publish("sensor/ec", ec.c_str());
      } else {
        Serial.println("Error: Unable to parse sensor data");
      }
    }
  }
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < WIFI_TIMEOUT) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    apMode = false;
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed. Starting AP mode...");
    startAPMode();
  }
}

void startAPMode() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("AP started");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  apMode = true;
}

void handleRoot() {
  String html = "<html><body><h1>Teros 12 Config</h1>";
  html += "<form action='/config' method='POST'>";
  html += "WiFi SSID: <input type='text' name='ssid' value='" + String(wifi_ssid) + "'><br>";
  html += "WiFi Password: <input type='text' name='password' value='" + String(wifi_password) + "'><br>";
  html += "MQTT Server IP: <input type='text' name='ip' value='" + String(mqtt_server) + "'><br>";
  html += "<input type='submit' value='Save'>";
  html += "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleConfig() {
  String newSSID = server.arg("ssid");
  String newPass = server.arg("password");
  String newIP = server.arg("ip");

  if (newSSID.length() > 0 && newSSID.length() < 32 &&
      
      newIP.length() > 0 && newIP.length() < 16) {
    newSSID.toCharArray(wifi_ssid, 32);
    newPass.toCharArray(wifi_password, 64);
    newIP.toCharArray(mqtt_server, 16);

    saveConfigToEEPROM(wifi_ssid, wifi_password, mqtt_server);
    server.send(200, "text/html", "<html><body><h1>Config Saved</h1><p>Rebooting...</p></body></html>");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "<html><body><h1>Error</h1><p>Invalid input lengths (SSID < 32, Pass < 64, IP < 16)</p></body></html>");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("NodeMCUClient")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void loadConfigFromEEPROM() {
  char storedSSID[32];
  char storedPass[64];
  char storedIP[16];

  for (int i = 0; i < 32; i++) {
    storedSSID[i] = EEPROM.read(EEPROM_SSID_ADDR + i);
  }
  for (int i = 0; i < 64; i++) {
    storedPass[i] = EEPROM.read(EEPROM_PASS_ADDR + i);
  }
  for (int i = 0; i < 16; i++) {
    storedIP[i] = EEPROM.read(EEPROM_MQTT_ADDR + i);
  }

  if (storedSSID[0] != 0xFF && storedSSID[0] != 0) {
    strncpy(wifi_ssid, storedSSID, 32);
    wifi_ssid[31] = '\0';
  }
  if (storedPass[0] != 0xFF && storedPass[0] != 0) {
    strncpy(wifi_password, storedPass, 64);
    wifi_password[63] = '\0';
  }
  if (storedIP[0] != 0xFF && storedIP[0] != 0) {
    strncpy(mqtt_server, storedIP, 16);
    mqtt_server[15] = '\0';
  }
}

void saveConfigToEEPROM(const char* ssid, const char* pass, const char* ip) {
  for (int i = 0; i < 16; i++) {
    EEPROM.write(EEPROM_MQTT_ADDR + i, i < strlen(ip) ? ip[i] : 0);
  }
  for (int i = 0; i < 32; i++) {
    EEPROM.write(EEPROM_SSID_ADDR + i, i < strlen(ssid) ? ssid[i] : 0);
  }
  for (int i = 0; i < 64; i++) {
    EEPROM.write(EEPROM_PASS_ADDR + i, i < strlen(pass) ? pass[i] : 0);
  }
  EEPROM.commit();
}

void setUpOverTheAirProgramming() {
  ArduinoOTA.setHostname("TERROS12");
  ArduinoOTA.onStart([]() {
    Serial.println("Start updating sketch");
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
  Serial.println("OTA Initialized");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}