#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <time.h>
#include <user_interface.h>
#include "trust_anchor.h"
#include "title_font.h"
#if __has_include("private_setup.h")
#include "private_setup.h"
#endif

// SmallTV-Ultra ESP-12F: hardware SPI SCLK=14/MOSI=13, CS=15,
// DC=0, RESET=2, active-low backlight=5. No full-screen framebuffer.
Adafruit_ST7789 tft(15, 0, 2);
U8G2_FOR_ADAFRUIT_GFX font;
ESP8266WebServer web(80);
BearSSL::Session tlsSession;

constexpr uint16_t BG = 0x0841, MUTED = 0x9CF3, WHITE = 0xEF7D;
constexpr uint16_t PURPLE = 0x795B, RED = 0xF924;
constexpr uint8_t ROWS = 6;
const char* API_URL = "https://napotim.duckdns.org/api/device/v1/today";
String token, webPassword;
uint8_t brightness = 22;
uint8_t rotation = 2;
bool configured = false, apActive = false, showSetup = true;
uint32_t nextPoll = 0, pollInterval = 60000, setupStarted = 0, lastFooterMinute = UINT32_MAX;
time_t lastSuccess = 0;
int utcOffset = 0;
String date, failure, setupAddress;
int lastHttpStatus = 0, lastTlsError = 0;
int responseExpected = 0, responseBytes = 0;
String dataError;
int todayTotal = 0, overdueTotal = 0, hiddenCount = 0;
uint8_t taskCount = 0;
uint32_t pollCount = 0, previousStage = 0, previousHeap = 0;
struct BootTrace { uint32_t magic, boots, stage, heap; } bootTrace;

void markStage(uint32_t stage) {
  bootTrace.stage = stage; bootTrace.heap = ESP.getFreeHeap();
  ESP.rtcUserMemoryWrite(96, reinterpret_cast<uint32_t*>(&bootTrace), sizeof(bootTrace));
}

struct Task { String title, category, dueDate, dueTime; uint16_t color; bool high, overdue, today; };
Task tasks[ROWS];

String randomPassword() {
  uint8_t bytes[8]; os_get_random(bytes, sizeof(bytes));
  char out[17];
  for (uint8_t i = 0; i < 8; ++i) snprintf(out + i * 2, 3, "%02x", bytes[i]);
  return String(out);
}

void saveConfig() {
  DynamicJsonDocument doc(1536);
  doc["token"] = token; doc["web_password"] = webPassword;
  doc["brightness"] = brightness; doc["rotation"] = rotation;
  File file = LittleFS.open("/config.tmp", "w");
  if (!file || serializeJson(doc, file) == 0) return;
  file.close();
  LittleFS.rename("/config.tmp", "/config.json");
}

void loadConfig() {
  // First migration from the stock firmware may require a clean filesystem.
  if (!LittleFS.begin()) { LittleFS.format(); LittleFS.begin(); }
  DynamicJsonDocument doc(1536);
  File file = LittleFS.open("/config.json", "r");
  if (file && !deserializeJson(doc, file)) {
    token = doc["token"] | ""; webPassword = doc["web_password"] | "";
    brightness = constrain(doc["brightness"] | 22, 1, 100);
    rotation = constrain(doc["rotation"] | 2, 0, 3);
  }
  if (webPassword.length() != 16) {
#ifdef PRIVATE_SETUP_PASSWORD
    webPassword = PRIVATE_SETUP_PASSWORD;
#else
    webPassword = randomPassword();
#endif
    saveConfig();
  }
  configured = token.startsWith("nptd_") && token.length() == 48;
}

void setBrightness() { analogWrite(5, 1023 - brightness * 1023 / 100); }

String clip(String value, int width) {
  if (font.getUTF8Width(value.c_str()) <= width) return value;
  while (value.length()) {
    size_t n = value.length() - 1;
    while (n > 0 && (static_cast<uint8_t>(value[n]) & 0xC0) == 0x80) --n;
    value.remove(n);
    if (font.getUTF8Width((value + "...").c_str()) <= width) return value + "...";
  }
  return "";
}

