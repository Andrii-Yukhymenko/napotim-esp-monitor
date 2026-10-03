#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <private_wifi.h>

// Private build used only for the first installation. The saved SDK WiFi
// credentials survive the next OTA; the main firmware has no compiled secrets.
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

void setup() {
  Serial.begin(115200);
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(PRIVATE_WIFI_SSID, PRIVATE_WIFI_PASSWORD);
  updater.setup(&server, "/update", "admin", PRIVATE_LOADER_PASSWORD);
  server.on("/", []() { server.send(200, "text/plain", "Napotim first-install loader. Upload main firmware at /update."); });
  server.on("/health", []() { server.send(200, "text/plain", "napotim-loader-v1"); });
  server.begin();
}

void loop() {
  server.handleClient();
  yield();
}
