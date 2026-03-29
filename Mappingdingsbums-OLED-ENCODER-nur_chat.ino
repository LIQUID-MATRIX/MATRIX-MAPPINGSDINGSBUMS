/*
 * MatrixChat.ino  –  ESP32 Mini Chat Display für Matrix-Räume
 * ─────────────────────────────────────────────────────────────
 * Hardware:
 *   ESP32 Dev Module
 *   SSD1306 OLED 128×64 via I2C (SDA=21, SCL=22)
 *   Rotary Encoder  CLK=32  DT=33  SW=34
 *   HINWEIS: GPIO34 hat keinen internen Pull-up!
 *            → 10kΩ extern von SW nach 3.3V einbauen.
 *
 * Bibliotheken (Arduino Library Manager):
 *   Adafruit SSD1306   (+ Adafruit GFX)
 *   ArduinoJson        Version 7.x
 * ─────────────────────────────────────────────────────────────
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ═══════════════════════════════════════════
//  KONFIGURATION  ← hier anpassen
// ═══════════════════════════════════════════
const char* WIFI_SSID   = "DEIN_WLAN_NAME";
const char* WIFI_PASS   = "DEIN_WLAN_PASSWORT";

const char* MATRIX_HOST = "matrix.org";
const int   MATRIX_PORT = 443;
const char* MATRIX_USER = "@user:matrix.org";   // vollständige MXID
const char* MATRIX_PASS = "dein_passwort";
const char* MATRIX_ROOM = "!RoomId:matrix.org"; // Interne Raum-ID: Element → Einstellungen → Erweitert

// ═══════════════════════════════════════════
//  PINS
// ═══════════════════════════════════════════
#define OLED_SDA   21
#define OLED_SCL   22
#define OLED_ADDR  0x3C
#define OLED_W     128
#define OLED_H     64
#define ENC_CLK    32
#define ENC_DT     33
#define ENC_SW     34   // externer 10kΩ Pull-up nach 3.3V nötig!

// ═══════════════════════════════════════════
//  LAYOUT & TIMING
// ═══════════════════════════════════════════
#define HDR_H      10
#define LINE_H      9
#define VISIBLE     5
#define MAX_MSG    40
#define SYNC_MS    10000UL   // Polling-Intervall
#define TO_INIT    30000UL   // Timeout erster Sync
#define TO_POLL    15000UL   // Timeout Folge-Syncs

// ═══════════════════════════════════════════
//  GLOBALE VARIABLEN
// ═══════════════════════════════════════════
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);

struct Msg { String from; String text; };
Msg    msgs[MAX_MSG];
int    msgCount  = 0;
int    scrollPos = 0;

String accessToken = "";
String nextBatch   = "";

int           lastCLK   = HIGH;
unsigned long lastBtnMs = 0;
bool          btnHeld   = false;
int           encDelta  = 0;
unsigned long lastSyncMs = 0;

// ─────────────────────────────────────────
//  Vorwärts-Deklarationen
// ─────────────────────────────────────────
String readResponse(WiFiClientSecure& c, unsigned long timeoutMs);
String httpsGET(const String& path, unsigned long timeoutMs);
String httpsPOST(const String& path, const String& body);

// ═══════════════════════════════════════════
//  NACHRICHTEN-PUFFER
// ═══════════════════════════════════════════
void pushMsg(const String& from, const String& text) {
  if (msgCount < MAX_MSG) {
    msgs[msgCount++] = {from, text};
  } else {
    for (int i = 0; i < MAX_MSG - 1; i++) msgs[i] = msgs[i + 1];
    msgs[MAX_MSG - 1] = {from, text};
  }
  scrollPos = max(0, msgCount - VISIBLE);
}

String shorten(const String& s, int n) {
  return ((int)s.length() <= n) ? s : s.substring(0, n - 1) + "~";
}

// ═══════════════════════════════════════════
//  DISPLAY
// ═══════════════════════════════════════════
void showStatus(const String& l1, const String& l2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 18); display.println(l1);
  if (l2.length()) { display.setCursor(0, 30); display.println(l2); }
  display.display();
}

void drawChat() {
  display.clearDisplay();

  // Kopfzeile (invertiert)
  display.fillRect(0, 0, OLED_W, HDR_H, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(shorten(String(MATRIX_ROOM), 19));
  display.setCursor(OLED_W - 6, 1);
  display.print(WiFi.status() == WL_CONNECTED ? "+" : "!");

  // Nachrichten
  display.setTextColor(SSD1306_WHITE);
  int y = HDR_H + 2;
  for (int i = 0; i < VISIBLE; i++) {
    int idx = scrollPos + i;
    if (idx >= msgCount) break;
    display.setCursor(0, y);
    display.print(shorten(msgs[idx].from, 7) + ":" + shorten(msgs[idx].text, 13));
    y += LINE_H;
  }

  // Scrollbalken
  if (msgCount > VISIBLE) {
    int total = OLED_H - HDR_H;
    int bh    = max(4, VISIBLE * total / msgCount);
    int by    = HDR_H + scrollPos * (total - bh) / max(1, msgCount - VISIBLE);
    display.drawFastVLine(OLED_W - 2, HDR_H, total, SSD1306_WHITE);
    display.fillRect(OLED_W - 2, by, 2, bh, SSD1306_WHITE);
  }

  display.display();
}

// ═══════════════════════════════════════════
//  HTTP (Chunked-Transfer-aware)
// ═══════════════════════════════════════════
String readResponse(WiFiClientSecure& c, unsigned long timeoutMs) {
  String       resp      = "";
  bool         inHeader  = true;
  bool         chunked   = false;
  bool         firstLine = true;
  unsigned long t        = millis();

  while (c.connected() || c.available()) {
    if (millis() - t > timeoutMs) {
      Serial.printf("[HTTP] Timeout nach %lums\n", timeoutMs);
      break;
    }
    if (!c.available()) { delay(1); continue; }

    String line = c.readStringUntil('\n');

    if (inHeader) {
      if (firstLine) { Serial.println("[HTTP] " + line); firstLine = false; }
      String lo = line; lo.toLowerCase();
      if (lo.indexOf("transfer-encoding: chunked") >= 0) chunked = true;
      if (line == "\r") inHeader = false;
    } else {
      String tr = line; tr.trim();
      if (!tr.length()) continue;
      if (chunked) {
        // Chunk-Größen-Zeilen (reine Hex-Zahlen) überspringen
        bool isSize = true;
        for (char ch : tr) if (!isHexadecimalDigit(ch)) { isSize = false; break; }
        if (!isSize) resp += tr;
      } else {
        resp += tr;
      }
    }
  }
  Serial.printf("[HTTP] Body: %d Bytes\n", resp.length());
  return resp;
}

String httpsGET(const String& path, unsigned long timeoutMs = 8000) {
  WiFiClientSecure c; c.setInsecure();
  Serial.printf("[HTTP] GET %s\n", path.c_str());
  if (!c.connect(MATRIX_HOST, MATRIX_PORT)) { Serial.println("[HTTP] Verbindung fehlgeschlagen!"); return ""; }
  c.print("GET " + path + " HTTP/1.1\r\n"
          "Host: " + String(MATRIX_HOST) + "\r\n"
          "Authorization: Bearer " + accessToken + "\r\n"
          "Connection: close\r\n\r\n");
  return readResponse(c, timeoutMs);
}

String httpsPOST(const String& path, const String& body) {
  WiFiClientSecure c; c.setInsecure();
  Serial.printf("[HTTP] POST %s\n", path.c_str());
  if (!c.connect(MATRIX_HOST, MATRIX_PORT)) { Serial.println("[HTTP] Verbindung fehlgeschlagen!"); return ""; }
  c.print("POST " + path + " HTTP/1.1\r\n"
          "Host: " + String(MATRIX_HOST) + "\r\n"
          "Content-Type: application/json\r\n"
          "Content-Length: " + String(body.length()) + "\r\n"
          "Connection: close\r\n\r\n" + body);
  return readResponse(c, 8000);
}

// ═══════════════════════════════════════════
//  MATRIX LOGIN
// ═══════════════════════════════════════════
bool matrixLogin() {
  String lp = String(MATRIX_USER);
  if (lp.startsWith("@")) { int c = lp.indexOf(':'); if (c > 0) lp = lp.substring(1, c); }
  Serial.println("[LOGIN] Localpart: " + lp);

  String resp = httpsPOST("/_matrix/client/v3/login",
    "{\"type\":\"m.login.password\","
    "\"identifier\":{\"type\":\"m.id.user\",\"user\":\"" + lp + "\"},"
    "\"password\":\"" + String(MATRIX_PASS) + "\"}");

  if (!resp.length()) { Serial.println("[LOGIN] Leere Antwort!"); return false; }

  JsonDocument doc;
  if (deserializeJson(doc, resp)) { Serial.println("[LOGIN] JSON-Fehler"); return false; }
  if (doc["errcode"].is<const char*>()) {
    Serial.println("[LOGIN] Fehler: " + doc["errcode"].as<String>() + " – " + doc["error"].as<String>());
    return false;
  }
  if (!doc["access_token"].is<const char*>()) { Serial.println("[LOGIN] Kein Token!"); return false; }

  accessToken = doc["access_token"].as<String>();
  Serial.println("[LOGIN] OK!");
  return true;
}

// ═══════════════════════════════════════════
//  MATRIX SYNC  (String-Parser, kein ArduinoJson)
// ═══════════════════════════════════════════

// Gibt den Wert von "key":"value" zurück
String jStr(const String& s, const String& key) {
  String needle = "\"" + key + "\":\"";
  int i = s.indexOf(needle);
  if (i < 0) return "";
  i += needle.length();
  int e = s.indexOf('"', i);
  return (e < 0) ? "" : s.substring(i, e);
}

// Raum-ID URL-kodieren (! → %21, : → %3A)
String encRoom(const String& id) {
  String o;
  for (char c : id) {
    if (c == '!') o += "%21"; else if (c == ':') o += "%3A"; else o += c;
  }
  return o;
}

// Sync-Filter-String (URL-encoded JSON)
String syncFilter(int limit) {
  return "%7B%22room%22%3A%7B%22rooms%22%3A%5B%22" + encRoom(String(MATRIX_ROOM)) +
         "%22%5D%2C%22timeline%22%3A%7B%22limit%22%3A" + String(limit) +
         "%7D%2C%22state%22%3A%7B%22limit%22%3A0%7D%7D%2C"
         "%22presence%22%3A%7B%22not_types%22%3A%5B%22*%22%5D%7D%2C"
         "%22account_data%22%3A%7B%22not_types%22%3A%5B%22*%22%5D%7D%7D";
}

void parseSyncBody(const String& resp) {
  // next_batch
  String nb = jStr(resp, "next_batch");
  if (nb.length()) {
    nextBatch = nb;
    Serial.println("[SYNC] next_batch: " + nb.substring(0, 40));
  }

  // rooms-Block debuggen
  int rp = resp.indexOf("\"rooms\":");
  if (rp < 0) {
    Serial.println("[SYNC] Kein rooms-Block (Raum leer / keine neuen Events)");
    return;
  }
  // Erste 250 Zeichen des rooms-Blocks ausgeben
  Serial.println("[SYNC] rooms→ " + resp.substring(rp, min((int)resp.length(), rp + 250)));

  // m.room.message Events durchsuchen
  int from = 0, found = 0;
  while (true) {
    int tp = resp.indexOf("\"m.room.message\"", from);
    if (tp < 0) break;
    from = tp + 16;
    found++;

    String chunk  = resp.substring(max(0, tp - 400), min((int)resp.length(), tp + 400));
    String sender = jStr(chunk, "sender");
    String body   = jStr(chunk, "body");

    if (sender.length() && body.length()) {
      if (sender.startsWith("@")) { int co = sender.indexOf(':'); if (co > 0) sender = sender.substring(1, co); }
      Serial.println("[MSG] " + sender + ": " + body);
      pushMsg(sender, body);
    }
  }
  Serial.printf("[SYNC] %d m.room.message Event(s)\n", found);
}

void matrixInitialSync() {
  Serial.println("[SYNC] Initial (Timeout 30s)...");
  String resp = httpsGET("/_matrix/client/v3/sync?filter=" + syncFilter(15), TO_INIT);
  if (!resp.length()) { Serial.println("[SYNC] Keine Antwort!"); return; }
  parseSyncBody(resp);
  scrollPos = max(0, msgCount - VISIBLE);
  drawChat();
}

void matrixSync() {
  if (!nextBatch.length()) return;
  String resp = httpsGET("/_matrix/client/v3/sync?since=" + nextBatch +
                         "&timeout=8000&filter=" + syncFilter(20), TO_POLL);
  if (!resp.length()) return;
  parseSyncBody(resp);
  drawChat();
}

// ═══════════════════════════════════════════
//  ROTARY ENCODER
// ═══════════════════════════════════════════
void handleEncoder() {
  int clk = digitalRead(ENC_CLK);
  if (clk != lastCLK && clk == LOW)
    encDelta += (digitalRead(ENC_DT) != clk) ? 1 : -1;
  lastCLK = clk;

  if (digitalRead(ENC_SW) == LOW) {
    if (!btnHeld && millis() - lastBtnMs > 300) {
      btnHeld   = true; lastBtnMs = millis();
      scrollPos = max(0, msgCount - VISIBLE);
      drawChat();
    }
  } else { btnHeld = false; }

  if (encDelta != 0) {
    scrollPos = constrain(scrollPos - encDelta, 0, max(0, msgCount - VISIBLE));
    encDelta  = 0;
    drawChat();
  }
}

// ═══════════════════════════════════════════
//  SETUP & LOOP
// ═══════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED nicht gefunden!"); for (;;) delay(1000);
  }
  display.clearDisplay(); display.display();

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT);
  lastCLK = digitalRead(ENC_CLK);

  // WLAN
  showStatus("WLAN...", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t > 20000) { showStatus("WLAN Fehler!"); delay(3000); ESP.restart(); }
    handleEncoder(); delay(200);
  }
  Serial.println("[WIFI] " + WiFi.localIP().toString());
  showStatus("WLAN OK", WiFi.localIP().toString()); delay(700);

  // Login
  showStatus("Matrix Login...", MATRIX_HOST);
  if (!matrixLogin()) { showStatus("Login Fehler!", "Prüfe Zugangsdaten"); for (;;) delay(5000); }
  showStatus("Login OK!", String(MATRIX_USER)); delay(700);

  // Erster Sync
  showStatus("Sync...", "Lade Nachrichten...");
  matrixInitialSync();
}

void loop() {
  handleEncoder();
  if (millis() - lastSyncMs > SYNC_MS) { lastSyncMs = millis(); matrixSync(); }
  delay(5);
}