String supportedText(const String& value) {
  String display = value;
  display.replace("…", "..."); display.replace("’", "'"); display.replace("‘", "'");
  display.replace("–", "-"); display.replace("—", "-"); display.replace("·", "|");
  display.replace("«", "\""); display.replace("»", "\""); display.replace("“", "\""); display.replace("”", "\"");
  String supported;
  for (size_t i=0; i<display.length();) {
    size_t end=i+1;
    while (end<display.length() && (static_cast<uint8_t>(display[end]) & 0xC0)==0x80) ++end;
    String glyph=display.substring(i,end);
    supported += font.getUTF8Width(glyph.c_str()) > 0 ? glyph : "?";
    i=end;
  }
  return supported;
}

void textAt(int x, int y, const String& value, uint16_t color, int width = 230) {
  font.setForegroundColor(color); font.setBackgroundColor(BG);
  font.setCursor(x, y); font.print(clip(supportedText(value), width));
}

const TitleGlyph* titleGlyph(uint16_t codepoint) {
  int left = 0, right = TITLE_GLYPH_COUNT - 1;
  while (left <= right) {
    int mid = (left + right) / 2;
    uint16_t current = pgm_read_word(&TITLE_GLYPHS[mid].codepoint);
    if (current == codepoint) return &TITLE_GLYPHS[mid];
    if (current < codepoint) left = mid + 1; else right = mid - 1;
  }
  return &TITLE_GLYPHS[TITLE_FALLBACK_INDEX];
}

uint16_t nextTitleCodepoint(const String& value, size_t& index) {
  uint8_t first = value[index++];
  if (first < 0x80) return first;
  int remaining = (first & 0xE0) == 0xC0 ? 1 : (first & 0xF0) == 0xE0 ? 2 : (first & 0xF8) == 0xF0 ? 3 : 0;
  if (!remaining) return '?';
  uint32_t codepoint = first & (0x7F >> remaining);
  while (remaining--) {
    if (index >= value.length() || (static_cast<uint8_t>(value[index]) & 0xC0) != 0x80) return '?';
    codepoint = (codepoint << 6) | (value[index++] & 0x3F);
  }
  return codepoint <= 0xFFFF ? codepoint : '?';
}

void titleAt(int x, int baseline, const String& value, uint16_t color) {
  // Native larger size of the original Misc Fixed family: clean one-pixel
  // strokes, 10px capitals and 7px lowercase, without fractional resampling.
  String display = supportedText(value);
  uint16_t codepoints[31];
  uint8_t count = 0;
  size_t index = 0;
  while (index < display.length() && count < 31) codepoints[count++] = nextTitleCodepoint(display, index);
  if (index < display.length() || count > 30) {
    count = 27;
    for (int i = 0; i < 3; ++i) codepoints[count++] = '.';
  }
  tft.startWrite();
  for (uint8_t i = 0; i < count; ++i) {
    const TitleGlyph* glyph = titleGlyph(codepoints[i]);
    for (int row = 0; row < 14; ++row) {
      uint8_t pixels = pgm_read_byte(&glyph->rows[row]);
      int run = -1;
      for (int column = 0; column <= 7; ++column) {
        bool on = column < 7 && (pixels & (0x80 >> column));
        if (on && run < 0) run = column;
        if (!on && run >= 0) {
          tft.writeFastHLine(x + i * 7 + run, baseline - 12 + row, column - run, color);
          run = -1;
        }
      }
    }
  }
  tft.endWrite();
  yield();
}

uint16_t categoryColor(const String& hex) {
  if (hex.length() != 7 || hex[0] != '#') return MUTED;
  uint32_t color = strtoul(hex.substring(1).c_str(), nullptr, 16);
  uint8_t r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
  int low = min(r, min(g, b)), high = max(r, max(g, b));
  // Remove the pastel white component while keeping category hue. Near-neutral
  // categories stay neutral; keep dark dots visible on the black LCD background.
  if (high - low >= 24) {
    int peak = max(high, 192), range = high - low;
    r = (r - low) * peak / range;
    g = (g - low) * peak / range;
    b = (b - low) * peak / range;
  }
  return tft.color565(r, g, b);
}

