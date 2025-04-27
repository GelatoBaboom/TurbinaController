#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

// WiFi credentials
const char * ssid = "cotemax_48d";
const char * password = "cote1da7";

// Static IP configuration
IPAddress staticIP(192, 168, 1, 230);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8); // Google's DNS
IPAddress secondaryDNS(8, 8, 4, 4); // Backup DNS

// NTP setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000); // Argentina UTC-3 with timeout

// Web server
ESP8266WebServer server(80);

// Relay pin
# define RELAY_PIN D4
bool manualOverride = false;
bool relayState = false;
unsigned long relayStartTime = 0;
unsigned long relayStateMil = 0;
unsigned long RELAY_DURATION = 2 * 60 * 1000; // 2 minutes in milliseconds
unsigned long RELAY_WAIT = 10 * 60 * 1000; // 10 minutes in milliseconds

unsigned long lastNtpUpdate = 0;
const unsigned long NTP_UPDATE_INTERVAL = 60 * 60 * 1000; // cada 10 minutos

String getHTTPTime() {
  WiFiClient client;
  HTTPClient http;
  http.begin(client, "http://worldtimeapi.org/api/timezone/America/Argentina/Buenos_Aires");
  int httpCode = http.GET();
  String payload = http.getString();
  http.end();

  if (httpCode == 200) {
    int pos = payload.indexOf("\"datetime\":\"") + 11;
    return payload.substring(pos, pos + 19); // Extracts ISO 8601 datetime
  }
  return "Error fetching time";
}
void handleRoot() {
  String html = "<!DOCTYPE html>"
                "<html lang='en'>"
                "<head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>Recirculador de aire</title>"
                "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css' rel='stylesheet'>"
                "<style>"
                "#overlay {"
                "  position: fixed;"
                "  top: 0;"
                "  left: 0;"
                "  width: 100%;"
                "  height: 100%;"
                "  background: rgba(0, 0, 0, 0.5);"
                "  display: none;"
                "  align-items: center;"
                "  justify-content: center;"
                "  z-index: 1000;"
                "}"
                "#spinner {"
                "  width: 4rem;"
                "  height: 4rem;"
                "}"
                "</style>"
                "</head>"
                "<body>"
                "<div class='container mt-5 text-center'>"
                "<h1 class='mb-3'>Recirculador de aire</h1>"
                "<div class='card shadow-lg p-4'>"
                "<h4 class='mb-3'>Hora actual: <span id='timeDisplay'>" + timeClient.getFormattedTime() + "</span></h4>"
                "<h4 class='mb-3'>Estado de la turbina: <span id='statusDisplay' class='fw-bold'>" + (relayState ? "ON" : "OFF") + "</span></h4>"

                // SPINNER ADDED HERE
                "<div id='overlay'>"
                "  <div class='spinner-border text-light' id='spinner' role='status'>"
                "    <span class='visually-hidden'>Loading...</span>"
                "  </div>"
                "</div>"

                "<div class='d-flex justify-content-center gap-3'>"
                "<button id='enableBtn' class='btn btn-success btn-lg " + (relayState ? "disabled" : "") + "' onclick='toggleRelay(true)'>Prender</button>"
                "<button id='disableBtn' class='btn btn-danger btn-lg " + (!relayState ? "disabled" : "") + "' onclick='toggleRelay(false)'>Apagar</button>"
                "</div>"
                "<div class='row justify-content-center' style='margin-top: 10px;'>"
                "<div class='col-md-2'>"
                "<div class='form-check form-switch'>"
                "<input class='form-check-input' type='checkbox' " + (!manualOverride ? "checked='checked'" : "") + " role='switch' id='manualOverride' onchange='toggleManualOverride(this.checked)'>"
                "<label class='form-check-label' for='manualOverride'>Modo automatico</label>"
                "</div>"
                "</div>"
                "</div>"
                "<br/>"
                "<h4 style='margin-top: 30px;'>Configuracion</h4>"
                "<div class='row justify-content-center'>"
                "<div class='col-md-3'>"
                "<label>Run time (minutos):</label>"
                "<input type='number' id='relayDuration' class='form-control' value='" + String(RELAY_DURATION / 60000) + "'>"
                "<label>Tiempo de espera (minutos):</label>"
                "<input type='number' id='relayWait' class='form-control' value='" + String(RELAY_WAIT / 60000) + "'>"
                "<button class='btn btn-primary mt-2' onclick='updateConfig()'>Guardar</button>"
                "</div>"
                "</div>"
                "</div>"
                "</div>"
                "<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js'></script>"
                "<script>"
                "var updateInterval = null;"
                "function showSpinner() {"
                "  document.getElementById('overlay').style.display = 'flex';"
                "}"
                "function hideSpinner() {"
                "  document.getElementById('overlay').style.display = 'none';"
                "}"
                "function updateStatus() {"
                "  clearInterval(updateInterval);"
                "  fetch('/status').then(response => response.json()).then(data => {"
                "    document.getElementById('timeDisplay').innerText = data.time;"
                "    document.getElementById('statusDisplay').innerText = data.relay ? 'ON' : 'OFF';"
                "    document.getElementById('enableBtn').classList.toggle('disabled', data.relay);"
                "    document.getElementById('disableBtn').classList.toggle('disabled', !data.relay);"
                "    updateInterval = setInterval(updateStatus, 2000);"
                "  }).finally(hideSpinner);"
                "}"
                "function toggleRelay(state) {"
                "  clearInterval(updateInterval);"
                "  showSpinner();"
                "  fetch(state ? '/enable' : '/disable').then(() => updateStatus());"
                "}"
                "function toggleManualOverride(state) {"
                "  fetch(state ? '/manualOff' : '/manualOn');"
                "}"
                "function updateConfig() {"
                "  showSpinner();"
                "  const duration = document.getElementById('relayDuration').value * 60000;"
                "  const wait = document.getElementById('relayWait').value * 60000;"
                "  fetch('/config?duration=' + duration + '&wait=' + wait, { method: 'POST' });"
                "}"
                "updateInterval = setInterval(updateStatus, 2000);"
                "</script>"
                "</body>"
                "</html>";

  server.send(200, "text/html", html);
}

