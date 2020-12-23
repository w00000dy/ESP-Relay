// filesystem
#ifdef ESP32
#define LittleFS LITTLEFS
#include <LITTLEFS.h>
#else
#include <LittleFS.h>
#endif

// libraries
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <index.h>

#define TOGGLE_TIME 250

#ifdef ESP32
DynamicJsonDocument relays(ESP.getMaxAllocHeap() / 2);
#else
DynamicJsonDocument relays(ESP.getMaxFreeBlockSize() / 2);
#endif

AsyncWebServer server(80);
DNSServer dns;

void notFound(AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); }

void setup() {
    Serial.begin(9600);

#ifdef ESP32
    LittleFS.begin(true);
    Serial.println("Board: ESP32");
#else
    LittleFS.begin();
    Serial.println("Board: ESP8266");
#endif

    // detects if system has crashed
    File crashFileRead = LittleFS.open("/crash.json", "r");
    if (!crashFileRead) {
        Serial.println("crash-file open failed");
        StaticJsonDocument<64> crash;
        crash["crash"] = false;
        crash["amount"] = 0;
        File crashFileWrite = LittleFS.open("/crash.json", "w");
        serializeJson(crash, crashFileWrite);
        crashFileWrite.close();
    }
    String json = crashFileRead.readString();
    crashFileRead.close();
    StaticJsonDocument<64> crash;
    deserializeJson(crash, json);
    Serial.println("Crash: " + json);
    if (crash["amount"] >= 3) {
        crash["crash"] = true;
        LittleFS.remove("/relays.json");
    } else {
        crash["amount"] = int(crash["amount"]) + 1;
    }
    File crashFileWrite = LittleFS.open("/crash.json", "w");
    serializeJson(crash, crashFileWrite);
    crashFileWrite.close();

    // load relays from file
    File f = LittleFS.open("/relays.json", "r");
    String data = f.readString();
    f.close();
    Serial.println("Relays: " + data);

    deserializeJson(relays, data);

    // turn every relay off at boot
    for (size_t i = 0; i < relays.size(); i++) {
        relays[i]["toggleRelay"] = 0;
        relays[i]["switchRelay"] = false;
    }

    initializePins();

    AsyncWiFiManager wifiManager(&server, &dns);
    wifiManager.autoConnect("ESP-Relay");
#ifdef ESP32
    WiFi.setSleep(false);
#endif

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", PAGE_index);
        request->send(response);
    });

    server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("r", false)) {
            uint8_t response = request->getParam("r", false)->value().toInt();
            Serial.println("Toggle Relay " + String(response));
            relays[response]["toggleRelay"] = millis() + TOGGLE_TIME;
            saveJson();
        }
        request->send(200, "text/plain", "okay");
    });

    server.on("/switch", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("r", false) && request->hasParam("on", false)) {
            uint8_t response = request->getParam("r", false)->value().toInt();
            String on = request->getParam("on", false)->value();
            Serial.println("Switch Relay " + String(response) + " to " + on);
            if (on == "true") {
                relays[response]["switchRelay"] = true;
            } else {
                relays[response]["switchRelay"] = false;
            }
        }
        request->send(200, "text/plain", "okay");
    });

    server.on("/json", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("json (GET)");
        String json;
        serializeJson(relays, json);
        request->send(200, "application/json", json);
    });

    server.on("/json", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("json (POST)");
        if (request->hasParam("json", true)) {
            String response = request->getParam("json", true)->value();
            Serial.println(response);
            deserializeJson(relays, response);
            saveJson();
            initializePins();
            request->send(200, "text/plain", getJson());
        }
    });

    server.on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("info");
        File crashFileRead = LittleFS.open("/crash.json", "r");
        String json = crashFileRead.readString();
        crashFileRead.close();
        StaticJsonDocument<64> crash;
        deserializeJson(crash, json);
        StaticJsonDocument<350> info;
        info["esp"]["flashChipSize"] = ESP.getFlashChipSize();
        info["esp"]["flashFreq"] = ESP.getFlashChipSpeed();
        info["esp"]["CpuFreqMHz"] = ESP.getCpuFreqMHz();
        info["esp"]["SketchSize"] = ESP.getSketchSize();
        info["esp"]["SketchMD5"] = ESP.getSketchMD5();
        info["esp"]["FreeHeap"] = ESP.getFreeHeap();