String dayLabel(const Task& task) {
  if (task.today) return "Сьогодні";
  return task.dueDate.substring(8, 10) + "." + task.dueDate.substring(5, 7);
}

void drawFooter() {
  tft.fillRect(0, 213, 240, 27, BG);
  font.setFont(u8g2_font_6x12_t_cyrillic);
  if (!lastSuccess) { textAt(5, 227, failure.length() ? failure : "Очікування даних…", MUTED); return; }
  time_t local = lastSuccess + utcOffset;
  struct tm clock; gmtime_r(&local, &clock);
  char stamp[24];
  time_t now = time(nullptr);
  bool oldDay = (now + utcOffset) / 86400 != local / 86400;
  strftime(stamp, sizeof(stamp), oldDay ? "%d.%m %H:%M" : "%H:%M", &clock);
  textAt(5, 225, String("Оновлено ") + stamp, MUTED);
  if (failure.length()) {
    int age = now > lastSuccess ? (now - lastSuccess) / 60 : 0;
    textAt(5, 238, failure + " · " + age + " хв тому", RED);
  } else if (hiddenCount) textAt(5, 238, String("Ще ") + hiddenCount + " задач", MUTED);
}

void drawRow(uint8_t i) {
  int y = 43 + i * 28;
  tft.fillRect(0, y, 240, 28, BG);
  if (i >= taskCount) return;
  const Task& task = tasks[i];
  tft.drawRect(5, y + 4, 7, 7, MUTED); // task type icon
  if (task.high) tft.fillRect(15, y + 3, 2, 10, RED);
  font.setFont(u8g2_font_6x13_t_cyrillic);
  titleAt(21, y + 13, task.title, task.overdue ? RED : WHITE);
  font.setFont(u8g2_font_6x12_t_cyrillic);
  tft.fillCircle(8, y + 22, 2, task.color);
  String due = dayLabel(task);
  if (task.dueTime.length()) due += " " + task.dueTime;
  int dueX = 235 - font.getUTF8Width(due.c_str());
  textAt(15, y + 26, task.category, MUTED, dueX - 23);
  textAt(dueX, y + 26, due, task.overdue ? RED : MUTED, 235 - dueX);
}

void drawHeader() {
  tft.fillRect(0, 0, 240, 40, BG);
  font.setFont(u8g2_font_6x13_t_cyrillic);
  textAt(5, 15, "НАПОТІМ", PURPLE);
  if (date.length() == 10) textAt(169, 15, date.substring(8) + "." + date.substring(5, 7), MUTED, 66);
  font.setFont(u8g2_font_6x12_t_cyrillic);
  textAt(5, 32, String("Сьогодні: ") + todayTotal + "   Прострочено: " + overdueTotal, MUTED);
  tft.drawFastHLine(5, 38, 230, 0x2945);
}

void drawSetup() {
  bool connected = WiFi.status() == WL_CONNECTED;
  setupAddress = connected ? WiFi.localIP().toString() : "192.168.4.1";
  tft.fillScreen(BG);
  if (configured) {
    drawHeader();
    font.setFont(u8g2_font_6x13_t_cyrillic);
    textAt(8, 83, "Отримання задач…", WHITE);
    textAt(8, 110, connected ? "Wi-Fi підключено" : "Очікування Wi-Fi", MUTED);
    textAt(8, 133, setupAddress, MUTED);
    drawFooter();
    return;
  }
  font.setFont(u8g2_font_6x13_t_cyrillic);
  textAt(8, 22, "НАПОТІМ · налаштування", PURPLE);
  textAt(8, 55, connected ? "Відкрийте в браузері:" : "Wi-Fi: Napotim-Setup", WHITE);
  textAt(8, 77, setupAddress, WHITE);
  textAt(8, 110, "Користувач: admin", MUTED);
  textAt(8, 131, "Пароль:", MUTED);
  textAt(8, 151, webPassword, WHITE);
  textAt(8, 183, "Створіть ключ у Напотім:", MUTED);
  textAt(8, 204, "Налаштування > Пристрої", MUTED);
}