void handleEnable() {
  digitalWrite(RELAY_PIN, LOW);
  relayState = true;
  relayStateMil = millis();
  server.send(200, "text/plain", "Turbine enabled");
}

void handleDisable() {
  digitalWrite(RELAY_PIN, HIGH);
  relayState = false;
  relayStateMil = millis();
  server.send(200, "text/plain", "Turbine disabled");
}

void handleStatus() {
  String json = "{\"relay\":" + String(relayState) + ",\"time\":\"" + timeClient.getFormattedTime() + "\"}";
  server.send(200, "application/json", json);
}

void handleConfig() {
  if (server.hasArg("duration")) RELAY_DURATION = server.arg("duration").toInt();
  if (server.hasArg("wait")) RELAY_WAIT = server.arg("wait").toInt();
  server.send(200, "text/plain", "Configuration updated");
}
void handleManualOn() {
  manualOverride = true;
  digitalWrite(RELAY_PIN, HIGH);
  relayState = false;
  relayStateMil = millis();
  server.send(200, "text/plain", "Manual override enabled");
}

void handleManualOff() {
  manualOverride = false;
  digitalWrite(RELAY_PIN, LOW);
  relayState = true;
  relayStateMil = millis();
  server.send(200, "text/plain", "Manual override disabled");
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Default OFF


  WiFi.config(staticIP, gateway, subnet, primaryDNS, secondaryDNS);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  WiFiClient client;
  if (!client.connect("www.google.com", 80)) {
    Serial.println("DNS resolution failed!");
  } else {
    Serial.println("DNS resolution OK");
    client.stop();
  }
  // Start NTP client
  timeClient.begin();
  Serial.println("Fetching time...");
  int retryCount = 0;
  while (!timeClient.update() && retryCount < 10) {
    Serial.print(".");
    delay(1000); // Wait and retry
    retryCount++;
  }

  if (retryCount >= 10) {
    Serial.println("Failed to get NTP time! Using HTTP time as fallback.");
    Serial.println(getHTTPTime());
  } else {
    Serial.println("NTP time acquired.");
  }

  Serial.print("Current Time: ");
  Serial.println(timeClient.getFormattedTime());

  server.on("/", handleRoot);
  server.on("/enable", handleEnable);
  server.on("/disable", handleDisable);
  server.on("/status", handleStatus);
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/manualOn", handleManualOn);
  server.on("/manualOff", handleManualOff);
  server.begin();
}

void loop() {
  //delay(1000);
  server.handleClient();
  if (millis() - lastNtpUpdate > NTP_UPDATE_INTERVAL) {
    if (timeClient.update()) {
      Serial.println("Hora actualizada OK.");
    } else {
      Serial.println("Fallo al actualizar hora NTP.");
    }
    lastNtpUpdate = millis();
  }

  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  int totalMinutes = (currentHour * 60) + currentMinute;

  // If manual mode is ON, auto-disable after 5 min
  //    if (manualOverride && relayState && millis() - relayStartTime >= RELAY_DURATION) {
  //        digitalWrite(RELAY_PIN, HIGH);
  //        relayState = false;
  //        manualOverride = false;
  //    }

  Serial.print("milis: ");
  Serial.println(relayStateMil);
  Serial.print("currentHour: ");
  Serial.println(currentHour);
  Serial.print("timer: ");
  Serial.println((millis() - relayStateMil) / 1000);
  Serial.print("relay state: ");
  Serial.println(relayState);

  if (!manualOverride && totalMinutes > 570 && currentHour < 23) {
    if (!relayState) {
      if ((millis() - relayStateMil) > RELAY_WAIT) {
        digitalWrite(RELAY_PIN, LOW);
        Serial.println("ON!");
        relayState = true;
        relayStateMil = millis();
      }
    }
    if (relayState) {
      if ((millis() - relayStateMil) > RELAY_DURATION) {
        digitalWrite(RELAY_PIN, HIGH);
        Serial.println("OFF!");
        relayState = false;
        relayStateMil = millis();
      }

    }
  } else {
    if (!manualOverride && relayState) {
      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("OFF!");
      relayState = false;
    }
  }
  //  if (manualOverride)
  //  {
  //    if (relayState) {
  //      if ((millis() - relayStateMil) > RELAY_DURATION) {
  //        digitalWrite(RELAY_PIN, HIGH);
  //        Serial.println("OFF!");
  //        relayState = false;
  //        relayStateMil = millis();
  //      }
  //    }
  //  }
}
