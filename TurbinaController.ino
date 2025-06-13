#include <ESP8266WiFi.h>
#include <NTPClient.h>
//#include <WiFiUdp.h>
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

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
# define RELAY_PIN D3
# define LED_PIN D4
bool manualOverride = false;
bool relayState = false;
bool ledState = false;
unsigned long relayStartTime = 0;
unsigned long relayStateMil = 0;
unsigned long RELAY_DURATION = 2 * 60 * 1000; // 2 minutes in milliseconds
unsigned long RELAY_WAIT = 10 * 60 * 1000; // 10 minutes in milliseconds

unsigned long ledBlinkWait = 0;
unsigned long lastNtpUpdate = 0;
const unsigned long NTP_UPDATE_INTERVAL = 300 * 60 * 1000; // cada 300 minutos
int startMinutes = 545;
int endMinutes = 1380;

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
int timeStringToMinutes(const String& timeStr) {
  // Ejemplo de entrada: "12:34 AM"
  int hour = timeStr.substring(0, timeStr.indexOf(':')).toInt();
  int minute = timeStr.substring(timeStr.indexOf(':') + 1, timeStr.indexOf(' ')).toInt();
  String meridian = timeStr.substring(timeStr.indexOf(' ') + 1);

  // Convertir a formato 24 horas
  if (meridian == "AM") {
    if (hour == 12) hour = 0; // 12 AM es 00:00
  } else if (meridian == "PM") {
    if (hour != 12) hour += 12; // 1 PM → 13, 12 PM → 12
  }

  return hour * 60 + minute;
}

