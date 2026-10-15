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
#include <memory>
#include <new>
#include <user_interface.h>
#include "trust_anchor.h"
#include "title_font.h"
#include "display_settings.h"
#include "night_mode.h"
#include "poll_schedule.h"
#include "snapshot_reader.h"
#include "update_status.h"
#include "theme_display.h"
#include "api_origin.h"
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
// A soft red edge (40% RED over BG) gives overdue titles a little more weight.
// The original glyph pixels retain RED; white titles use no edge.
constexpr uint16_t RED_TITLE_EDGE = 0x68A2;
constexpr uint8_t ROWS = 6;
const char* DEFAULT_ORIGIN = "https://napotim.duckdns.org";
String apiOrigin;
ThemeDisplay themeDisplay;
bool legacyApi=false;
bool sourceConfirmationNeeded=false;
uint32_t nextV2Probe=0;
String frameError;
uint32_t frameDownloads=0;
String token, webPassword;
DisplaySettings displaySettings;
NightMode nightMode;
uint8_t& brightness = displaySettings.brightness;
uint8_t& rotation = displaySettings.rotation;
bool settingsRemote = false, settingsLocal = false, timezoneKnown = false;
OffsetTransition timezoneChanges[4];
uint8_t timezoneChangeCount = 0;
uint16_t effectiveBrightness = 2200;
int lastBrightnessPwm = -1;
bool configured = false, apActive = false, showSetup = true;
PollSchedule pollSchedule;
uint32_t setupStarted = 0, lastFooterMinute = UINT32_MAX;
time_t lastSuccess = 0;
bool refreshFailed = false;
int utcOffset = 0;
String date, failure, setupAddress;
int lastHttpStatus = 0, lastTlsError = 0;
int responseExpected = 0, responseBytes = 0;
String dataError;
int todayTotal = 0, overdueTotal = 0, hiddenCount = 0;
uint8_t taskCount = 0;
uint32_t pollCount = 0, previousStage = 0, previousHeap = 0;
uint32_t firstWifiReadyMs = UINT32_MAX, firstClockReadyMs = UINT32_MAX;
uint32_t firstRequestMs = UINT32_MAX, firstSuccessMs = UINT32_MAX;
uint32_t pollStartedMs = 0, lastPollMs = 0, lastHttpMs = 0, lastBodyMs = 0;
uint32_t lastCleanupMs = 0, lastRenderMs = 0;
bool pollInProgress = false;
struct BootTrace { uint32_t magic, boots, stage, heap; } bootTrace;

