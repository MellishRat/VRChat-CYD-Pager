// VRChat CYD Pager - ESP32-2432S028R ("Cheap Yellow Display")
// Receives OSC strings over Wi-Fi and renders them on the built-in ILI9341.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <SPI.h>

namespace Pins {
constexpr uint8_t TFT_CS = 15, TFT_DC = 2, TFT_RST = 4;
constexpr uint8_t TFT_SCK = 14, TFT_MOSI = 13, TFT_MISO = 12;
constexpr uint8_t BACKLIGHT = 21, CONFIG_BUTTON = 0;
}

constexpr char VERSION[] = "1.0.0";
constexpr uint16_t OSC_PORT_DEFAULT = 9001;
constexpr uint16_t C_NAVY = 0x0861, C_CYAN = 0x4F7F, C_WHITE = 0xFFFF;
constexpr uint16_t C_MUTED = 0x9CF3, C_GREEN = 0x46A9, C_RED = 0xF986;

SPIClass displaySPI(HSPI);
WiFiUDP oscUdp;
WebServer server(80);
DNSServer dns;
Preferences prefs;
String wifiSsid, wifiPassword, deviceName = "VRChat CYD Pager";
String lastMessage = "Waiting for an OSC message...";
String lastSender = "System";
uint16_t oscPort = OSC_PORT_DEFAULT;
bool portalMode = false;
unsigned long lastPacketAt = 0;

void tftCommand(uint8_t command) {
  digitalWrite(Pins::TFT_DC, LOW); digitalWrite(Pins::TFT_CS, LOW);
  displaySPI.transfer(command); digitalWrite(Pins::TFT_CS, HIGH);
}
void tftData(uint8_t data) {
  digitalWrite(Pins::TFT_DC, HIGH); digitalWrite(Pins::TFT_CS, LOW);
  displaySPI.transfer(data); digitalWrite(Pins::TFT_CS, HIGH);
}
void tftWindow(int x0, int y0, int x1, int y1) {
  tftCommand(0x2A); tftData(x0 >> 8); tftData(x0); tftData(x1 >> 8); tftData(x1);
  tftCommand(0x2B); tftData(y0 >> 8); tftData(y0); tftData(y1 >> 8); tftData(y1);
  tftCommand(0x2C);
}
void fillRect(int x, int y, int w, int h, uint16_t color) {
  if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
  if (x + w > 320) w = 320 - x; if (y + h > 240) h = 240 - y;
  if (w <= 0 || h <= 0) return;
  tftWindow(x, y, x + w - 1, y + h - 1);
  digitalWrite(Pins::TFT_DC, HIGH); digitalWrite(Pins::TFT_CS, LOW);
  for (uint32_t i = 0; i < uint32_t(w) * h; ++i) { displaySPI.transfer16(color); }
  digitalWrite(Pins::TFT_CS, HIGH);
}

// Compact 5x7 ASCII font, encoded as five vertical columns per glyph (0x20-0x7f).
const uint8_t FONT[] PROGMEM = {
0,0,0,0,0,0,0,95,0,0,0,7,0,7,0,20,127,20,127,20,36,42,127,42,18,35,19,8,100,98,54,73,85,34,80,0,5,3,0,0,0,28,34,65,0,0,65,34,28,0,20,8,62,8,20,8,8,62,8,8,0,80,48,0,0,8,8,8,8,8,0,96,96,0,0,32,16,8,4,2,62,81,73,69,62,0,66,127,64,0,66,97,81,73,70,33,65,69,75,49,24,20,18,127,16,39,69,69,69,57,60,74,73,73,48,1,113,9,5,3,54,73,73,73,54,6,73,73,41,30,0,54,54,0,0,0,86,54,0,0,8,20,34,65,0,20,20,20,20,20,0,65,34,20,8,2,1,81,9,6,50,73,121,65,62,126,17,17,17,126,127,73,73,73,54,62,65,65,65,34,127,65,65,34,28,127,73,73,73,65,127,9,9,9,1,62,65,73,73,122,127,8,8,8,127,0,65,127,65,0,32,64,65,63,1,127,8,20,34,65,127,64,64,64,64,127,2,12,2,127,127,4,8,16,127,62,65,65,65,62,127,9,9,9,6,62,65,81,33,94,127,9,25,41,70,70,73,73,73,49,1,1,127,1,1,63,64,64,64,63,31,32,64,32,31,63,64,56,64,63,99,20,8,20,99,7,8,112,8,7,97,81,73,69,67,0,127,65,65,0,2,4,8,16,32,0,65,65,127,0,4,2,1,2,4,64,64,64,64,64,0,1,2,4,0,32,84,84,120,64,127,40,68,68,56,56,68,68,68,40,56,68,68,40,127,56,84,84,84,24,8,126,9,1,2,12,82,82,82,62,127,8,4,4,120,0,68,125,64,0,32,64,68,61,0,127,16,40,68,0,0,65,127,64,0,124,4,24,4,120,124,8,4,4,120,56,68,68,68,56,124,20,20,20,8,8,20,20,24,124,124,8,4,4,8,72,84,84,84,32,4,63,68,64,32,60,64,64,32,124,28,32,64,32,28,60,64,48,64,60,68,40,16,40,68,12,80,80,80,60,68,100,84,76,68,0,8,54,65,0,0,0,127,0,0,0,65,54,8,0,8,4,8,16,8,0,0,0,0,0};