bool taskEqual(const Task& a, const Task& b) {
  return a.title == b.title && a.category == b.category && a.dueDate == b.dueDate &&
    a.dueTime == b.dueTime && a.color == b.color && a.high == b.high && a.overdue == b.overdue && a.today == b.today;
}

bool applySnapshot(const String& payload) {
  markStage(6);
  DynamicJsonDocument doc(6144);
  DeserializationError parsed = deserializeJson(doc, payload);
  if (parsed) { dataError = parsed.c_str(); return false; }
  JsonArray items = doc["items"].as<JsonArray>();
  if (doc["schema_version"] != 1 || !doc["generated_at"].is<long>() ||
      !doc["date"].is<const char*>() || !doc["utc_offset_seconds"].is<int>() ||
      items.isNull() || items.size() > ROWS || !doc["total"].is<int>() || !doc["hidden_count"].is<int>() ||
      !doc["today_total"].is<int>() || !doc["overdue_total"].is<int>()) { dataError = "snapshot_schema"; return false; }
  Task fresh[ROWS];
  uint8_t count = 0;
  for (JsonObject item : items) {
    if (!item["title"].is<const char*>() || !item["category"].is<const char*>() ||
        !item["due_date"].is<const char*>() || item["type"] != "task" ||
        !item["overdue"].is<bool>() || !item["today"].is<bool>()) { dataError = "task_schema"; return false; }
    Task& out = fresh[count++];
    out.title = item["title"].as<String>(); out.category = item["category"].as<String>();
    out.dueDate = item["due_date"].as<String>(); out.dueTime = item["due_time"] | "";
    out.color = categoryColor(item["category_color"] | "#71717a");
    out.high = item["priority"] == "high"; out.overdue = item["overdue"]; out.today = item["today"];
  }
  String newDate = doc["date"].as<String>();
  bool first = !lastSuccess || showSetup;
  bool newHeader = first || newDate != date || todayTotal != doc["today_total"].as<int>() || overdueTotal != doc["overdue_total"].as<int>();
  date = newDate; todayTotal = doc["today_total"]; overdueTotal = doc["overdue_total"];
  hiddenCount = doc["hidden_count"]; utcOffset = doc["utc_offset_seconds"];
  if (first) tft.fillScreen(BG);
  markStage(7);
  showSetup = false;
  if (newHeader) drawHeader();
  uint8_t previousCount = taskCount;
  if (previousCount == 0 && count > 0 && !first) { tft.fillRect(0, 43, 240, 168, BG); first = true; }
  taskCount = count;
  for (uint8_t i = 0; i < ROWS; ++i) {
    bool changed = first || (i < count && (i >= previousCount || !taskEqual(tasks[i], fresh[i]))) || (i >= count && i < previousCount);
    tasks[i] = fresh[i];
    if (changed) drawRow(i);
  }
  if (!count) {
    font.setFont(u8g2_font_6x13_t_cyrillic);
    textAt(15, 90, "На сьогодні все виконано", WHITE);
  }
  // Advance the success stamp only after validation and rendering.
  lastSuccess = time(nullptr); failure = ""; drawFooter();
  return true;
}