void markStage(uint32_t stage) {
  if (stage == 1) { pollStartedMs = millis(); pollInProgress = true; }
  if (stage == 0 && pollInProgress) { lastPollMs = millis() - pollStartedMs; pollInProgress = false; }
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

String scheduleTime(uint16_t minute) {
  char out[6]; snprintf(out, sizeof(out), "%02u:%02u", (minute / 60) % 24, minute % 60);
  return String(out);
}

void writeDisplaySettings(JsonObject out, const DisplaySettings& settings) {
  out["mode"] = settings.scheduled ? "scheduled" : "manual";
  out["brightness"] = settings.brightness; out["rotation"] = settings.rotation;
  out["day_brightness"] = settings.dayBrightness; out["night_brightness"] = settings.nightBrightness;
  out["day_start"] = scheduleTime(settings.dayStart); out["night_start"] = scheduleTime(settings.nightStart);
  out["sleep_enabled"] = settings.sleepEnabled; out["sleep_start"] = scheduleTime(settings.sleepStart);
}

bool parseScheduleTime(JsonVariantConst value, uint16_t& minute) {
  if (!value.is<const char*>()) return false;
  const char* text = value.as<const char*>();
  if (strlen(text) != 5 || text[2] != ':' || !isdigit(text[0]) || !isdigit(text[1]) ||
      !isdigit(text[3]) || !isdigit(text[4])) return false;
  int hour = (text[0] - '0') * 10 + text[1] - '0', minutes = (text[3] - '0') * 10 + text[4] - '0';
  if (hour > 23 || minutes > 59) return false;
  minute = hour * 60 + minutes; return true;
}

bool parseDisplaySettings(JsonVariantConst value, DisplaySettings& out) {
  if (!value.is<JsonObjectConst>() || !value["mode"].is<const char*>() ||
      (value["mode"] != "manual" && value["mode"] != "scheduled")) return false;
  for (const char* key : {"brightness", "day_brightness", "night_brightness"}) {
    if (!value[key].is<int>() || value[key].as<int>() < 1 || value[key].as<int>() > 100) return false;
  }
  if (!value["rotation"].is<int>() || value["rotation"].as<int>() < 0 || value["rotation"].as<int>() > 3 ||
      !parseScheduleTime(value["day_start"], out.dayStart) || !parseScheduleTime(value["night_start"], out.nightStart)) return false;
  int duration = (out.nightStart - out.dayStart + 1440) % 1440;
  if (value["mode"] == "scheduled" && (duration < 15 || duration > 1425)) return false;
  out.brightness = value["brightness"]; out.rotation = value["rotation"];
  out.dayBrightness = value["day_brightness"]; out.nightBrightness = value["night_brightness"];
  out.scheduled = value["mode"] == "scheduled";
  // Older servers/config files have no sleep fields: preserve compatibility.
  out.sleepEnabled = false; out.sleepStart = 0;
  if (value.as<JsonObjectConst>().containsKey("sleep_enabled")) {
    if (!value["sleep_enabled"].is<bool>() || !parseScheduleTime(value["sleep_start"], out.sleepStart)) return false;
    out.sleepEnabled = value["sleep_enabled"];
  } else if (!value["sleep_start"].isNull()) return false;
  return validSleepSchedule(out);
}

void writeTimezoneChanges(JsonArray out) {
  for (uint8_t i = 0; i < timezoneChangeCount; ++i) {
    JsonObject item = out.createNestedObject();
    item["at"] = timezoneChanges[i].at; item["utc_offset_seconds"] = timezoneChanges[i].offset;
  }
}

bool parseTimezoneChanges(JsonVariantConst value, OffsetTransition* out, uint8_t& count) {
  count = 0;
  if (value.isNull()) return true; // Compatibility with the original API.
  if (!value.is<JsonArrayConst>() || value.size() > 4) return false;
  for (JsonObjectConst item : value.as<JsonArrayConst>()) {
    if (!item["at"].is<long>() || !item["utc_offset_seconds"].is<int>()) return false;
    int32_t at = item["at"], offset = item["utc_offset_seconds"];
    if (at <= 0 || (count && at <= out[count - 1].at) || offset < -86400 || offset > 86400) return false;
    out[count++] = {at, offset};
  }
  return true;
}

void saveConfig() {
  DynamicJsonDocument doc(1536);
  doc["token"] = token; doc["web_password"] = webPassword;
  doc["api_origin"] = apiOrigin;
  doc["source_confirmed"] = !sourceConfirmationNeeded;
  doc["brightness"] = brightness; doc["rotation"] = rotation;
  writeDisplaySettings(doc.createNestedObject("display_settings"), displaySettings);
  doc["settings_remote"] = settingsRemote; doc["timezone_known"] = timezoneKnown;
  doc["settings_local"] = settingsLocal;
  doc["utc_offset_seconds"] = utcOffset;
  writeTimezoneChanges(doc.createNestedArray("timezone_transitions"));
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
    apiOrigin = doc["api_origin"] | "";
    // Require an explicit source after upgrading a v1 configuration.
    sourceConfirmationNeeded=!(doc["source_confirmed"]|false);
    brightness = constrain(doc["brightness"] | 22, 1, 100);
    rotation = constrain(doc["rotation"] | 2, 0, 3);
    DisplaySettings restored = displaySettings;
    if (parseDisplaySettings(doc["display_settings"], restored)) {
      displaySettings = restored; settingsRemote = doc["settings_remote"] | false;
      settingsLocal = doc["settings_local"] | false;
    }
    utcOffset = doc["utc_offset_seconds"] | 0;
    timezoneKnown = (doc["timezone_known"] | false) && utcOffset >= -86400 && utcOffset <= 86400 &&
      parseTimezoneChanges(doc["timezone_transitions"], timezoneChanges, timezoneChangeCount);
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

int localOffset(time_t epoch) { return offsetAt(epoch, utcOffset, timezoneChanges, timezoneChangeCount); }

void setBrightness() {
  time_t now = time(nullptr);
  bool clockReady = timezoneKnown && now >= 1760000000;
  effectiveBrightness = brightnessAt(displaySettings, (now % 86400 + localOffset(now)) % 86400, clockReady);
  if (nightMode.blank()) effectiveBrightness = 0;
  else if (nightMode.notice()) effectiveBrightness = 500;
  int pwm = 1023 - static_cast<uint32_t>(effectiveBrightness) * 1023 / 10000;
  if (pwm != lastBrightnessPwm) {
    if (pwm == 1023) { analogWrite(5, 0); digitalWrite(5, HIGH); }
    else analogWrite(5, pwm);
    lastBrightnessPwm = pwm;
  }
}

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

void titleAt(int x, int baseline, const String& value, uint16_t color, uint16_t edgeColor = BG) {
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
      if (edgeColor != BG) {
        // Add a faint right edge only in empty pixels of the same 7px cell.
        // Keep the native mask, height, baseline and character advance intact.
        uint8_t edge = (pixels >> 1) & ~pixels;
        for (int column = 0; column < 7; ++column) {
          if (edge & (0x80 >> column))
            tft.writePixel(x + i * 7 + column, baseline - 12 + row, edgeColor);
        }
      }
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

bool updateStale() {
  return lastSuccess && (refreshFailed || WiFi.status() != WL_CONNECTED || time(nullptr) < 1760000000);
}

// Misc Fixed metadata uses 6px advances and a 12px cell (baseline at row 10).
constexpr int UPDATE_GLYPH_WIDTH = 6, UPDATE_GLYPH_HEIGHT = 12;
constexpr int UPDATE_STATUS_X = 235 - (9 + 5) * UPDATE_GLYPH_WIDTH;

class UpdateGlyphCanvas : public Adafruit_GFX {
 public:
  UpdateGlyphCanvas() : Adafruit_GFX(UPDATE_GLYPH_WIDTH, UPDATE_GLYPH_HEIGHT) {}
  uint16_t pixels[UPDATE_GLYPH_WIDTH * UPDATE_GLYPH_HEIGHT];
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (x >= 0 && x < UPDATE_GLYPH_WIDTH && y >= 0 && y < UPDATE_GLYPH_HEIGHT)
      pixels[y * UPDATE_GLYPH_WIDTH + x] = color;
  }
};

void drawUpdateGlyph(int x, char glyph, uint16_t color) {
  // Compose background and glyph in 144 bytes of RAM, then replace the cell
  // in one SPI transaction. Never expose a cleared cell on the LCD.
  UpdateGlyphCanvas canvas;
  canvas.fillScreen(BG);
  U8G2_FOR_ADAFRUIT_GFX glyphFont;
  glyphFont.begin(canvas);
  glyphFont.setFont(u8g2_font_6x12_t_cyrillic);
  // setFont() resets the library to opaque mode; never use its unset bg_color.
  glyphFont.setFontMode(1);
  glyphFont.setBackgroundColor(BG);
  glyphFont.setForegroundColor(color);
  glyphFont.drawGlyph(0, 10, glyph);
  tft.drawRGBBitmap(x, 5, canvas.pixels, UPDATE_GLYPH_WIDTH, UPDATE_GLYPH_HEIGHT);
}

void drawUpdateStatus(bool force = false) {
  String stamp;
  if (lastSuccess) {
    time_t local = lastSuccess + localOffset(lastSuccess);
    struct tm clock; gmtime_r(&local, &clock);
    char formatted[6]; strftime(formatted, sizeof(formatted), "%H:%M", &clock);
    stamp = formatted;
  }
  uint16_t color = updateStale() ? RED : MUTED;
  if (themeDisplay.active) {
    themeDisplay.status(tft,stamp,updateStale(),force);
    return;
  }
  static UpdateStatusCache cache;
  UpdateStatusChanges changes = cache.update(stamp.c_str(), color, force);
  if (!changes.label && !changes.glyphs) return;
  font.setFont(u8g2_font_6x12_t_cyrillic);
  const String label = "Оновлено ";
  int labelWidth = font.getUTF8Width(label.c_str());
  if (changes.label) {
    if (stamp.length()) textAt(UPDATE_STATUS_X, 15, label, MUTED, labelWidth);
    else tft.fillRect(UPDATE_STATUS_X, 0, labelWidth, 19, BG);
  }
  for (uint8_t i = 0; i < 5; ++i) {
    if (changes.glyphs & (1 << i))
      drawUpdateGlyph(UPDATE_STATUS_X + labelWidth + i * UPDATE_GLYPH_WIDTH,
        stamp.length() ? stamp[i] : ' ', color);
  }
}

void drawFooter(bool force = false, bool clear = true) {
  if (nightMode.state != NightMode::Awake) return;
  drawUpdateStatus();
  if (themeDisplay.active) return;
  String message;
  int baseline = lastSuccess ? 225 : 227;
  if (!lastSuccess) {
    message = WiFi.status() != WL_CONNECTED ? "Очікування Wi-Fi" :
      time(nullptr) < 1760000000 ? "Очікування часу" :
      failure.length() ? failure : "Очікування даних…";
  } else if (hiddenCount) message = String("Ще ") + hiddenCount + " задач";
  static String paintedMessage;
  static int paintedBaseline = -1;
  font.setFont(u8g2_font_6x12_t_cyrillic);
  if (force || message != paintedMessage || baseline != paintedBaseline) {
    if (clear) tft.fillRect(0, 213, 240, 27, BG);
    textAt(5, baseline, message, MUTED);
    paintedMessage = message; paintedBaseline = baseline;
  }
}

void drawRow(uint8_t i, bool clear = true) {
  if (nightMode.state != NightMode::Awake) return;
  int y = 43 + i * 28;
  if (clear) tft.fillRect(0, y, 240, 28, BG);
  if (i >= taskCount) return;
  const Task& task = tasks[i];
  tft.drawRect(5, y + 4, 7, 7, MUTED); // task type icon
  if (task.high) tft.fillRect(15, y + 3, 2, 10, RED);
  font.setFont(u8g2_font_6x13_t_cyrillic);
  titleAt(21, y + 13, task.title, task.overdue ? RED : WHITE, task.overdue ? RED_TITLE_EDGE : BG);
  font.setFont(u8g2_font_6x12_t_cyrillic);
  tft.fillCircle(8, y + 22, 2, task.color);
  String due = dayLabel(task);
  if (task.dueTime.length()) due += " " + task.dueTime;
  int dueX = 235 - font.getUTF8Width(due.c_str());
  textAt(15, y + 26, task.category, MUTED, dueX - 23);
  textAt(dueX, y + 26, due, task.overdue ? RED : MUTED, 235 - dueX);
}

void drawHeader(bool clear = true) {
  if (nightMode.state != NightMode::Awake) return;
  if (clear) {
    // Task counts/date can change without disturbing the update status.
    tft.fillRect(0, 0, UPDATE_STATUS_X, 19, BG);
    tft.fillRect(0, 19, 240, 21, BG);
  }
  font.setFont(u8g2_font_6x13_t_cyrillic);
  textAt(5, 15, "НАПОТІМ", PURPLE);
  font.setFont(u8g2_font_6x12_t_cyrillic);
  if (date.length() == 10) {
    String label = date.substring(8) + "." + date.substring(5, 7);
    int width = font.getUTF8Width(label.c_str());
    textAt((240 - width) / 2, 15, label, MUTED, width);
  }
  drawUpdateStatus(!clear);
  textAt(5, 32, String("Сьогодні: ") + todayTotal + "   Прострочено: " + overdueTotal, MUTED);
  tft.drawFastHLine(5, 38, 230, 0x2945);
}

void drawSetup(bool force = false) {
  if (nightMode.state != NightMode::Awake) return;
  bool connected = WiFi.status() == WL_CONNECTED;
  setupAddress = connected ? WiFi.localIP().toString() : "192.168.4.1";
  static bool painted = false, paintedConfigured = false;
  static uint8_t paintedRotation = UINT8_MAX;
  static String paintedStage, paintedWifi, paintedAddress;
  bool full = force || !painted || configured != paintedConfigured || rotation != paintedRotation;
  if (full) {
    tft.fillScreen(BG);
    painted = true; paintedConfigured = configured; paintedRotation = rotation;
  }
  if (configured) {
    if (full) drawHeader(false);
    String stage = !connected ? "Підключення до Wi-Fi…" :
      time(nullptr) < 1760000000 ? "Синхронізація часу…" :
      failure.length() ? "Очікування повтору…" : "Отримання задач…";
    String wifi = connected ? "Wi-Fi підключено" : "Очікування Wi-Fi";
    String address = connected || apActive ? setupAddress : "IP ще не отримано";
    font.setFont(u8g2_font_6x13_t_cyrillic);
    if (full || stage != paintedStage) {
      if (!full) tft.fillRect(0, 70, 240, 18, BG);
      textAt(8, 83, stage, WHITE); paintedStage = stage;
    }
    if (full || wifi != paintedWifi) {
      if (!full) tft.fillRect(0, 97, 240, 18, BG);
      textAt(8, 110, wifi, MUTED); paintedWifi = wifi;
    }
    if (full || address != paintedAddress) {
      if (!full) tft.fillRect(0, 120, 240, 18, BG);
      textAt(8, 133, address, MUTED); paintedAddress = address;
    }
    drawFooter(full, !full);
    return;
  }
  // The pairing page is infrequent, but still needs its old IP erased.
  if (!full) tft.fillScreen(BG);
  font.setFont(u8g2_font_6x13_t_cyrillic);
  textAt(8, 22, "НАПОТІМ · налаштування", PURPLE);
  textAt(8, 55, connected ? "Відкрийте в браузері:" : "Wi-Fi: Napotim-Setup", WHITE);
  textAt(8, 77, setupAddress, WHITE);
  textAt(8, 110, "Користувач: admin", MUTED);
  textAt(8, 131, "Пароль:", MUTED);
  textAt(8, 151, webPassword, WHITE);
  textAt(8, 183, "Створіть ключ у Напотім:", MUTED);
  textAt(8, 204, "Налаштування > Інтеграції", MUTED);
}

bool taskEqual(const Task& a, const Task& b) {
  return a.title == b.title && a.category == b.category && a.dueDate == b.dueDate &&
    a.dueTime == b.dueTime && a.color == b.color && a.high == b.high && a.overdue == b.overdue && a.today == b.today;
}

void drawNightNotice() {
  tft.fillScreen(BG);
  font.setFont(u8g2_font_6x13_t_cyrillic);
  textAt(8, 35, "НАПОТІМ", PURPLE);
  if (nightMode.state == NightMode::SleepNotice) {
    textAt(8, 100, "Сон до " + scheduleTime(displaySettings.dayStart), WHITE);
    textAt(8, 130, "Нічний режим", MUTED);
    textAt(8, 165, "Екран зараз вимкнеться", MUTED);
  } else {
    textAt(8, 85, "Очікування точного часу", WHITE);
    textAt(8, 115, "Після вимкнення живлення", MUTED);
    textAt(8, 135, "потрібні Wi-Fi та інтернет", MUTED);
    textAt(8, 175, "Екран тимчасово вимкнеться", MUTED);
    textAt(8, 195, "Підключення триває", MUTED);
  }
}

void updateNightMode() {
  time_t now = time(nullptr);
  bool wasBlank = nightMode.blank();
  if (!nightMode.update(displaySettings, configured, timezoneKnown && now >= 1760000000,
      (now % 86400 + localOffset(now)) % 86400, millis())) return;
  if (nightMode.blank()) {
    setBrightness(); // Backlight fully off before putting ST7789 to sleep.
    tft.enableDisplay(false); tft.enableSleep(true);
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  } else {
    if (wasBlank) { tft.enableSleep(false); delay(120); tft.enableDisplay(true); }
    if (nightMode.notice()) drawNightNotice();
    else {
      tft.fillScreen(BG);
      if (showSetup) drawSetup(true);
      else {
        if (themeDisplay.active) { themeDisplay.render(tft); drawUpdateStatus(true); }
        else { drawHeader(false); for (uint8_t i = 0; i < ROWS; ++i) drawRow(i, false);
        if (!taskCount) { font.setFont(u8g2_font_6x13_t_cyrillic); textAt(15, 90, "На сьогодні все виконано", WHITE); }
        drawFooter(true, false); }
      }
      pollSchedule.next = millis(); // Refresh on wake, even after a failed poll.
    }
    setBrightness();
  }
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
  DisplaySettings freshSettings = displaySettings;
  bool hasSettings = !doc["display_settings"].isNull();
  if (hasSettings && !parseDisplaySettings(doc["display_settings"], freshSettings)) { dataError = "display_settings_schema"; return false; }
  if (settingsLocal) freshSettings = displaySettings;
  OffsetTransition freshChanges[4]; uint8_t freshChangeCount = 0;
  int freshOffset = doc["utc_offset_seconds"];
  if (freshOffset < -86400 || freshOffset > 86400 ||
      !parseTimezoneChanges(doc["timezone_transitions"], freshChanges, freshChangeCount)) { dataError = "timezone_schema"; return false; }
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
  bool rotate = rotation != freshSettings.rotation;
  bool first = !lastSuccess || showSetup || rotate || themeDisplay.active;
  // Retain the themed frame until the complete legacy snapshot is validated.
  if (themeDisplay.active) themeDisplay.clear();
  bool configChanged = !settingsEqual(displaySettings, freshSettings) || (hasSettings && !settingsRemote) ||
    !timezoneKnown || utcOffset != freshOffset || timezoneChangeCount != freshChangeCount;
  for (uint8_t i = 0; i < freshChangeCount && !configChanged; ++i)
    configChanged = timezoneChanges[i].at != freshChanges[i].at || timezoneChanges[i].offset != freshChanges[i].offset;
  displaySettings = freshSettings;
  if (hasSettings) settingsRemote = true;
  utcOffset = freshOffset; timezoneKnown = true; timezoneChangeCount = freshChangeCount;
  for (uint8_t i = 0; i < freshChangeCount; ++i) timezoneChanges[i] = freshChanges[i];
  if (configChanged) saveConfig();
  setBrightness();
  if (rotate) tft.setRotation(rotation);
  bool newHeader = first || newDate != date || todayTotal != doc["today_total"].as<int>() || overdueTotal != doc["overdue_total"].as<int>();
  date = newDate; todayTotal = doc["today_total"]; overdueTotal = doc["overdue_total"];
  hiddenCount = doc["hidden_count"]; utcOffset = doc["utc_offset_seconds"];
  if (nightMode.state != NightMode::Awake) {
    // Keep the snapshot fresh without waking/painting over the night notice.
    taskCount = count; for (uint8_t i = 0; i < ROWS; ++i) tasks[i] = fresh[i];
    showSetup = false; lastSuccess = time(nullptr); failure = ""; refreshFailed = false;
    if (nightMode.notice()) drawNightNotice();
    if (firstSuccessMs == UINT32_MAX) firstSuccessMs = millis();
    return true;
  }
  bool screenCleared = first;
  if (first) tft.fillScreen(BG);
  markStage(7);
  showSetup = false;
  if (newHeader) drawHeader(!first);
  uint8_t previousCount = taskCount;
  bool rowsCleared = first;
  if (previousCount == 0 && count > 0 && !first) {
    tft.fillRect(0, 43, 240, 168, BG); first = true; rowsCleared = true;
  }
  taskCount = count;
  for (uint8_t i = 0; i < ROWS; ++i) {
    bool changed = first || (i < count && (i >= previousCount || !taskEqual(tasks[i], fresh[i]))) || (i >= count && i < previousCount);
    tasks[i] = fresh[i];
    if (changed) drawRow(i, !rowsCleared);
  }
  if (!count) {
    font.setFont(u8g2_font_6x13_t_cyrillic);
    textAt(15, 90, "На сьогодні все виконано", WHITE);
  }
  // Advance the success stamp only after validation and rendering.
  lastSuccess = time(nullptr); failure = ""; refreshFailed = false;
  drawFooter(first, !screenCleared);
  if (firstSuccessMs == UINT32_MAX) firstSuccessMs = millis();
  return true;
}

template<class Operation> int serverRequest(const String& path, Operation operation);
void rejectDeviceKey();

void pollLegacy() {
  dataError = ""; responseExpected = 0; responseBytes = 0;
  lastHttpMs = lastBodyMs = lastCleanupMs = lastRenderMs = 0;
  ++pollCount; markStage(1);
  if (WiFi.status() != WL_CONNECTED) { failure = "Немає Wi-Fi"; refreshFailed = true; drawFooter(); markStage(0); return; }
  if (time(nullptr) < 1760000000) { failure = "Очікування часу"; refreshFailed = true; drawFooter(); markStage(0); return; }
  failure = "";
  if (showSetup) drawSetup(); else drawFooter();
  int status;
  String payload;
  String path="/api/device/v1/today";
  if (!settingsRemote) path += "?initial_brightness=" + String(brightness) + "&initial_rotation=" + String(rotation);
  uint32_t requestStartedMs=millis();
  if (firstRequestMs==UINT32_MAX) firstRequestMs=requestStartedMs;
  status=serverRequest(path,[&](HTTPClient& http,WiFiClient& stream) {
    responseExpected=http.getSize();
    if (responseExpected>0 && responseExpected<=6144) {
      uint32_t bodyStartedMs=millis();
      const char* error=readSnapshotBody(stream,payload,responseExpected,15000,
        [](){return millis();},[](){return WiFi.status()==WL_CONNECTED;},[](){delay(1);});
      lastBodyMs=millis()-bodyStartedMs;responseBytes=payload.length();
      if (error) { dataError=error;payload=""; }
    } else dataError="empty_or_unbounded_body";
  });
  lastHttpMs=millis()-requestStartedMs;
  bool applied = false;
  if (status == 200 && payload.length()) {
    uint32_t renderStartedMs = millis();
    applied = applySnapshot(payload);
    lastRenderMs = millis() - renderStartedMs;
  }
  if (applied) {
    pollSchedule.interval = PollSchedule::NORMAL_INTERVAL; markStage(0); return;
  }
  refreshFailed = true;
  if (status == 401 || status == 403) {
    rejectDeviceKey();markStage(0);return;
  }
  else if (status == 404) {
    failure = "API ще не встановлено";
    pollSchedule.interval = lastSuccess ? PollSchedule::NORMAL_INTERVAL : PollSchedule::STARTUP_RETRY;
  }
  else { failure = status == 200 ? "Некоректні дані" : "Немає зв’язку"; pollSchedule.retry(lastSuccess != 0); }
  if (showSetup) drawSetup(); else drawFooter();
  markStage(0);
}

template<class Operation>
int serverRequest(const String& path, Operation operation) {
  String origin=apiOrigin.length()?apiOrigin:String(DEFAULT_ORIGIN);
  HTTPClient http;
  http.setTimeout(15000);http.useHTTP10(true);http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  auto request=[&](WiFiClient& client) {
    client.setTimeout(15000);
    if (!http.begin(client,origin+path)) return -1;
    http.addHeader("Authorization","Bearer "+token);
    http.addHeader("Accept-Encoding","identity");
    int status=http.GET();lastHttpStatus=status;
    if (status==200) {
      // ESP8266HTTPClient clones the supplied client. Plain HTTP connects the
      // clone, so reading the original WiFiClient dereferences a null context.
      WiFiClient* stream=http.getStreamPtr();
      if (stream) operation(http,*stream);
      else dataError="response_stream";
    }
    http.end();return status;
  };
  if (origin.startsWith("http://")) { WiFiClient client;return request(client); }
  BearSSL::WiFiClientSecure client;
  BearSSL::X509List ca(TRUST_ANCHOR);client.setTrustAnchors(&ca);
  client.setSSLVersion(BR_TLS12,BR_TLS12);client.setBufferSizes(16384,512);client.setSession(&tlsSession);
  int result=request(client);lastTlsError=client.getLastSSLError();return result;
}

bool validFrameId(const String& id) {
  if (id.length()!=64) return false;
  for (unsigned i=0;i<64;++i) if (!((id[i]>='0' && id[i]<='9') || (id[i]>='a' && id[i]<='f'))) return false;
  return true;
}

bool downloadFrame(const String& id, const String& theme, unsigned expected, ThemeHeader& header) {
  bool complete=false;
  FSInfo info;
  if (!LittleFS.info(info) || info.totalBytes-info.usedBytes<expected+8192) { frameError="frame_disk_full";return false; }
  int status=serverRequest("/api/device/v2/frames/"+id,[&](HTTPClient& http,WiFiClient& client) {
    if (http.getSize()!=int(expected)) { frameError="frame_length";return; }
    File file=LittleFS.open("/theme.pending","w");
    if (!file) { frameError="frame_file";return; }
    br_sha256_context hash;br_sha256_init(&hash);
    uint8_t chunk[256];unsigned received=0;uint32_t started=millis();
    while (received<expected && millis()-started<30000) {
      unsigned amount=min(unsigned(sizeof(chunk)),expected-received);
      int n=client.read(chunk,amount);
      if (n>0) {
        if (file.write(chunk,n)!=size_t(n)) { frameError="frame_write";break; }
        br_sha256_update(&hash,chunk,n);received+=n;yield();
      } else if (n<0 || WiFi.status()!=WL_CONNECTED) break;
      else delay(1);
    }
    file.close();
    if (received!=expected) { if (!frameError.length()) frameError="frame_incomplete";return; }
    uint8_t digest[32];br_sha256_out(&hash,digest);char hex[65];
    for (unsigned i=0;i<32;++i) snprintf(hex+i*2,3,"%02x",digest[i]);
    if (id!=hex) { frameError="frame_sha256";return; }
    complete=true;
  });
  if (!complete) { if (!frameError.length()) frameError="frame_http_"+String(status);LittleFS.remove("/theme.pending");return false; }
  // HTTP/TLS buffers have been freed before validation and rendering.
  File file=LittleFS.open("/theme.pending","r");
  const char* error=validateThemePacket(file,expected,header,[](){yield();});
  std::unique_ptr<uint8_t[]> preparedGlyphs;
  if (!error) {
    preparedGlyphs.reset(new(std::nothrow) uint8_t[header.atlasBytes]);
    if (!preparedGlyphs) error="frame_memory";
    else if (!file.seek(40) || file.read(preparedGlyphs.get(),header.atlasBytes)!=int(header.atlasBytes)) error="frame_atlas_read";
  }
  file.close();
  if (error) { frameError=error;LittleFS.remove("/theme.pending");return false; }
  if (!LittleFS.rename("/theme.pending","/theme.frame")) { frameError="frame_commit";LittleFS.remove("/theme.pending");return false; }
  // No fallible file read occurs after replacing the previous accepted file.
  themeDisplay.activate(header,id,theme,std::move(preparedGlyphs));
  ++frameDownloads;return true;
}

void rejectDeviceKey() {
  themeDisplay.clear();token="";configured=false;lastSuccess=0;taskCount=0;settingsRemote=false;
  for (auto& task:tasks) task=Task();
  failure="Ключ відкликано або недійсний";saveConfig();showSetup=true;drawSetup(true);
}

void poll() {
  if (legacyApi && static_cast<int32_t>(millis()-nextV2Probe)<0) { pollLegacy();return; }
  ++pollCount;markStage(1);dataError="";frameError="";responseBytes=0;
  lastHttpMs=lastBodyMs=lastCleanupMs=lastRenderMs=0;
  if (WiFi.status()!=WL_CONNECTED || time(nullptr)<1760000000) {
    failure=WiFi.status()!=WL_CONNECTED?"Немає Wi-Fi":"Очікування часу";
    refreshFailed=true;drawFooter();markStage(0);return;
  }
  String path="/api/device/v2/display";
  if (!settingsRemote) path+="?initial_brightness="+String(brightness)+"&initial_rotation="+String(rotation);
  String payload;uint32_t started=millis();
  if (firstRequestMs==UINT32_MAX) firstRequestMs=started;
  int status=serverRequest(path,[&](HTTPClient& http,WiFiClient& client) {
    responseExpected=http.getSize();
    if (responseExpected<=0 || responseExpected>4096) { dataError="manifest_length";return; }
    const char* error=readSnapshotBody(client,payload,responseExpected,15000,[](){return millis();},
      [](){return WiFi.status()==WL_CONNECTED;},[](){delay(1);});
    responseBytes=payload.length();if (error) { dataError=error;payload=""; }
  });
  lastHttpMs=millis()-started;
  if (status==404) {
    legacyApi=true;nextV2Probe=millis()+300000;--pollCount;pollLegacy();return;
  }
  legacyApi=false;
  bool applied=false;
  if (status==200 && payload.length()) {
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc,payload)) dataError="manifest_json";
    else {
      String id=doc["frame"]["id"]|"", theme=doc["theme"]|"";
      unsigned bytes=doc["frame"]["bytes"]|0U;
      DisplaySettings fresh=displaySettings;
      OffsetTransition changes[4];uint8_t changeCount=0;
      int offset=doc["utc_offset_seconds"]|100000;
      int visible=doc["visible_count"]|-1,total=doc["total"]|-1;
      String frameDate=doc["date"]|"";
      int freshToday=doc["today_total"]|-1,freshOverdue=doc["overdue_total"]|-1;
      bool validDate=frameDate.length()==10 && frameDate[4]=='-' && frameDate[7]=='-';
      for (unsigned i=0;i<frameDate.length() && validDate;++i)
        if (i!=4 && i!=7 && !isdigit(frameDate[i])) validDate=false;
      if (doc["schema_version"]!=2 || !validFrameId(id) || theme.length()>32 || !theme.length() ||
          !validDate || !doc["frame"]["bytes"].is<unsigned>() ||
          !doc["utc_offset_seconds"].is<int>() || !doc["visible_count"].is<int>() || !doc["total"].is<int>() ||
          bytes<40 || bytes>THEME_MAX_BYTES || visible<0 || visible>6 || total<visible ||
          !doc["today_total"].is<int>() || !doc["overdue_total"].is<int>() ||
          doc["today_total"].as<int>()<0 || doc["today_total"].as<int>()>total ||
          doc["overdue_total"].as<int>()!=total-doc["today_total"].as<int>() ||
          offset < -86400 || offset > 86400 ||
          !parseDisplaySettings(doc["display_settings"],fresh) ||
          !parseTimezoneChanges(doc["timezone_transitions"],changes,changeCount)) dataError="manifest_schema";
      else {
        // The manifest pool must also be freed before the second TLS handshake.
        // clear() alone retains its 4 KB allocation; only copied values survive.
        payload="";
        doc.clear();doc.shrinkToFit();
        bool changed=!themeDisplay.active || id!=themeDisplay.id;
        ThemeHeader header=themeDisplay.header;
        bool ready=!changed || downloadFrame(id,theme,bytes,header);
        if (ready) {
          if (settingsLocal) fresh=displaySettings;
          bool rotated=fresh.rotation!=rotation;
          bool configChanged=!settingsEqual(fresh,displaySettings) || !settingsRemote || !timezoneKnown ||
            offset!=utcOffset || changeCount!=timezoneChangeCount;
          for (uint8_t i=0;i<changeCount && !configChanged;++i)
            configChanged=changes[i].at!=timezoneChanges[i].at || changes[i].offset!=timezoneChanges[i].offset;
          displaySettings=fresh;settingsRemote=true;timezoneKnown=true;utcOffset=offset;timezoneChangeCount=changeCount;
          for (uint8_t i=0;i<changeCount;++i) timezoneChanges[i]=changes[i];
          if (configChanged) saveConfig();
          if (rotated) tft.setRotation(rotation);
          taskCount=visible;hiddenCount=total-visible;todayTotal=freshToday;overdueTotal=freshOverdue;
          date=frameDate;
          bool full=changed || showSetup || rotated;showSetup=false;
          updateNightMode();setBrightness();
          uint32_t renderStarted=millis();
          ready=nightMode.state!=NightMode::Awake || !full || themeDisplay.render(tft);
          lastRenderMs=millis()-renderStarted;
          if (ready) {
            lastSuccess=time(nullptr);refreshFailed=false;failure="";
            if (nightMode.state==NightMode::Awake) drawUpdateStatus(full);
            if (firstSuccessMs==UINT32_MAX) firstSuccessMs=millis();
            applied=true;
          } else frameError="frame_render";
        } else if (!frameError.length()) frameError="frame_load";
      }
    }
  }
  if (applied) pollSchedule.interval=PollSchedule::NORMAL_INTERVAL;
  else {
    refreshFailed=true;
    if (status==401 || status==403 || lastHttpStatus==401 || lastHttpStatus==403) rejectDeviceKey();
    else { failure="Немає свіжих даних";pollSchedule.retry(lastSuccess!=0);if (showSetup) drawSetup();else drawFooter(); }
  }
  markStage(0);
}