void drawChar(int x, int y, char ch, uint16_t color, uint8_t scale = 2) {
  if (ch < 32 || ch > 127) ch = '?';
  uint16_t index = (ch - 32) * 5;
  for (uint8_t col = 0; col < 5; ++col) {
    uint8_t bits = pgm_read_byte(&FONT[index + col]);
    for (uint8_t row = 0; row < 7; ++row)
      if (bits & (1 << row)) fillRect(x + col * scale, y + row * scale, scale, scale, color);
  }
}
void drawText(int x, int y, String text, uint16_t color, uint8_t scale = 2) {
  for (size_t i = 0; i < text.length(); ++i) {
    drawChar(x, y, text[i], color, scale); x += 6 * scale;
  }
}
void drawWrapped(String text, int x, int y, int maxChars, int maxLines, uint16_t color) {
  text.replace("\r", " "); text.replace("\n", " ");
  int line = 0;
  while (text.length() && line < maxLines) {
    while (text.startsWith(" ")) text.remove(0, 1);
    int take = min(maxChars, (int)text.length());
    if (take < (int)text.length()) {
      int space = text.substring(0, take + 1).lastIndexOf(' ');
      if (space > maxChars / 2) take = space;
    }
    String part = text.substring(0, take);
    drawText(x, y + line * 22, part, color, 2);
    text.remove(0, take); ++line;
  }
  if (text.length() && line > 0) drawText(x + (maxChars - 3) * 12, y + (line - 1) * 22, "...", color, 2);
}
void initDisplay() {
  pinMode(Pins::TFT_CS, OUTPUT); pinMode(Pins::TFT_DC, OUTPUT); pinMode(Pins::TFT_RST, OUTPUT);
  pinMode(Pins::BACKLIGHT, OUTPUT); digitalWrite(Pins::BACKLIGHT, LOW);
  displaySPI.begin(Pins::TFT_SCK, Pins::TFT_MISO, Pins::TFT_MOSI, Pins::TFT_CS);
  digitalWrite(Pins::TFT_RST, LOW); delay(20); digitalWrite(Pins::TFT_RST, HIGH); delay(120);
  const uint8_t init[] = {0x01,0x11,0x3A,0x55,0x36,0x28,0x29};
  tftCommand(init[0]); delay(120); tftCommand(init[1]); delay(120);
  tftCommand(init[2]); tftData(init[3]); tftCommand(init[4]); tftData(init[5]); tftCommand(init[6]);
  fillRect(0, 0, 320, 240, C_NAVY); digitalWrite(Pins::BACKLIGHT, HIGH);
}
void renderScreen() {
  fillRect(0, 0, 320, 240, C_NAVY);
  fillRect(0, 0, 320, 35, C_CYAN); drawText(12, 10, "VRCHAT CYD PAGER", C_NAVY, 2);
  drawText(12, 48, lastSender.substring(0, 24), C_CYAN, 2);
  drawWrapped(lastMessage, 12, 76, 24, 6, C_WHITE);
  fillRect(0, 218, 320, 22, 0x10A2);
  String status = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() + "  UDP:" + String(oscPort) : "SETUP: 192.168.4.1";
  drawText(8, 222, status.substring(0, 37), WiFi.status() == WL_CONNECTED ? C_GREEN : C_RED, 1);
}