void poll() {
  dataError = ""; responseExpected = 0; responseBytes = 0;
  ++pollCount; markStage(1);
  if (WiFi.status() != WL_CONNECTED) { failure = "Немає Wi-Fi"; drawFooter(); return; }
  if (time(nullptr) < 1760000000) { failure = "Очікування часу"; drawFooter(); return; }
  int status;
  String payload;
  {
    markStage(2);
    BearSSL::WiFiClientSecure client;
    markStage(3);
    BearSSL::X509List ca(TRUST_ANCHOR);
    client.setTrustAnchors(&ca);
    client.setSSLVersion(BR_TLS12, BR_TLS12);
    client.setBufferSizes(16384, 512); // Safe default receive size; no assumed MFLN.
    client.setSession(&tlsSession);
    client.setTimeout(15000);
    HTTPClient http;
    http.setTimeout(15000); http.useHTTP10(true);
    if (!http.begin(client, API_URL)) { failure = "Помилка HTTPS"; drawFooter(); return; }
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("Accept-Encoding", "identity");
    markStage(4);
    status = http.GET();
    markStage(5);
    lastHttpStatus = status;
    lastTlsError = client.getLastSSLError();
    responseExpected = http.getSize();
    if (status == 200 && responseExpected > 0 && responseExpected <= 6144) {
      if (payload.reserve(responseExpected)) {
        // getString()/sendSize() can stop between BearSSL records when TCP is
        // already closing. Drain the announced body without using connected()
        // as EOF: encrypted bytes may still be buffered after the TCP FIN.
        uint8_t chunk[256];
        uint32_t deadline = millis() + 15000;
        while (payload.length() < static_cast<unsigned>(responseExpected) &&
               static_cast<int32_t>(deadline - millis()) > 0) {
          int count = client.read(chunk, min<unsigned>(sizeof(chunk), responseExpected - payload.length()));
          if (count > 0) {
            if (!payload.concat(reinterpret_cast<const char*>(chunk), count)) break;
          } else delay(1);
        }
        responseBytes = payload.length();
        if (responseBytes != responseExpected) { dataError = "incomplete_body"; payload = ""; }
      } else dataError = "body_allocation";
    } else if (status == 200) dataError = "empty_or_unbounded_body";
    http.end();
  } // Free TLS buffers before parsing the bounded JSON document.
  if (status == 200 && payload.length() && applySnapshot(payload)) { pollInterval = 60000; markStage(0); return; }
  if (status == 401 || status == 403) {
    token = ""; configured = false; taskCount = 0; lastSuccess = 0;
    for (auto& task : tasks) task = Task();
    failure = "Ключ відкликано або недійсний";
    saveConfig(); showSetup = true; drawSetup(); markStage(0); return;
  }
  else if (status == 404) { failure = "API ще не встановлено"; pollInterval = 60000; }
  else { failure = status == 200 ? "Некоректні дані" : "Немає зв’язку"; pollInterval = min<uint32_t>(pollInterval * 2, 300000); }
  drawFooter();
  markStage(0);
}

bool authenticated() {
  if (web.authenticateDigest("admin", web.credentialHash("admin", "Napotim Cube", webPassword.c_str()))) return true;
  web.requestAuthentication(DIGEST_AUTH, "Napotim Cube"); return false;
}

