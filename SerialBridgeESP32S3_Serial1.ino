#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <time.h>
#include <lwip/dns.h>

// Interface to scanning device
#define RXD_PIN 18
#define TXD_PIN 17
// To reset EEPROM, pull this pin to GND on boot  
#define RESET_PIN 21

#define EEPROM_SIZE 160
#define SERIAL_BAUD 115200
#define TELNET_PORT 23

// Waiting time in seconds to let scanning device boot up before try adjusting its time
#define SetTimeDelay 45

#define NumOfNTPServers 6
String ntpServers[NumOfNTPServers] = {
  "", // Placeholders for now, will hold value from EEPROM
  "", // will hold DNS server (which in most SoHo scenarios is your router which also can act as a NTP server)
  "fritz.box", // Common home router in DE
  "pool.ntp.org",
  "time.google.com",
  "time.windows.com"
};

WiFiServer telnetServer(TELNET_PORT);
WiFiClient telnetClient;

WebServer server(80);

char ssid[32] = "";
char password[64] = "";
char tzString[32] = "";
// char DefaulttzString[32] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
char ntpServer[32] = "";

bool wifiConnected = false;
unsigned long startTime;
bool timeSent = false;

void loadCredentials() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, ssid);
  EEPROM.get(32, password);
  EEPROM.get(96, tzString);
  EEPROM.get(128, ntpServer);
  EEPROM.end();
  Serial.print("\nReading from EEPROM:\nSSID: ");
  Serial.print(ssid);
  Serial.print(", TZ: ");
  // strcpy(tzString, DefaulttzString);
  Serial.print(tzString);
  Serial.print(", NTP Server: ");
  Serial.println(ntpServer);
}

void saveCredentials(const char* newSSID, const char* newPassword, const char* newTZ, const char* newNTP) {
  EEPROM.begin(EEPROM_SIZE);
  memset(ssid, 0, sizeof(ssid));
  memset(password, 0, sizeof(password));
  memset(tzString, 0, sizeof(tzString));
  memset(ntpServer, 0, sizeof(ntpServer));
  strncpy(ssid, newSSID, sizeof(ssid) - 1);
  strncpy(password, newPassword, sizeof(password) - 1);
  strncpy(tzString, newTZ, sizeof(tzString) - 1);
  strncpy(ntpServer, newNTP, sizeof(ntpServer) - 1);
  EEPROM.put(0, ssid);
  EEPROM.put(32, password);
  EEPROM.put(96, tzString);
  EEPROM.put(128, ntpServer);
  EEPROM.commit();
  EEPROM.end();
  Serial.print("\nSaving to EEPROM:\nSSID: ");
  Serial.print(ssid);
  Serial.print(", TZ: ");
  Serial.print(tzString);
  Serial.print(", NTP Server: ");
  Serial.println(ntpServer);
}