String minutesToTimeString(int totalMinutes) {
  int hour = totalMinutes / 60;
  int minute = totalMinutes % 60;

  String hourStr = (hour < 10) ? "0" + String(hour) : String(hour);
  String minuteStr = (minute < 10) ? "0" + String(minute) : String(minute);

  return hourStr + ":" + minuteStr;
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
                "#overlay {position: fixed;top: 0;left: 0;width: 100%;height: 100%;background: rgba(0, 0, 0, 0.5);display: none;align-items: center;justify-content: center;z-index: 1000;}"
                "#spinner {width: 4rem;height: 4rem;}"
                "</style>"
                "</head>"
                "<body>"
                "<div class='container mt-5 text-center'>"
                "<h1 class='mb-3'>Recirculador de aire</h1>"
                "<div class='card shadow-lg p-4'>"
                "<h4 class='mb-3'>Hora actual: <span id='timeDisplay'>" + timeClient.getFormattedTime() + "</span></h4>"
                "<h4 class='mb-3'>Estado de la turbina: <span id='statusDisplay' class='fw-bold'>" + (relayState ? "ON" : "OFF") + "</span></h4>"
                "<div id='overlay'>"
                "<div class='spinner-border text-light' id='spinner' role='status'>"
                "<span class='visually-hidden'>Loading...</span>"
                "</div>"
                "</div>"
                "<div class='d-flex justify-content-center gap-3'>"
                "<button id='enableBtn' class='btn btn-success btn-lg " + (relayState ? "disabled" : "" ) + "'onclick='toggleRelay(true)'>Prender</button>"
                "<button id='disableBtn' class='btn btn-danger btn-lg " + (!relayState ? "disabled" : "" ) + "'onclick='toggleRelay(false)'>Apagar</button>"
                "</div>"
                "<div class='row justify-content-center' style='margin-top: 10px;'>"
                "<div class='col-md-2'>"
                "<div class='form-check form-switch'>"
                "<input class='form-check-input' type='checkbox' " + (!manualOverride ? "checked = 'checked'" : "" ) + " role='switch' id='manualOverride' onchange='toggleManualOverride(this.checked)'>"
                "<label class='form-check-label' for='manualOverride'>Modo automatico</label>"
                "</div>"
                "</div>"
                "</div>"
                "<br />"
                "<div class='container'>"
                "<button class='btn btn-secondary my-3' type='button' data-bs-toggle='collapse'"
                "data-bs-target='#configSection' aria-expanded='false' aria-controls='configSection'>"
                "Mostrar / Ocultar Configuración"
                "</button>"
                "<div class='collapse' id='configSection'>"
                "<h4 style='margin-top: 30px;'>Configuración</h4>"
                "<h5 style='margin-top: 10px;'>Tiempos de operación</h5>"
                "<div class='row justify-content-center'>"
                "<div class='col-md-6'>"
                "<div class='row'>"
                "<div class='col'>"
                "<label>Run time (minutos):</label>"
                "<input type='number' id='relayDuration' class='form-control' value='" + String(RELAY_DURATION / 60000) + "'>"
                "</div>"
                "<div class='col'>"
                "<label>Tiempo de espera (minutos):</label>"
                "<input type='number' id='relayWait' class='form-control' value='" + String(RELAY_WAIT / 60000) + "'>"
                "</div>"
                "</div>"
                "<h5 style='margin-top: 10px;'>Hora de inicio y fin</h5>"
                "<div class='row'>"
                "<div class='col'>"
                "<label>Inicio:</label>"
                "<input type='time' id='startTime' class='form-control' value='" + minutesToTimeString(startMinutes) + "'>"
                "</div>"
                "<div class='col'>"
                "<label>Fin:</label>"
                "<input type='time' id='endTime' class='form-control' value='" + minutesToTimeString(endMinutes) + "'>"
                "</div>"
                "</div>"
                "<button class='btn btn-primary mt-2' onclick='updateConfig()'>Guardar</button>"
                "</div>"
                "</div>"
                "</div>"
                "</div>"
                "</div>"
                "<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js'></script>"
                "<script>"
                "var updateInterval = null;"
                "function showSpinner() {document.getElementById('overlay').style.display = 'flex';}"
                "function hideSpinner() {document.getElementById('overlay').style.display = 'none';}"
                "function updateStatus() {clearInterval(updateInterval);"
                "fetch('/status').then(response => response.json()).then(data => {"
                "document.getElementById('timeDisplay').innerText = data.time;"
                "document.getElementById('statusDisplay').innerText = data.relay ? 'ON' : 'OFF';"
                "document.getElementById('enableBtn').classList.toggle('disabled', data.relay);"
                "document.getElementById('disableBtn').classList.toggle('disabled', !data.relay);"
                "updateInterval = setInterval(updateStatus, 2000);}).finally(hideSpinner);}"
                "function toggleRelay(state) {clearInterval(updateInterval);showSpinner();fetch(state ? '/enable' : '/disable').then(() => updateStatus());}"
                "function toggleManualOverride(state) {fetch(state ? '/manualOff' : '/manualOn');}"
                "function updateConfig() {"
                "showSpinner();"
                "const duration = document.getElementById('relayDuration').value * 60000;"
                "const wait = document.getElementById('relayWait').value * 60000;"
                "const startTime = document.getElementById('startTime').value ;"
                "const endTime = document.getElementById('endTime').value ;"
                "fetch('/config?duration=' + duration + '&wait=' + wait + '&startTime=' + startTime + '&endTime=' + endTime, { method: 'POST' });}"
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
  if (server.hasArg("startTime")) startMinutes = timeStringToMinutes(server.arg("startTime"));
  if (server.hasArg("endTime")) endMinutes = timeStringToMinutes(server.arg("endTime"));

  //  if (server.hasArg("ssidName")) {
  //    ssid = server.arg("ssidName");
  //  }
  //  if (server.hasArg("pss")) {
  //    bool resertConn = password != server.arg("pss") ? true : false;
  //    password = server.arg("pss");
  //    if (resertConn) {
  //      resetWiFi();
  //    }
  //  }
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
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Default OFF

  //wifi manager

  WiFiManager wifiManager;
  //reset saved settings
  //wifiManager.resetSettings();

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("Recirculador");

  wifiManager.setTimeout(180); // 3 minutos
  if (!wifiManager.autoConnect("ConfigAP")) {
    Serial.println("Failed to connect, restarting...");
    wifiManager.resetSettings();
    ESP.restart();
  }
  WiFi.config(staticIP, gateway, subnet, primaryDNS, secondaryDNS);
  Serial.print(F("WiFi connected! IP address: "));
  Serial.println(WiFi.localIP());

  //end wifimanager
  //  WiFi.config(staticIP, gateway, subnet, primaryDNS, secondaryDNS);
  //  WiFi.begin(ssid, password);
  //
  //  Serial.print("Connecting to WiFi");
  //  while (WiFi.status() != WL_CONNECTED) {
  //    delay(500);
  //    Serial.print(".");
  //  }
  //  Serial.println(" Connected!");
  //  Serial.print("IP Address: ");
  //  Serial.println(WiFi.localIP());

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
  bool ledStat = false;
  while (!timeClient.update() && retryCount < 10) {
    digitalWrite(LED_PIN, ledStat ? HIGH : LOW);
    ledStat = !ledStat;
    Serial.print(".");
    delay(1000); // Wait and retry
    retryCount++;
  }
  digitalWrite(LED_PIN, HIGH);
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
  if (millis() - ledBlinkWait > (ledState ? 100 : 5000)) {
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    ledState = !ledState;
    ledBlinkWait = millis();

  }
  int connTries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    ledState = !ledState;
    delay(500);
    connTries++;
    if (connTries > 20)
    {
      ESP.restart();
    }
  }
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

  //  Serial.print("milis: ");
  //  Serial.println(relayStateMil);
  //  Serial.print("currentHour: ");
  //  Serial.println(currentHour);
  //  Serial.print("timer: ");
  //  Serial.println((millis() - relayStateMil) / 1000);
  //  Serial.print("relay state: ");
  //  Serial.println(relayState);

  if (!manualOverride && totalMinutes > startMinutes && totalMinutes < endMinutes) {
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