const char SETTINGS_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="uk"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Напотім · кубик</title><style>body{background:#101014;color:#eee;font:16px system-ui;max-width:520px;margin:40px auto;padding:20px}label{display:block;margin:20px 0}input,button{font:inherit;padding:10px;width:100%;box-sizing:border-box;background:#202026;color:#eee;border:1px solid #555;border-radius:8px}button{background:#7046b5}a{color:#bb9cec}</style><h1>Напотім · кубик</h1><p>Один нерухомий екран із найважливішими задачами. Оновлення раз на хвилину.</p><form method="post" action="/config"><label>Ключ із Напотім → Налаштування → Пристрої<input name="token" type="password" autocomplete="off" maxlength="48" placeholder="Залиште порожнім, щоб зберегти поточний"></label><label>Яскравість: 1–100<input name="brightness" type="number" min="1" max="100" value="22"></label><label>Поворот: 0–3<input name="rotation" type="number" min="0" max="3" value="0"></label><button>Зберегти</button></form><hr><h2>Wi-Fi</h2><form method="post" action="/wifi"><label>Назва мережі<input name="ssid" required maxlength="32"></label><label>Пароль<input name="password" type="password" autocomplete="off" minlength="8" maxlength="63" required></label><button>Підключити</button></form><p><a href="/update">Оновити прошивку</a></p></html>)HTML";

void configureWeb() {
  static bool uploadAllowed = false;
  static bool uploadStarted = false;
  web.on("/health", HTTP_GET, []() { web.send(200, "application/json", "{\"firmware\":\"napotim-cube\",\"version\":\"1.0.9\"}"); });
  web.on("/status", HTTP_GET, []() {
    if (!authenticated()) return;
    DynamicJsonDocument doc(1536);
    doc["configured"]=configured; doc["wifi_connected"]=WiFi.status()==WL_CONNECTED;
    doc["free_heap"]=ESP.getFreeHeap(); doc["largest_heap_block"]=ESP.getMaxFreeBlockSize();
    doc["http_status"]=lastHttpStatus; doc["tls_error"]=lastTlsError;
    doc["response_expected"]=responseExpected; doc["response_bytes"]=responseBytes; doc["data_error"]=dataError;
    doc["last_success"]=lastSuccess; doc["task_count"]=taskCount;
    doc["brightness"]=brightness; doc["rotation"]=rotation;
    doc["error"]=failure; doc["uptime_seconds"]=millis()/1000;
    doc["station_ip"]=WiFi.localIP().toString(); doc["wifi_status"]=static_cast<int>(WiFi.status());
    doc["reset_info"]=ESP.getResetInfo(); doc["boot_count"]=bootTrace.boots;
    doc["previous_stage"]=previousStage; doc["previous_heap"]=previousHeap;
    doc["poll_count"]=pollCount; doc["clock_epoch"]=time(nullptr);
    String out; serializeJson(doc,out);
    web.sendHeader("Cache-Control","no-store"); web.send(200,"application/json",out);
  });
  web.on("/", HTTP_GET, []() {
    if (!authenticated()) return;
    String page = FPSTR(SETTINGS_PAGE);
    page.replace("name=\"brightness\" type=\"number\" min=\"1\" max=\"100\" value=\"22\"", "name=\"brightness\" type=\"number\" min=\"1\" max=\"100\" value=\"" + String(brightness) + "\"");
    page.replace("name=\"rotation\" type=\"number\" min=\"0\" max=\"3\" value=\"0\"", "name=\"rotation\" type=\"number\" min=\"0\" max=\"3\" value=\"" + String(rotation) + "\"");
    web.sendHeader("Cache-Control", "no-store"); web.send(200, "text/html; charset=utf-8", page);
  });
  web.on("/config", HTTP_POST, []() {
    if (!authenticated()) return;
    String supplied = web.arg("token"); supplied.trim();
    bool valid = supplied.length() == 48 && supplied.startsWith("nptd_");
    for (uint8_t i = 5; valid && i < supplied.length(); ++i) valid = isalnum(supplied[i]) || supplied[i] == '_' || supplied[i] == '-';
    if (supplied.length() && !valid) { web.send(400, "text/plain; charset=utf-8", "Некоректний ключ пристрою."); return; }
    if (supplied.length()) token = supplied;
    uint8_t newRotation = web.hasArg("rotation") ? constrain(web.arg("rotation").toInt(), 0, 3) : rotation;
    if (web.hasArg("brightness")) brightness = constrain(web.arg("brightness").toInt(), 1, 100);
    bool rotate = rotation != newRotation; rotation = newRotation;
    configured = token.length() == 48; saveConfig(); setBrightness();
    failure = "Очікування синхронізації";
    if (rotate) {
      tft.setRotation(rotation);
      if (showSetup) drawSetup();
      else { tft.fillScreen(BG); drawHeader(); for (uint8_t i=0;i<ROWS;++i) drawRow(i); drawFooter(); }
    }
    else if (showSetup) drawSetup();
    nextPoll = millis() + 1500; pollInterval = 60000;
    web.sendHeader("Cache-Control", "no-store"); web.send(200, "text/plain; charset=utf-8", "Збережено. Екран оновиться після синхронізації.");
  });
  web.on("/wifi", HTTP_POST, []() {
    if (!authenticated()) return;
    String ssid = web.arg("ssid"), password = web.arg("password");
    if (!ssid.length() || ssid.length()>32 || password.length()<8 || password.length()>63) { web.send(400,"text/plain","Invalid WiFi settings"); return; }
    web.send(200,"text/plain; charset=utf-8","Підключення до Wi-Fi…");
    WiFi.persistent(true); WiFi.begin(ssid.c_str(), password.c_str()); WiFi.persistent(false);
  });
  web.on("/update", HTTP_GET, []() {
    if (!authenticated()) return;
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "text/html; charset=utf-8", "<meta charset=utf-8><form method=post enctype=multipart/form-data><input type=file name=firmware accept=.bin required><button>Оновити прошивку</button></form>");
  });
  web.on("/update", HTTP_POST, []() {
    if (!authenticated()) return;
    bool success = uploadAllowed && uploadStarted && !Update.hasError();
    web.send( success ? 200 : 400, "text/plain", success ? "Update complete. Rebooting." : "Update failed. Existing firmware retained.");
    if (success) { delay(250); ESP.restart(); }
  }, []() {
    HTTPUpload& upload = web.upload();
    if (upload.status == UPLOAD_FILE_START) {
      uploadAllowed = web.authenticateDigest("admin", web.credentialHash("admin", "Napotim Cube", webPassword.c_str()));
      uploadStarted = false;
      if (!uploadAllowed) return;
      Update.runAsync(true);
      uploadStarted = Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
    } else if (upload.status == UPLOAD_FILE_WRITE && uploadAllowed && uploadStarted) {
      Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END && uploadAllowed && uploadStarted) {
      if (!Update.end(true)) uploadStarted = false;
    } else if (upload.status == UPLOAD_FILE_ABORTED && uploadStarted) {
      Update.end(); uploadStarted = false;
    }
    yield();
  });
  web.begin();
}