void startAPMode() {
  WiFi.softAP("SerialServer");
  IPAddress IP = WiFi.softAPIP();
  Serial.println("AP Mode. Connect to SSID: SerialServer");
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html",
      "<!DOCTYPE html><html><body><h1>Filmscanner Serial Interface Setup</h1><form action=\"/save\" method=\"POST\"><table>"
      "<tr><td>SSID:</td><td><input name=\"ssid\"></td><td>Max. 32 characters</td></tr>"
      "<tr><td>Password:</td><td><input name=\"pass\" type=\"password\"></td><td>Max. 64 char., other limitations may apply</td></tr>"
      "<tr><td>NTP Server:</td><td><input name=\"ntp\"></td><td>Optional. Define own NTP server to override defaults: address of your DNS, fritz.box, pool.ntp.org</td></tr>"

      "<tr><td>Region:</td><td><select name=\"region\">"
      "<option value=\"UTC0\">UTC</option>"
      "<option value=\"AWST-8\">Australia/Perth</option>"
      "<option value=\"AEST-10AEDT,M10.1.0/2,M4.1.0/3\">Australia/Sydney</option>"
      "<option value=\"CST6\">America/Mexico_City</option>"
      "<option value=\"CST6CDT,M3.2.0/2,M11.1.0/2\">America/Chicago</option>"
      "<option value=\"EST5EDT,M3.2.0/2,M11.1.0/2\">America/New_York</option>"
      "<option value=\"MST7MDT,M3.2.0/2,M11.1.0/2\">America/Denver</option>"
      "<option value=\"PST8PDT,M3.2.0/2,M11.1.0/2\">America/Los_Angeles</option>"
      "<option value=\"-03\">America/Sao_Paulo</option>"
      "<option value=\"SAST-2\">Africa/Johannesburg</option>"
      "<option value=\"IST-5:30\">Asia/Kolkata</option>"
      "<option value=\"CST-8\">Asia/Shanghai</option>"
      "<option value=\"SGT-8\">Asia/Singapore</option>"
      "<option value=\"JST-9\">Asia/Tokyo</option>"
      "<option value=\"CET-1CEST,M3.5.0/2,M10.5.0/3\">Europe/Berlin</option>"
      "<option value=\"GMT0BST,M3.5.0/1,M10.5.0/2\">Europe/London</option>"
      "<option value=\"+03\">Europe/Istanbul</option>"
      "<option value=\"FET-3\">Europe/Minsk</option>"
      "</select></td><td>Choose your location to define proper time zone</td></tr>"
      "<tr><td>Custom TZ:</td><td><input name=\"tz\"></td><td>Optional, if your region is not on the list. Must be in POSIX notation, max. 32 char.</td></tr>"

      "<tr><td colspan=2><input type=\"submit\" value=\"Save\"></td><td>Will save inputs to EEPROM and reboot</td></tr>"
      "<table></form></body></html>");
  });

  server.on("/save", HTTP_POST, []() {
    String newSSID = server.arg("ssid");
    String newPass = server.arg("pass");
    String newTZ = server.arg("tz");
    if (newTZ.length() == 0) {
      newTZ = server.arg("region");
    }
    String newNTP = server.arg("ntp");
    saveCredentials(newSSID.c_str(), newPass.c_str(), newTZ.c_str(), newNTP.c_str());
    server.send(200, "text/html", "<html><body>Saved. Rebooting...</body></html>");
    delay(1000);
    ESP.restart();
  });

  server.begin();
}

void connectToWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  for (int i = 0; i < 30; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiConnected = true;
      Serial.print("\nConnected to WiFi: ");
      Serial.print(ssid);
      Serial.print("; IP address: ");
      Serial.println(WiFi.localIP());
      return;
    }
    delay(500);
    Serial.print(".");
  }
  wifiConnected = false;
  Serial.println("\nFailed to connect.");
}

bool setupTime() {
  const int maxCycles = 10;
  const int maxWaitPerServer = 5; // seconds

  ntpServers[0] = ntpServer; // from EEPROM
  IPAddress dns = WiFi.dnsIP();
  if (dns != INADDR_NONE) {
    Serial.print("Will use NTP from DNS: ");
    Serial.println(dns.toString().c_str());    
    ntpServers[1] = dns.toString();
  }

  struct tm timeinfo;

  if (strlen(tzString) > 0) {
    Serial.print("Using TZ: ");
    Serial.println(tzString);
  }

  for (int cycle = 0; cycle < maxCycles; ++cycle) {
    Serial.printf("NTP sync attempt cycle %d...\n", cycle + 1);

    for (int i = 0; i < NumOfNTPServers; ++i) {
      if (ntpServers[i].length() > 0) {

        Serial.printf("Trying NTP server %d: %s\n", i, ntpServers[i]);
        configTzTime(tzString, ntpServers[i].c_str());

        for (int j = 0; j < maxWaitPerServer; ++j) {
          if (getLocalTime(&timeinfo)) {
            Serial.print("Time successfully obtained: ");
            Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");
            return true;
          }
          delay(1000);
          Serial.print(".");
        }
        Serial.println("\nTimeout for this server.");
      } else {
        Serial.printf("Skipping NTP server %d: %s\n", i, ntpServers[i]);
      }
    }
  }
  Serial.println("Failed to sync time after all attempts.");
  return false;
}