String htmlEscape(String s) {
  s.replace("&", "&amp;"); s.replace("<", "&lt;"); s.replace(">", "&gt;"); s.replace("\"", "&quot;"); return s;
}
String page() {
  String h = F("<!doctype html><meta name=viewport content='width=device-width'><title>VRChat CYD Pager</title><style>body{font:16px system-ui;background:#071525;color:#e8f7ff;max-width:620px;margin:40px auto;padding:0 20px}main{background:#10243a;padding:24px;border-radius:18px}input{box-sizing:border-box;width:100%;padding:12px;margin:6px 0 16px;border-radius:8px;border:1px solid #47708f}button{background:#45d8df;color:#06202d;border:0;border-radius:8px;padding:12px 18px;font-weight:700}.muted{color:#a9bdca}</style><main><h1>VRChat CYD Pager</h1>");
  h += "<p class=muted>Firmware " + String(VERSION) + " &middot; " + (portalMode ? "setup mode" : htmlEscape(WiFi.localIP().toString())) + "</p>";
  h += F("<form method=post action=/save><label>Wi-Fi name</label><input name=ssid required value='"); h += htmlEscape(wifiSsid);
  h += F("'><label>Wi-Fi password</label><input type=password name=password placeholder='Leave blank to keep current password'><label>Device name</label><input name=name maxlength=31 value='"); h += htmlEscape(deviceName);
  h += F("'><label>OSC UDP port</label><input name=port type=number min=1 max=65535 value='"); h += String(oscPort);
  h += F("'><button>Save and restart</button></form><p class=muted>Send a string to <code>/cyd/message</code> or VRChat chatbox data to <code>/chatbox/input</code>.</p></main>"); return h;
}
void configureWeb() {
  server.on("/", HTTP_GET, [] { server.send(200, "text/html", page()); });
  server.on("/generate_204", HTTP_GET, [] { server.sendHeader("Location", "/", true); server.send(302); });
  server.on("/hotspot-detect.html", HTTP_GET, [] { server.send(200, "text/html", page()); });
  server.on("/save", HTTP_POST, [] {
    String ssid = server.arg("ssid"), password = server.arg("password"), name = server.arg("name");
    long port = server.arg("port").toInt();
    if (!ssid.length() || port < 1 || port > 65535) { server.send(400, "text/plain", "Invalid Wi-Fi name or port"); return; }
    prefs.begin("pager", false); prefs.putString("ssid", ssid);
    if (password.length()) prefs.putString("password", password);
    prefs.putString("name", name.length() ? name : "VRChat CYD Pager"); prefs.putUShort("port", port); prefs.end();
    server.send(200, "text/html", "<h1>Saved</h1><p>The pager is restarting...</p>"); delay(750); ESP.restart();
  });
  server.onNotFound([] { server.sendHeader("Location", "/", true); server.send(302); }); server.begin();
}
void startPortal() {
  portalMode = true; WiFi.mode(WIFI_AP); uint64_t id = ESP.getEfuseMac();
  String ap = "CYD-Pager-" + String((uint32_t)id, HEX).substring(4); ap.toUpperCase();
  WiFi.softAP(ap.c_str()); dns.start(53, "*", WiFi.softAPIP());
  lastSender = "SETUP MODE"; lastMessage = "Connect to " + ap + " then open 192.168.4.1"; configureWeb(); renderScreen();
}
bool connectWifi() {
  if (!wifiSsid.length()) return false;
  WiFi.mode(WIFI_STA); WiFi.setHostname("vrchat-cyd-pager"); WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  lastSender = "CONNECTING"; lastMessage = wifiSsid; renderScreen();
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) delay(250);
  return WiFi.status() == WL_CONNECTED;
}

int oscPaddedLength(const uint8_t *data, int length, int offset) {
  while (offset < length && data[offset]) ++offset;
  if (offset >= length) return -1; return (offset + 4) & ~3;
}
String oscString(const uint8_t *data, int length, int offset) {
  String out; while (offset < length && data[offset] && out.length() < 500) out += char(data[offset++]); return out;
}
void readOsc() {
  int size = oscUdp.parsePacket(); if (size <= 0 || size > 1024) return;
  uint8_t packet[1025]; int n = oscUdp.read(packet, min(size, 1024)); packet[n] = 0;
  String address = oscString(packet, n, 0); int typesAt = oscPaddedLength(packet, n, 0);
  if (typesAt < 0 || typesAt >= n || packet[typesAt] != ',') return;
  int valueAt = oscPaddedLength(packet, n, typesAt);
  if (valueAt < 0 || valueAt >= n || packet[typesAt + 1] != 's') return;
  String message = oscString(packet, n, valueAt); if (!message.length()) return;
  if (address == "/chatbox/input" || address == "/cyd/message" || address.endsWith("/message")) {
    lastSender = address == "/chatbox/input" ? "VRCHAT CHATBOX" : oscUdp.remoteIP().toString();
    lastMessage = message; lastPacketAt = millis(); renderScreen();
  }
}
void loadSettings() {
  prefs.begin("pager", true); wifiSsid = prefs.getString("ssid", ""); wifiPassword = prefs.getString("password", "");
  deviceName = prefs.getString("name", "VRChat CYD Pager"); oscPort = prefs.getUShort("port", OSC_PORT_DEFAULT); prefs.end();
}
void setup() {
  Serial.begin(115200); pinMode(Pins::CONFIG_BUTTON, INPUT_PULLUP); initDisplay(); loadSettings();
  if (digitalRead(Pins::CONFIG_BUTTON) == LOW || !connectWifi()) startPortal();
  else { portalMode = false; oscUdp.begin(oscPort); configureWeb(); lastSender = "READY"; lastMessage = "Forward VRChat OSC to this address"; renderScreen(); }
}
void loop() {
  server.handleClient(); if (portalMode) dns.processNextRequest(); else readOsc();
  static unsigned long pressedAt = 0;
  if (digitalRead(Pins::CONFIG_BUTTON) == LOW) { if (!pressedAt) pressedAt = millis(); if (millis() - pressedAt > 5000) { prefs.begin("pager", false); prefs.clear(); prefs.end(); ESP.restart(); } }
  else pressedAt = 0;
  delay(2);
}