bool authenticated() {
  if (web.authenticateDigest("admin", web.credentialHash("admin", "Napotim Cube", webPassword.c_str()))) return true;
  web.requestAuthentication(DIGEST_AUTH, "Napotim Cube"); return false;
}

bool writeScreenBytes(WiFiClient& client,const uint8_t* data,unsigned length) {
  unsigned written=0;uint32_t started=millis();
  while (written<length && millis()-started<10000) {
    size_t n=client.write(data+written,length-written);
    if (n) { written+=n;started=millis(); }
    else if (!client.connected()) return false;
    yield();
  }
  return written==length;
}

const char SETTINGS_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="uk"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Напотім · кубик</title><style>body{background:#101014;color:#eee;font:16px system-ui;max-width:520px;margin:40px auto;padding:20px}label{display:block;margin:20px 0}input,select,button{font:inherit;padding:10px;width:100%;box-sizing:border-box;background:#202026;color:#eee;border:1px solid #555;border-radius:8px}input[type=checkbox]{width:auto}button{background:#7046b5}a{color:#bb9cec}</style><h1>Напотім · кубик</h1><p>Сервер: <input form="display-config" name="api_origin" value="{{api_origin}}" aria-label="Сервер Напотім"></p><p>Оновлення задач раз на хвилину. Налаштування зберігаються після вимкнення живлення.</p><form id="display-config" method="post" action="/config"><label>Ключ із Напотім → Налаштування → Інтеграції<input name="token" type="password" autocomplete="off" maxlength="48" placeholder="Залиште порожнім, щоб зберегти поточний"></label><label>Джерело налаштувань<select name="source"><option value="remote" {{remote}}>Напотім</option><option value="local" {{local}}>Ця плата</option></select></label><p>У режимі «Ця плата» наведені нижче параметри мають перевагу. У режимі «Напотім» вони заміняться під час синхронізації.</p><label>Яскравість: 1–100<input name="brightness" type="number" min="1" max="100" value="{{brightness}}" required></label><label>Поворот: 0–3<input name="rotation" type="number" min="0" max="3" value="{{rotation}}" required></label><label><input name="scheduled" type="checkbox" {{scheduled}}> Яскравість за розкладом</label><label>Денна яскравість<input name="day_brightness" type="number" min="1" max="100" value="{{day_brightness}}" required></label><label>Вечірня яскравість<input name="night_brightness" type="number" min="1" max="100" value="{{night_brightness}}" required></label><label>Початок дня / пробудження<input name="day_start" type="time" value="{{day_start}}" required></label><label>Початок вечора<input name="night_start" type="time" value="{{night_start}}" required></label><label><input name="sleep_enabled" type="checkbox" {{sleep_enabled}}> Нічний сон: повністю вимикати екран</label><label>Початок сну<input name="sleep_start" type="time" value="{{sleep_start}}" required></label><p>Сон працює і з ручною яскравістю. Часовий пояс береться з Напотім. Після нічного ввімкнення показується повідомлення про сон. Якщо після втрати живлення час невідомий, через 15 секунд екран гасне до синхронізації; підключення триває.</p><button>Зберегти</button></form><hr><h2>Wi-Fi</h2><form method="post" action="/wifi"><label>Назва мережі<input name="ssid" required maxlength="32"></label><label>Пароль<input name="password" type="password" autocomplete="off" minlength="8" maxlength="63" required></label><button>Підключити</button></form><p><a href="/update">Оновити прошивку</a></p></html>)HTML";

void configureWeb() {
  static bool uploadAllowed = false;
  static bool uploadStarted = false;
  web.on("/health", HTTP_GET, []() { web.send(200, "application/json", "{\"firmware\":\"napotim-cube\",\"version\":\"2.0.2\"}"); });
  web.on("/refresh",HTTP_POST,[]() {
    if (!authenticated()) return;
    legacyApi=false;
    pollSchedule.next=millis();web.send(200,"application/json","{\"scheduled\":true}");
  });
  web.on("/restore-source",HTTP_POST,[]() {
    if (!authenticated()) return;
    File backup=LittleFS.open("/source-backup.json","r");DynamicJsonDocument doc(1536);
    DisplaySettings restored;
    if (!backup || deserializeJson(doc,backup) || !doc["token"].is<const char*>() ||
        !parseDisplaySettings(doc["display_settings"],restored)) {
      web.send(409,"text/plain","No valid previous server configuration");return;
    }
    backup.close();
    token=doc["token"].as<String>();apiOrigin=doc["api_origin"]|"";
    displaySettings=restored;settingsLocal=doc["settings_local"]|false;settingsRemote=false;
    themeDisplay.clear();lastSuccess=0;taskCount=0;showSetup=true;legacyApi=false;
    configured=token.startsWith("nptd_") && token.length()==48;
    sourceConfirmationNeeded=false;
    saveConfig();LittleFS.remove("/source-backup.json");tft.setRotation(rotation);updateNightMode();setBrightness();
    if (nightMode.state==NightMode::Awake) drawSetup(true);
    pollSchedule.next=millis();web.send(200,"application/json","{\"restored\":true}");
  });
  web.on("/screen.bmp",HTTP_GET,[]() {
    if (!authenticated()) return;
    if (!themeDisplay.active || nightMode.state!=NightMode::Awake) { web.send(409,"text/plain","No active themed screen");return; }
    uint8_t bmp[66]={0};bmp[0]='B';bmp[1]='M';
    auto u32=[&](int pos,uint32_t value) { for (int i=0;i<4;++i) bmp[pos+i]=(value>>(i*8))&255; };
    u32(2,66+115200);u32(10,66);u32(14,40);u32(18,240);u32(22,uint32_t(-240));
    bmp[26]=1;bmp[28]=16;u32(30,3);u32(34,115200);u32(54,0xF800);u32(58,0x07E0);u32(62,0x001F);
    web.sendHeader("Cache-Control","no-store");web.setContentLength(66+115200);web.send(200,"image/bmp","");
    WiFiClient output=web.client();
    bool complete=writeScreenBytes(output,bmp,66);
    String stamp=themeDisplay.paintedStamp;bool stale=themeDisplay.paintedStale;
    themeDisplay.rows([&](int y,uint16_t* pixels) {
      if (y>=themeDisplay.header.y && y<themeDisplay.header.y+themeDisplay.header.glyphHeight)
        for (unsigned x=0;x<5U*themeDisplay.header.glyphWidth;++x)
          pixels[themeDisplay.header.x+x]=themeDisplay.statusPixel(stamp,stale,x,y-themeDisplay.header.y);
      if (complete) complete=writeScreenBytes(output,reinterpret_cast<uint8_t*>(pixels),480);
    });
    if (complete) output.flush(5000);
  });
  web.on("/status", HTTP_GET, []() {
    if (!authenticated()) return;
    DynamicJsonDocument doc(2560);
    doc["configured"]=configured; doc["wifi_connected"]=WiFi.status()==WL_CONNECTED;
    doc["free_heap"]=ESP.getFreeHeap(); doc["largest_heap_block"]=ESP.getMaxFreeBlockSize();
    doc["http_status"]=lastHttpStatus; doc["tls_error"]=lastTlsError;
    doc["response_expected"]=responseExpected; doc["response_bytes"]=responseBytes; doc["data_error"]=dataError;
    doc["last_success"]=lastSuccess; doc["task_count"]=taskCount;
    doc["hidden_count"]=hiddenCount; doc["update_stale"]=updateStale();
    doc["display_protocol"]=themeDisplay.active?2:1;doc["theme"]=themeDisplay.active?themeDisplay.theme:String("legacy");
    doc["frame_id"]=themeDisplay.id;doc["frame_downloads"]=frameDownloads;doc["frame_error"]=frameError;
    doc["api_origin"]=apiOrigin.length()?apiOrigin:String(DEFAULT_ORIGIN);
    doc["status_stamp"]=themeDisplay.paintedStamp;doc["status_red"]=themeDisplay.paintedStale;
    doc["source_confirmation_needed"]=sourceConfirmationNeeded;
    doc["brightness"]=brightness; doc["rotation"]=rotation;
    doc["effective_brightness"]=effectiveBrightness / 100.0;
    doc["settings_remote"]=settingsRemote; doc["settings_local"]=settingsLocal; doc["timezone_known"]=timezoneKnown;
    const char* states[] = {"awake", "sleep_notice", "sleeping", "clock_notice", "waiting_for_clock"};
    doc["night_state"] = states[nightMode.state]; doc["screen_off"] = nightMode.blank();
    doc["sleep_strategy"] = "display_sleep_with_running_clock";
    doc["wake_time"] = scheduleTime(displaySettings.dayStart);
    writeDisplaySettings(doc.createNestedObject("display_settings"), displaySettings);
    doc["error"]=failure; doc["uptime_seconds"]=millis()/1000;
    doc["station_ip"]=WiFi.localIP().toString(); doc["wifi_status"]=static_cast<int>(WiFi.status());
    doc["reset_info"]=ESP.getResetInfo(); doc["boot_count"]=bootTrace.boots;
    doc["previous_stage"]=previousStage; doc["previous_heap"]=previousHeap;
    doc["poll_count"]=pollCount; doc["clock_epoch"]=time(nullptr);
    doc["clock_ready"]=time(nullptr)>=1760000000; doc["wifi_rssi"]=WiFi.RSSI();
    doc["poll_interval_ms"]=pollSchedule.interval;
    JsonObject timing = doc.createNestedObject("timing");
    if (firstWifiReadyMs != UINT32_MAX) timing["wifi_ready_ms"] = firstWifiReadyMs;
    if (firstClockReadyMs != UINT32_MAX) timing["clock_ready_ms"] = firstClockReadyMs;
    if (firstRequestMs != UINT32_MAX) timing["first_request_ms"] = firstRequestMs;
    if (firstSuccessMs != UINT32_MAX) timing["first_success_ms"] = firstSuccessMs;
    timing["last_http_ms"] = lastHttpMs; timing["last_body_ms"] = lastBodyMs;
    timing["last_cleanup_ms"] = lastCleanupMs; timing["last_render_ms"] = lastRenderMs;
    timing["last_poll_ms"] = lastPollMs;
    String out; serializeJson(doc,out);
    web.sendHeader("Cache-Control","no-store"); web.send(200,"application/json",out);
  });
  web.on("/", HTTP_GET, []() {
    if (!authenticated()) return;
    String page = FPSTR(SETTINGS_PAGE);
    page.replace("{{brightness}}", String(brightness)); page.replace("{{rotation}}", String(rotation));
    page.replace("{{day_brightness}}", String(displaySettings.dayBrightness));
    page.replace("{{night_brightness}}", String(displaySettings.nightBrightness));
    page.replace("{{day_start}}", scheduleTime(displaySettings.dayStart));
    page.replace("{{night_start}}", scheduleTime(displaySettings.nightStart));
    page.replace("{{sleep_start}}", scheduleTime(displaySettings.sleepStart));
    page.replace("{{scheduled}}", displaySettings.scheduled ? "checked" : "");
    page.replace("{{sleep_enabled}}", displaySettings.sleepEnabled ? "checked" : "");
    String origin=apiOrigin.length()?apiOrigin:String(DEFAULT_ORIGIN);
    origin.replace("&","&amp;");origin.replace("\"","&quot;");origin.replace("<","&lt;");origin.replace(">","&gt;");
    page.replace("{{api_origin}}",origin);
    page.replace("{{local}}", settingsLocal ? "selected" : ""); page.replace("{{remote}}", settingsLocal ? "" : "selected");
    web.sendHeader("Cache-Control", "no-store"); web.send(200, "text/html; charset=utf-8", page);
  });
  web.on("/config", HTTP_POST, []() {
    if (!authenticated()) return;
    String supplied = web.arg("token"); supplied.trim();
    bool valid = supplied.length() == 48 && supplied.startsWith("nptd_");
    for (uint8_t i = 5; valid && i < supplied.length(); ++i) valid = isalnum(supplied[i]) || supplied[i] == '_' || supplied[i] == '-';
    if (supplied.length() && !valid) { web.send(400, "text/plain; charset=utf-8", "Некоректний ключ пристрою."); return; }
    String newOrigin=apiOrigin;
    if (web.hasArg("api_origin")) {
      newOrigin=web.arg("api_origin");newOrigin.trim();while (newOrigin.endsWith("/")) newOrigin.remove(newOrigin.length()-1);
      if (!validApiOrigin(newOrigin.c_str())) {
        web.send(400,"text/plain","Use HTTPS or a private IPv4 development server with a valid port");return;
      }
      if (newOrigin==DEFAULT_ORIGIN) newOrigin="";
      if (!validApiSourceChange(apiOrigin.c_str(),newOrigin.c_str(),token.c_str(),supplied.c_str())) { web.send(400,"text/plain","A new server requires a different device key");return; }
    }
    DisplaySettings fresh = displaySettings;
    bool fullForm = web.hasArg("source");
    if (fullForm) {
      if (web.arg("source") != "local" && web.arg("source") != "remote") { web.send(400, "text/plain", "Invalid settings source"); return; }
      DynamicJsonDocument doc(768);
      doc["mode"] = web.hasArg("scheduled") ? "scheduled" : "manual";
      for (const char* key : {"brightness", "rotation", "day_brightness", "night_brightness"}) {
        String value = web.arg(key);
        if (!value.length() || value.length() > 3) { web.send(400, "text/plain", "Invalid number"); return; }
        for (unsigned i = 0; i < value.length(); ++i) if (!isdigit(value[i])) { web.send(400, "text/plain", "Invalid number"); return; }
        doc[key] = value.toInt();
      }
      for (const char* key : {"day_start", "night_start", "sleep_start"}) doc[key] = web.arg(key);
      doc["sleep_enabled"] = web.hasArg("sleep_enabled");
      if (!parseDisplaySettings(doc.as<JsonVariantConst>(), fresh)) {
        web.send(400, "text/plain; charset=utf-8", "Перевірте розклад: день → вечір → сон → день, між початками щонайменше 15 хвилин. Для ручної яскравості потрібні лише різні часи сну й пробудження."); return;
      }
    } else {
      // Compatibility with older provisioning clients; omitted sleep fields persist.
      if (web.hasArg("rotation")) fresh.rotation = constrain(web.arg("rotation").toInt(), 0, 3);
      if (web.hasArg("brightness")) { fresh.brightness = constrain(web.arg("brightness").toInt(), 1, 100); fresh.scheduled = false; }
    }
    if (newOrigin!=apiOrigin && !LittleFS.exists("/source-backup.json")) {
      DynamicJsonDocument backup(1536);
      backup["token"]=token;backup["api_origin"]=apiOrigin;backup["settings_local"]=settingsLocal;
      writeDisplaySettings(backup.createNestedObject("display_settings"),displaySettings);
      File file=LittleFS.open("/source-backup.json","w");
      if (!file || !serializeJson(backup,file)) { web.send(500,"text/plain","Could not save previous source");return; }
      file.close();
    }
    if ((supplied.length() && supplied != token) || newOrigin!=apiOrigin) {
      settingsRemote=false;themeDisplay.clear();lastSuccess=0;taskCount=0;showSetup=true;
      legacyApi=false;nextV2Probe=0;
    }
    apiOrigin=newOrigin;
    sourceConfirmationNeeded=false;
    if (supplied.length()) token = supplied;
    bool rotate = rotation != fresh.rotation;
    displaySettings = fresh;
    if (fullForm) settingsLocal = web.arg("source") == "local";
    configured = token.length() == 48; saveConfig();
    failure = "Очікування синхронізації";
    if (rotate) tft.setRotation(rotation);
    updateNightMode(); setBrightness();
    if (nightMode.notice()) drawNightNotice();
    else if (nightMode.state == NightMode::Awake) {
      if (showSetup) drawSetup(true);
      else { tft.fillScreen(BG);if (themeDisplay.active) { themeDisplay.render(tft);drawUpdateStatus(true); }
        else { drawHeader(false); for (uint8_t i=0;i<ROWS;++i) drawRow(i, false); drawFooter(true, false); } }
    }
    pollSchedule.next = millis() + 1500; pollSchedule.interval = PollSchedule::NORMAL_INTERVAL;
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "text/plain; charset=utf-8", settingsLocal ?
      "Збережено на платі. Локальні налаштування застосовано." :
      "Збережено. Налаштування з Напотім надійдуть після синхронізації.");
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
  // Start networking before the display's reset delays, filesystem load and
  // initial rendering. Saved Wi-Fi credentials belong to the SDK, not LittleFS.
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin();
  setupStarted = millis();
  configTime(0, 0, "time.cloudflare.com", "pool.ntp.org");
  pinMode(5, OUTPUT); digitalWrite(5, HIGH); analogWriteRange(1023);
  tft.init(240, 240, SPI_MODE3); font.begin(tft); font.setFontMode(1); font.setFontDirection(0);
  loadConfig(); tft.setRotation(rotation);
  WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  updateNightMode(); setBrightness();
  configureWeb();
  if (configured) drawSetup();
  else { tft.fillScreen(BG); font.setFont(u8g2_font_6x13_t_cyrillic); textAt(8, 25, "НАПОТІМ · підключення…", PURPLE); }
}

void loop() {
  web.handleClient();
  updateNightMode();
  static uint32_t lastBrightnessSecond = UINT32_MAX;
  uint32_t second = millis() / 1000;
  if (second != lastBrightnessSecond) { setBrightness(); lastBrightnessSecond = second; }
  if (!apActive && WiFi.status() != WL_CONNECTED && millis() - setupStarted > 30000) {
    WiFi.mode(WIFI_AP_STA); WiFi.softAP("Napotim-Setup", webPassword.c_str()); apActive = true;
    if (showSetup) drawSetup();
  }
  bool connected = WiFi.status() == WL_CONNECTED;
  bool clockReady = time(nullptr) >= 1760000000;
  if (connected && firstWifiReadyMs == UINT32_MAX) firstWifiReadyMs = millis();
  if (clockReady && firstClockReadyMs == UINT32_MAX) firstClockReadyMs = millis();
  static bool previousConnected = false, previousClockReady = false;
  bool readinessChanged = connected != previousConnected || clockReady != previousClockReady;
  if (readinessChanged) {
    if (failure == "Немає Wi-Fi" || failure == "Очікування часу") failure = "";
    if ((!connected || !clockReady) && lastSuccess) {
      failure = connected ? "Очікування часу" : "Немає Wi-Fi"; refreshFailed = true;
    }
    previousConnected = connected; previousClockReady = clockReady;
    if (!showSetup) drawFooter();
  }
  if (showSetup && (configured || connected || apActive)) {
    String address = connected ? WiFi.localIP().toString() : "192.168.4.1";
    if (readinessChanged || address != setupAddress) drawSetup();
  }
  if (apActive && WiFi.status() == WL_CONNECTED && configured) { WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); apActive = false; }
  pollSchedule.observeReadiness(connected && clockReady, millis());
  if (configured && !sourceConfirmationNeeded && pollSchedule.due(millis())) { poll(); pollSchedule.scheduleFrom(millis()); }
  updateNightMode();
  uint32_t minute = millis() / 60000;
  if (lastSuccess && minute != lastFooterMinute) { drawFooter(); lastFooterMinute = minute; }
  delay(nightMode.blank() ? 20 : 1);
}