String intToReadableString(int value) {
  switch (value) {
    case 0: return "<NUL>";
    case 1: return "<SOH>";
    case 2: return "<STX>";
    case 3: return "<ETX>";
    case 4: return "<EOT>";
    case 5: return "<ENQ>";
    case 6: return "<ACK>";
    case 7: return "<BEL>";
    case 8: return "<BS>";
    case 9: return "<Tab>";
    case 10: return "<LF>";
    case 11: return "<VT>";
    case 12: return "<FF>";
    case 13: return "<CR>";
    case 14: return "<SO>";
    case 15: return "<SI>";
    case 16: return "<DLE>";
    case 17: return "<DC1>";
    case 18: return "<DC2>";
    case 20: return "<DC4>";
    case 21: return "<NAK>";
    case 22: return "<SYN>";
    case 23: return "<ETB>";
    case 24: return "<CAN>";
    case 25: return "<EM>";
    case 26: return "<SUB>";
    case 27: return "<ESC>";
    case 28: return "<FS>";
    case 29: return "<GS>";
    case 30: return "<RS>";
    case 31: return "<US>";
    case 127: return "<DEL>";
    default:
      if (value >= 32 && value <= 126) {
        return String((char)value);  // Printable ASCII
      } else {
        return "<" + String(value) + ">";  // Unknown or extended
      }
  }
}

void ResetEEPROM()
{
    EEPROM.begin(EEPROM_SIZE);
    for (int i = 0; i < EEPROM_SIZE; i++) {
      EEPROM.write(i, 0);
    }
    EEPROM.commit();
    EEPROM.end();
}

void setup() {
  pinMode(RESET_PIN, INPUT_PULLUP);
  if (digitalRead(RESET_PIN) == LOW) {
    Serial.println("Reset Pin triggered. Erasing EEPROM...");
    ResetEEPROM();
    delay(5000);
    ESP.restart();
  }

  // For debug messages
  Serial.begin(SERIAL_BAUD);
  // Connection to scanner
  Serial1.begin(SERIAL_BAUD, SERIAL_8N1, RXD_PIN, TXD_PIN);
  delay(1000);

  loadCredentials();

  if (strlen(ssid) == 0 || strlen(password) == 0) {
    startAPMode();
  } else {
    connectToWiFi();
    if (!wifiConnected) {
      startAPMode();
    }
  }

  if (wifiConnected) {
    telnetServer.begin();
    telnetServer.setNoDelay(true);
    setupTime();
    startTime = millis();
  }
}

void loop() {
  int sread;
  int tread;
  if (!wifiConnected) {
    server.handleClient();
    return;
  }

  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      telnetClient = telnetServer.available();
      Serial.println("Telnet client connected.");
    } else {
      WiFiClient newClient = telnetServer.available();
      newClient.println("Only one client allowed.");
      newClient.stop();
    }
  }

  if (telnetClient && telnetClient.connected()) {
    while (telnetClient.available()) {
      tread = telnetClient.read();
      Serial1.write(tread);
      Serial.println("TR+SW: " + intToReadableString(tread));
    }
  }

  while (Serial1.available()) {
    if (telnetClient && telnetClient.connected()) {
      sread = Serial1.read();
      telnetClient.write(sread);
      Serial.println("SR+TW: " + intToReadableString(sread));
    } else {
      sread = Serial1.read(); // discard
      Serial.println("SR: " + intToReadableString(sread));
    }
  }

  // Send date / time SetTimeDelay seconds after boot
  if (!timeSent && millis() - startTime > (SetTimeDelay * 1000)) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char buf[64];
      snprintf(buf, sizeof(buf), "hwclk set %02d/%02d/%02d/%02d/%02d/%02d\n",
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec);
      Serial.print("Sending time: ");
      Serial.println(buf);
      Serial1.write(buf);
      timeSent = true;
    }
  }
}
