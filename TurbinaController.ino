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
const unsigned long RELAY_DURATION = 2 * 60 * 1000; // 2 minutes in milliseconds
const unsigned long RELAY_WAIT = 10 * 60 * 1000; // 10 minutes in milliseconds

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
                  "</head>"
                  "<body>"
                  "<div class='container mt-5 text-center'>"
                  "<h1 class='mb-3'>Recirculador de aire</h1>"
                  "<div class='card shadow-lg p-4'>"
                  "<h4 class='mb-3'>Hora actual: <span id='timeDisplay'>" + timeClient.getFormattedTime() + "</span></h4>"
                  "<h4 class='mb-3'>Estado de la turbina: <span id='statusDisplay' class='fw-bold'>" + (relayState ? "ON" : "OFF") + "</span></h4>"
                  "<div class='d-flex justify-content-center gap-3'>"
                  "<button id='enableBtn' class='btn btn-success btn-lg " + (relayState ? "disabled" : "") + "' onclick='toggleRelay(true)'>Prender</button>"
                  "<button id='disableBtn' class='btn btn-danger btn-lg " + (!relayState ? "disabled" : "") + "' onclick='toggleRelay(false)'>Apagar</button>"
                  "</div>"
                  "</div>"
                  "</div>"
                  "<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/js/bootstrap.bundle.min.js'></script>"
                  "<script>"
                  "function updateStatus() {"
                  "  fetch('/status').then(response => response.json()).then(data => {"
                  "    document.getElementById('timeDisplay').innerText = data.time;"
                  "    document.getElementById('statusDisplay').innerText = data.relay ? 'ON' : 'OFF';"
                  "    document.getElementById('enableBtn').classList.toggle('disabled', data.relay);"
                  "    document.getElementById('disableBtn').classList.toggle('disabled', !data.relay);"
                  "  });"
                  "}"
                  "function toggleRelay(state) {"
                  "  fetch(state ? '/enable' : '/disable').then(() => updateStatus());"
                  "}"
                  "setInterval(updateStatus, 2000);"
                  "</script>"
                  "</body>"
                  "</html>";
    server.send(200, "text/html", html);
}
void handleEnable() {
  manualOverride = true;
  digitalWrite(RELAY_PIN, LOW); // Active LOW relay
  relayState = true;
  relayStartTime = millis(); // Start timer for auto-off
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Turbine enabled");
  relayStateMil = millis();
}

void handleDisable() {
  manualOverride = true;
  digitalWrite(RELAY_PIN, HIGH); // Active LOW relay
  relayState = false;
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Turbine disabled");
  relayStateMil = millis();
}
void handleStatus() {
    String json = "{\"relay\":" + String(relayState) + ",\"time\":\"" + timeClient.getFormattedTime() + "\"}";
    server.send(200, "application/json", json);
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
  server.begin();
}

void loop() {
  delay(1000);
  timeClient.update();
  server.handleClient();

  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  //int totalMinutes = currentHour * 60 + currentMinute;

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

  if (!manualOverride && currentHour > 9 && currentHour < 23) {
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
  }
  if (manualOverride)
  {
    if (relayState) {
      if ((millis() - relayStateMil) > RELAY_DURATION) {
        digitalWrite(RELAY_PIN, HIGH);
        Serial.println("OFF!");
        relayState = false;
        relayStateMil = millis();
        manualOverride=false;
      }
    }
  }

}
