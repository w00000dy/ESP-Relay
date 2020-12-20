#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <FS.h>
#include <SPIFFS.h>
#include <index.h>

#define TOGGLE_TIME 250
#define MAX_RELAYS 32

DynamicJsonDocument relays(2048);
AsyncWebServer server(80);
DNSServer dns;

unsigned long toggleRelay[MAX_RELAYS];
boolean switchRelay[MAX_RELAYS];

void notFound(AsyncWebServerRequest *request) { request->send(404, "text/plain", "Not found"); }

void setup() {
    Serial.begin(9600);
    SPIFFS.begin();

    File f = SPIFFS.open("/relays.json", "r");
    if (!f) {
        Serial.println("file open failed");
        File file = SPIFFS.open("/relays.json", "w");
        serializeJson(relays, file);
        file.close();
    }

    String data = f.readString();
    Serial.println("Content of file:");
    Serial.println(data);
    f.close();
    deserializeJson(relays, data);
    Serial.println("Space: " + String(relays.memoryUsage()) + "/" + String(relays.capacity()));

    // check if file is corrupted
    for (size_t i = 0; i < relays.size(); i++) {
        if (relays[i]["pin"].is<int>() == false || relays[i]["name"].is<char *>() == false) {
            Serial.println("Error! File is corrupted. Deleting file.");
            SPIFFS.remove("/relays.json");
            Serial.println("Restarting...");
            ESP.restart();
        }
    }

    initializePins();

    AsyncWiFiManager wifiManager(&server, &dns);
    wifiManager.autoConnect("ESP-Relay");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", PAGE_index);
        request->send(response);
    });

    server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("toggle Relay");
        if (request->hasParam("r", false)) {
            uint8_t response = request->getParam("r", false)->value().toInt();
            Serial.println("Toggle Relay " + String(response));
            toggleRelay[response] = millis() + TOGGLE_TIME;
        }
        request->send(200, "text/plain", "okay");
    });

    server.on("/switch", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("switch Relay");
        if (request->hasParam("r", false) && request->hasParam("on", false)) {
            uint8_t response = request->getParam("r", false)->value().toInt();
            String on = request->getParam("on", false)->value();
            Serial.println("Switch Relay " + String(response) + " to " + on);
            switchRelay[response] = (on == "true");
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
        StaticJsonDocument<350> info;
        info["esp"]["flashChipSize"] = ESP.getFlashChipSize();
        info["esp"]["flashFreq"] = ESP.getFlashChipSpeed();
        // info["esp"]["ChipId"] = ESP.getChipId();
        info["esp"]["FreeHeap"] = ESP.getFreeHeap();
        // info["esp"]["Vcc"] = ESP.getVcc();
        // info["esp"]["MaxFreeBlockSize"] = ESP.getMaxFreeBlockSize();
        info["esp"]["SketchSize"] = ESP.getSketchSize();
        info["esp"]["SketchMD5"] = ESP.getSketchMD5();
        info["relays"]["Capacity"] = relays.capacity();
        info["relays"]["Memory Usage"] = relays.memoryUsage();
        info["relays"]["currentRelays"] = relays.size();
        info["relays"]["maxRelays"] = MAX_RELAYS;
        info["info"]["Capacity"] = info.capacity();
        info["info"]["Memory Usage"] = info.memoryUsage();
        String json;
        serializeJson(info, json);
        request->send(200, "application/json", json);
    });

    server.onNotFound(notFound);

    server.begin();
}

void handleRelays() {
    for (size_t i = 0; i < relays.size(); i++) {
        if (switchRelay[i] == true) {
            if (toggleRelay[i] > millis()) {
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
            if (toggleRelay[i] > millis()) {
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
    File file = SPIFFS.open("/relays.json", "w");
    serializeJson(relays, file);
    file.close();
}

String getJson() {
    File file = SPIFFS.open("/relays.json", "r");
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