void setup() {
  Serial.begin(115200);
  ESP.rtcUserMemoryRead(96, reinterpret_cast<uint32_t*>(&bootTrace), sizeof(bootTrace));
  if (bootTrace.magic != 0x4e505432) bootTrace = {0x4e505432, 0, 0, 0};
  previousStage = bootTrace.stage; previousHeap = bootTrace.heap;
  ++bootTrace.boots; markStage(0);
  pinMode(5, OUTPUT); analogWriteRange(1023);
  tft.init(240, 240, SPI_MODE3); font.begin(tft); font.setFontMode(1); font.setFontDirection(0);
  loadConfig(); tft.setRotation(rotation); setBrightness(); tft.fillScreen(BG);
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin();
  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
  configureWeb(); setupStarted = millis(); nextPoll = millis() + 3000;
  font.setFont(u8g2_font_6x13_t_cyrillic); textAt(8, 25, "НАПОТІМ · підключення…", PURPLE);
}

void loop() {
  web.handleClient();
  if (!apActive && WiFi.status() != WL_CONNECTED && millis() - setupStarted > 30000) {
    WiFi.mode(WIFI_AP_STA); WiFi.softAP("Napotim-Setup", webPassword.c_str()); apActive = true;
    if (showSetup) drawSetup();
  }
  if (showSetup && (WiFi.status() == WL_CONNECTED || apActive)) {
    String address = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "192.168.4.1";
    if (address != setupAddress) drawSetup();
  }
  if (apActive && WiFi.status() == WL_CONNECTED && configured) { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); apActive = false; }
  if (configured && static_cast<int32_t>(millis() - nextPoll) >= 0) { poll(); nextPoll = millis() + pollInterval; }
  uint32_t minute = millis() / 60000;
  if (lastSuccess && minute != lastFooterMinute) { drawFooter(); lastFooterMinute = minute; }
  yield();
}