#ifdef ESP32
        info["esp"]["MaxAllocHeap"] = ESP.getMaxAllocHeap();
#else
        info["esp"]["MaxFreeBlockSize"] = ESP.getMaxFreeBlockSize();
#endif
        info["esp"]["uptime"] = millis();
        info["relays"]["Capacity"] = relays.capacity();
        info["relays"]["Memory Usage"] = relays.memoryUsage();
        info["relays"]["currentRelays"] = relays.size();
        info["crash"] = crash;
        info["info"]["Capacity"] = info.capacity();
        info["info"]["Memory Usage"] = info.memoryUsage();
        json = "";
        serializeJson(info, json);
        request->send(200, "application/json", json);
    });

    server.on("/crash", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("accept crash warning");
        File crashFileRead = LittleFS.open("/crash.json", "r");
        String json = crashFileRead.readString();
        crashFileRead.close();
        StaticJsonDocument<64> crash;
        deserializeJson(crash, json);
        crash["crash"] = false;
        File crashFileWrite = LittleFS.open("/crash.json", "w");
        serializeJson(crash, crashFileWrite);
        crashFileWrite.close();
        serializeJson(crash, json);
        request->send(200, "application/json", json);
    });

    server.on("/format", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("format");
        LittleFS.remove("/relays.json");
        LittleFS.remove("/crash.json");
        request->send(200, "text/plain", "device formatted");
    });

    server.onNotFound(notFound);

    server.begin();

    // reset crash counter
    File crashFile = LittleFS.open("/crash.json", "w");
    crash["amount"] = 0;
    serializeJson(crash, crashFile);
    crashFile.close();
}

void handleRelays() {
    for (size_t i = 0; i < relays.size(); i++) {
        if (relays[i]["switchRelay"] == true) {
            if (relays[i]["toggleRelay"] > millis()) {
                if (relays[i]["Low Level Trigger"]) {
                    digitalWrite(relays[i]["pin"], HIGH);
                } else {
                    digitalWrite(relays[i]["pin"], LOW);
                }
            } else {
                if (relays[i]["Low Level Trigger"]) {
                    digitalWrite(relays[i]["pin"], LOW);
                } else {
                    digitalWrite(relays[i]["pin"], HIGH);
                }
            }
        } else {
            if (relays[i]["toggleRelay"] > millis()) {
                if (relays[i]["Low Level Trigger"]) {
                    digitalWrite(relays[i]["pin"], LOW);
                } else {
                    digitalWrite(relays[i]["pin"], HIGH);
                }
            } else {
                if (relays[i]["Low Level Trigger"]) {
                    digitalWrite(relays[i]["pin"], HIGH);
                } else {
                    digitalWrite(relays[i]["pin"], LOW);
                }
            }
        }
    }
}

void saveJson() {
    File file = LittleFS.open("/relays.json", "w");
    serializeJson(relays, file);
    file.close();
}

String getJson() {
    File file = LittleFS.open("/relays.json", "r");
    String json = file.readString();
    file.close();
    return json;
}

void initializePins() {
    for (size_t i = 0; i < relays.size(); i++) {
        pinMode(relays[i]["pin"], OUTPUT);
        if (relays[i]["Low Level Trigger"]) {
            digitalWrite(relays[i]["pin"], HIGH);
        } else {
            digitalWrite(relays[i]["pin"], LOW);
        }
    }
}

void loop() {
    handleRelays();
}