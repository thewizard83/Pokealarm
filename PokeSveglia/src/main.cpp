// =====================================================================
//  PokeSveglia — sveglia a tema Pokémon per il robot XiaoZhi ESP32-S3
//  (Spotpear ESP32-S3-1.28-BOX, schermo rotondo 240x240 con touch)
//
//  Schermate:   [ Meteo ]  <— swipe a destra —  [ Orologio ]  — swipe a sinistra —>  [ Sveglie ]
//  Sveglia che suona:  tocco breve = posticipa,  tocco lungo / tasto BOOT / scuotere = spegni
//  Tasto BOOT tenuto premuto 3 s: attiva la rete di configurazione "PokeSveglia-Setup"
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <AnimatedGIF.h>
#include <esp_system.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include "app.h"
#include "lgfx_config.h"

// ----------------------------------------------------------------- stato
Alarm alarms[MAX_ALARMS];
Settings settings;
WeatherData weather;
SemaphoreHandle_t dataMutex;
volatile bool gifReloadRequested = false;
volatile bool weatherRefreshRequested = false;
volatile bool testAlarmRequested = false;
volatile bool stopAlarmRequested = false;
volatile bool portalActive = false;
bool hasAccelerometer = false;
volatile uint8_t timeSource = TIME_NONE;

// Ora conservata nella memoria RTC: sopravvive ai riavvii "morbidi" (riavvio software,
// crash, watchdog) ma non allo spegnimento. Il timer RTC continua a contare durante il riavvio.
extern "C" uint64_t esp_rtc_get_time_us(void);
struct RtcKeep {
  uint32_t magic;
  int64_t epoch;   // secondi UNIX ...
  uint64_t rtcUs;  // ... corrispondenti a questo istante del timer RTC
  char tz[48];
};
static const uint32_t RTC_KEEP_MAGIC = 0x504B5356;  // "PKSV"
RTC_NOINIT_ATTR static RtcKeep rtcKeep;

static LGFX lcd;
static LGFX_Sprite canvas(&lcd);
static AnimatedGIF gif;
static uint16_t* gifBuf = nullptr;  // fotogramma GIF corrente (RGB565 big-endian)
static uint8_t* gifData = nullptr;  // file GIF intero in PSRAM
static bool gifOk = false;
volatile int gifLayX = 0, gifLayY = 0, gifLayS = 100, gifLayV = 100;
volatile bool layoutChanged = false;
static int16_t mapX[SCREEN_W], mapY[SCREEN_H];  // pixel sorgente per ogni pixel dello schermo
static bool mapIdentity = true;
// zona della GIF cambiata nell'ultimo fotogramma (coordinate del buffer GIF)
static int dirtyX0 = SCREEN_W, dirtyY0 = SCREEN_H, dirtyX1 = 0, dirtyY1 = 0;
// zona dello schermo da ridisegnare (tutto lo schermo, salvo aggiornamenti parziali)
static int clipX0 = 0, clipY0 = 0, clipX1 = SCREEN_W, clipY1 = SCREEN_H;
static int gifOffX = 0, gifOffY = 0;
static uint32_t nextFrameAt = 0;

static DNSServer dns;
static Preferences prefs;
static String wifiSsid, wifiPass;
static bool portalManual = false;
static volatile uint8_t lastDiscReason = 0;  // ultimo motivo di disconnessione Wi-Fi
static uint32_t portalStartMs = 0;

enum Page { PAGE_WEATHER = -1, PAGE_CLOCK = 0, PAGE_ALARMS = 1 };
static int page = PAGE_CLOCK;
static uint32_t lastInteraction = 0;
static uint32_t wakeUntil = 0;  // schermo acceso al massimo dopo un tocco notturno

enum RingState { RING_IDLE, RING_ACTIVE, RING_SNOOZED };
static RingState ringState = RING_IDLE;
static int ringingAlarm = -1;  // -1 = prova
static uint32_t ringStartMs = 0;
static time_t snoozeUntil = 0;
static uint32_t testStopAt = 0;

static int batteryLevel = -1;
static bool charging = false;   // batteria in carica
static bool usbPower = false;   // alimentata da USB (in carica o già carica)
static int batteryAdc = 0;
static uint8_t accelAddr = 0;
static TwoWire* accelBus = nullptr;

static char toastMsg[48] = "";
static uint32_t toastUntil = 0;

// ----------------------------------------------------------------- colori
static constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static constexpr uint16_t C_WHITE = rgb(255, 255, 255);
static constexpr uint16_t C_BLACK = rgb(0, 0, 0);
static constexpr uint16_t C_YELLOW = rgb(255, 203, 5);
static constexpr uint16_t C_RED = rgb(227, 53, 13);
static constexpr uint16_t C_BLUE = rgb(59, 76, 202);
static constexpr uint16_t C_GRAY = rgb(120, 120, 135);
static constexpr uint16_t C_DGRAY = rgb(55, 55, 70);
static constexpr uint16_t C_GREEN = rgb(46, 190, 90);
static constexpr uint16_t C_CYAN = rgb(110, 200, 255);
static constexpr uint16_t C_ORANGE = rgb(255, 140, 40);
static constexpr uint16_t C_NAVY = rgb(16, 26, 58);

static const char* DOW[] = {"DOM", "LUN", "MAR", "MER", "GIO", "VEN", "SAB"};
static const char* MON[] = {"GEN", "FEB", "MAR", "APR", "MAG", "GIU", "LUG", "AGO", "SET", "OTT", "NOV", "DIC"};

void showToast(const char* msg, uint32_t ms = 3000) {
  strlcpy(toastMsg, msg, sizeof(toastMsg));
  toastUntil = millis() + ms;
}

static bool timeValid() { return time(nullptr) > 1700000000; }

String ipString() { return WiFi.localIP().toString(); }

// =====================================================================
//  Salvataggio configurazione
// =====================================================================
void saveConfig() {
  JsonDocument doc;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  JsonArray al = doc["alarms"].to<JsonArray>();
  for (auto& a : alarms) {
    JsonObject o = al.add<JsonObject>();
    o["en"] = a.enabled;
    o["h"] = a.hour;
    o["m"] = a.minute;
    o["days"] = a.days;
    o["label"] = a.label;
  }
  JsonObject s = doc["settings"].to<JsonObject>();
  s["volume"] = settings.volume;
  s["brightDay"] = settings.brightDay;
  s["brightNight"] = settings.brightNight;
  s["nightStart"] = settings.nightStart;
  s["nightEnd"] = settings.nightEnd;
  s["snoozeMin"] = settings.snoozeMin;
  s["ringMaxMin"] = settings.ringMaxMin;
  s["city"] = settings.city;
  s["gif"] = settings.gif;
  xSemaphoreGive(dataMutex);

  File f = LittleFS.open("/config.json", "w");
  if (f) {
    serializeJson(doc, f);
    f.close();
  }
}

void loadConfig() {
  File f = LittleFS.open("/config.json", "r");
  if (!f) {
    alarms[0].hour = 7;
    alarms[0].minute = 0;
    alarms[0].days = 0x1F;
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, f)) {
    f.close();
    return;
  }
  f.close();
  JsonArray al = doc["alarms"];
  for (int i = 0; i < MAX_ALARMS && i < (int)al.size(); i++) {
    alarms[i].enabled = al[i]["en"] | false;
    alarms[i].hour = al[i]["h"] | 7;
    alarms[i].minute = al[i]["m"] | 0;
    alarms[i].days = al[i]["days"] | 0x1F;
    strlcpy(alarms[i].label, al[i]["label"] | "", sizeof(alarms[i].label));
  }
  JsonObject s = doc["settings"];
  settings.volume = s["volume"] | settings.volume;
  settings.brightDay = s["brightDay"] | settings.brightDay;
  settings.brightNight = s["brightNight"] | settings.brightNight;
  settings.nightStart = s["nightStart"] | settings.nightStart;
  settings.nightEnd = s["nightEnd"] | settings.nightEnd;
  settings.snoozeMin = s["snoozeMin"] | settings.snoozeMin;
  settings.ringMaxMin = s["ringMaxMin"] | settings.ringMaxMin;
  strlcpy(settings.city, s["city"] | "", sizeof(settings.city));
  strlcpy(settings.gif, s["gif"] | settings.gif, sizeof(settings.gif));
}

void saveWifi(const String& ssid, const String& pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

// =====================================================================
//  GIF animata di sfondo
// =====================================================================
static void GIFDraw(GIFDRAW* d) {
  int y = gifOffY + d->iY + d->y;
  if (y < 0 || y >= SCREEN_H) return;
  int x0 = gifOffX + d->iX;
  int xa = max(0, x0), xb = min(SCREEN_W, x0 + d->iWidth);
  if (xa < xb) {
    dirtyY0 = min(dirtyY0, y);
    dirtyY1 = max(dirtyY1, y + 1);
    dirtyX0 = min(dirtyX0, xa);
    dirtyX1 = max(dirtyX1, xb);
  }
  uint16_t* dst = gifBuf + y * SCREEN_W;
  const uint8_t* src = d->pPixels;
  const uint16_t* pal = d->pPalette;
  for (int i = 0; i < d->iWidth; i++) {
    int x = x0 + i;
    if (x < 0 || x >= SCREEN_W) continue;
    uint8_t c = src[i];
    if (d->ucHasTransparency && c == d->ucTransparent) continue;
    dst[x] = pal[c];
  }
}

static String firstGifName() {
  File dir = LittleFS.open("/gifs");
  if (!dir) return "";
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    if (n.endsWith(".gif")) return n;
  }
  return "";
}

// ---- posizione/dimensione: file /gifs/<nome>.gif.json accanto alla GIF
static void buildLayoutMap() {
  int x = gifLayX, y = gifLayY, s = constrain((int)gifLayS, 20, 400);
  mapIdentity = x == 0 && y == 0 && s == 100;
  for (int d = 0; d < SCREEN_W; d++) {
    int sx = (int)floorf((d - SCREEN_W / 2 - x) * 100.0f / s + SCREEN_W / 2);
    mapX[d] = (sx < 0 || sx >= SCREEN_W) ? -1 : sx;
  }
  for (int d = 0; d < SCREEN_H; d++) {
    int sy = (int)floorf((d - SCREEN_H / 2 - y) * 100.0f / s + SCREEN_H / 2);
    mapY[d] = (sy < 0 || sy >= SCREEN_H) ? -1 : sy;
  }
}

static void loadLayout(const String& name) {
  gifLayX = 0;
  gifLayY = 0;
  gifLayS = 100;
  gifLayV = 100;
  File f = LittleFS.open("/gifs/" + name + ".json", "r");
  if (f) {
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
      gifLayX = doc["x"] | 0;
      gifLayY = doc["y"] | 0;
      gifLayS = doc["s"] | 100;
      gifLayV = doc["v"] | 100;
    }
    f.close();
  }
  buildLayoutMap();
}

static void saveLayout() {
  String path = String("/gifs/") + settings.gif + ".json";
  if (gifLayX == 0 && gifLayY == 0 && gifLayS == 100 && gifLayV == 100) {
    LittleFS.remove(path);
    return;
  }
  File f = LittleFS.open(path, "w");
  if (!f) return;
  JsonDocument doc;
  doc["x"] = gifLayX;
  doc["y"] = gifLayY;
  doc["s"] = gifLayS;
  doc["v"] = gifLayV;
  serializeJson(doc, f);
  f.close();
}

// copia il fotogramma sullo schermo applicando spostamento e zoom
static void composeGif() {
  uint16_t* dst = (uint16_t*)canvas.getBuffer();
  if (mapIdentity) {
    for (int y = clipY0; y < clipY1; y++)
      memcpy(dst + y * SCREEN_W + clipX0, gifBuf + y * SCREEN_W + clipX0, (clipX1 - clipX0) * 2);
    return;
  }
  uint16_t bg = gifBuf[0];  // colore dell'angolo per riempire i bordi
  for (int y = clipY0; y < clipY1; y++) {
    int sy = mapY[y];
    uint16_t* row = dst + y * SCREEN_W;
    if (sy < 0) {
      for (int x = clipX0; x < clipX1; x++) row[x] = bg;
      continue;
    }
    const uint16_t* src = gifBuf + sy * SCREEN_W;
    for (int x = clipX0; x < clipX1; x++) row[x] = mapX[x] < 0 ? bg : src[mapX[x]];
  }
}

// zona dello schermo toccata dall'ultimo fotogramma (tenendo conto di spostamento e zoom)
static bool gifDirtyOnScreen(int& x0, int& y0, int& x1, int& y1) {
  if (dirtyX1 <= dirtyX0 || dirtyY1 <= dirtyY0) return false;
  if (mapIdentity) {
    x0 = dirtyX0, y0 = dirtyY0, x1 = dirtyX1, y1 = dirtyY1;
    return true;
  }
  float k = constrain((int)gifLayS, 20, 400) / 100.0f;
  x0 = (int)floorf(SCREEN_W / 2 + gifLayX + (dirtyX0 - SCREEN_W / 2) * k) - 2;
  x1 = (int)ceilf(SCREEN_W / 2 + gifLayX + (dirtyX1 - SCREEN_W / 2) * k) + 2;
  y0 = (int)floorf(SCREEN_H / 2 + gifLayY + (dirtyY0 - SCREEN_H / 2) * k) - 2;
  y1 = (int)ceilf(SCREEN_H / 2 + gifLayY + (dirtyY1 - SCREEN_H / 2) * k) + 2;
  x0 = max(0, x0), y0 = max(0, y0), x1 = min(SCREEN_W, x1), y1 = min(SCREEN_H, y1);
  return x1 > x0 && y1 > y0;
}

static bool loadGif() {
  gif.close();
  gifOk = false;
  if (gifData) {
    free(gifData);
    gifData = nullptr;
  }
  memset(gifBuf, 0, SCREEN_W * SCREEN_H * 2);

  String name = settings.gif;
  if (!LittleFS.exists("/gifs/" + name)) {
    name = firstGifName();
    if (name.isEmpty()) return false;
    strlcpy(settings.gif, name.c_str(), sizeof(settings.gif));
  }
  File f = LittleFS.open("/gifs/" + name, "r");
  if (!f) return false;
  size_t size = f.size();
  gifData = (uint8_t*)ps_malloc(size);
  if (!gifData) {
    f.close();
    return false;
  }
  f.read(gifData, size);
  f.close();

  if (!gif.open(gifData, size, GIFDraw)) {
    log_e("GIF non valida: %s", name.c_str());
    return false;
  }
  gifOffX = (SCREEN_W - gif.getCanvasWidth()) / 2;
  gifOffY = (SCREEN_H - gif.getCanvasHeight()) / 2;
  gifOk = true;
  nextFrameAt = millis();
  loadLayout(name);
  log_i("GIF caricata: %s (%u byte, %dx%d)", name.c_str(), size, gif.getCanvasWidth(), gif.getCanvasHeight());
  return true;
}

// avanza di un fotogramma; restituisce true se l'immagine è cambiata
static bool gifStep() {
  if (!gifOk || (int32_t)(millis() - nextFrameAt) < 0) return false;
  int delayMs = 100;
  dirtyX0 = SCREEN_W;
  dirtyY0 = SCREEN_H;
  dirtyX1 = dirtyY1 = 0;
  int r = gif.playFrame(false, &delayMs);
  if (r == 0) gif.reset();  // ultimo fotogramma: ricomincia
  if (r < 0) {
    if (gif.getLastError() != GIF_EMPTY_FRAME) {
      gifOk = false;
      return false;
    }
    gif.reset();
  }
  if (delayMs < 20) delayMs = 100;
  delayMs = delayMs * 100 / constrain((int)gifLayV, 25, 400);  // velocità scelta per questa GIF
  // il prossimo fotogramma parte dall'orario previsto, non da "adesso": così il tempo
  // speso a decodificare e disegnare non rallenta l'animazione
  nextFrameAt += delayMs;
  if ((int32_t)(millis() - nextFrameAt) > 300) nextFrameAt = millis();  // troppo indietro: riallinea
  return true;
}

// =====================================================================
//  Touch CST816D
// =====================================================================
static void touchBegin() {
  pinMode(PIN_TP_RST, OUTPUT);
  pinMode(PIN_TP_INT, INPUT_PULLUP);
  digitalWrite(PIN_TP_RST, LOW);
  delay(5);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(50);
  Wire1.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);
  // disattiva l'auto-sleep del controller così risponde sempre
  Wire1.beginTransmission(TP_ADDR);
  Wire1.write(0xFE);
  Wire1.write(0x01);
  Wire1.endTransmission();
}

static bool touchRead(int& x, int& y) {
  Wire1.beginTransmission(TP_ADDR);
  Wire1.write(0x02);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((uint8_t)TP_ADDR, (uint8_t)5) != 5) return false;
  uint8_t n = Wire1.read();
  uint8_t xh = Wire1.read(), xl = Wire1.read(), yh = Wire1.read(), yl = Wire1.read();
  if (n == 0xFF || (n & 0x0F) == 0) return false;
  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  if (TOUCH_FLIP_X) x = SCREEN_W - 1 - x;
  if (TOUCH_FLIP_Y) y = SCREEN_H - 1 - y;
  return true;
}

// =====================================================================
//  Accelerometro (opzionale): QMI8658 se presente sul bus I2C
// =====================================================================
static bool i2cReadReg(TwoWire& bus, uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len) {
  bus.beginTransmission(addr);
  bus.write(reg);
  if (bus.endTransmission(false) != 0) return false;
  if (bus.requestFrom(addr, len) != len) return false;
  for (int i = 0; i < len; i++) buf[i] = bus.read();
  return true;
}

static void i2cWriteReg(TwoWire& bus, uint8_t addr, uint8_t reg, uint8_t val) {
  bus.beginTransmission(addr);
  bus.write(reg);
  bus.write(val);
  bus.endTransmission();
}

static void accelProbe() {
  TwoWire* buses[] = {&Wire, &Wire1};
  const uint8_t addrs[] = {0x6B, 0x6A};
  for (auto* bus : buses)
    for (uint8_t a : addrs) {
      uint8_t id = 0;
      if (i2cReadReg(*bus, a, 0x00, &id, 1) && id == 0x05) {
        accelBus = bus;
        accelAddr = a;
        i2cWriteReg(*bus, a, 0x02, 0x40);  // CTRL1: auto-incremento indirizzi
        i2cWriteReg(*bus, a, 0x03, 0x15);  // CTRL2: ±4g
        i2cWriteReg(*bus, a, 0x08, 0x01);  // CTRL7: accelerometro attivo
        hasAccelerometer = true;
        log_i("Accelerometro QMI8658 trovato (0x%02X): scuoti per spegnere la sveglia", a);
        return;
      }
    }
  log_i("Nessun accelerometro: la sveglia si spegne con tocco lungo o tasto BOOT");
}

// true se il robot viene scosso
static bool accelShaken() {
  static uint32_t lastPoll = 0, windowStart = 0;
  static int hits = 0;
  if (!hasAccelerometer || millis() - lastPoll < 20) return false;
  lastPoll = millis();
  uint8_t b[6];
  if (!i2cReadReg(*accelBus, accelAddr, 0x35, b, 6)) return false;
  float ax = (int16_t)(b[0] | b[1] << 8) / 8192.0f;
  float ay = (int16_t)(b[2] | b[3] << 8) / 8192.0f;
  float az = (int16_t)(b[4] | b[5] << 8) / 8192.0f;
  float dev = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
  if (millis() - windowStart > 1500) {
    windowStart = millis();
    hits = 0;
  }
  if (dev > 0.8f) hits++;
  return hits >= 4;
}

// =====================================================================
//  Batteria
// =====================================================================
static void updateBattery() {
  static uint32_t last = 0, lastFast = 0;
  // stato del caricatore e dell'USB: controllato spesso, così l'icona cambia subito
  if (millis() - lastFast >= 500) {
    lastFast = millis();
    bool pin = digitalRead(PIN_CHARGING) == HIGH;
    charging = pin && batteryLevel < 100;
    // USB: il caricatore è attivo, oppure c'è un computer collegato, oppure la tensione
    // è al massimo (a batteria piena il caricatore si spegne ma l'USB la tiene a 4,2 V)
    usbPower = pin || HWCDC::isPlugged() || batteryAdc >= 2480;
  }
  if (last && millis() - last < 10000) return;
  last = millis();
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogRead(PIN_BAT_ADC);
  int adc = sum / 16;
  // tabella del firmware originale (ADC grezzo -> %)
  static const int tbl[][2] = {{1980, 0}, {2081, 20}, {2163, 40}, {2250, 60}, {2340, 80}, {2480, 100}};
  int lvl = 0;
  if (adc >= tbl[5][0]) lvl = 100;
  else if (adc > tbl[0][0])
    for (int i = 0; i < 5; i++)
      if (adc < tbl[i + 1][0]) {
        lvl = tbl[i][1] + (adc - tbl[i][0]) * (tbl[i + 1][1] - tbl[i][1]) / (tbl[i + 1][0] - tbl[i][0]);
        break;
      }
  batteryLevel = lvl;
  batteryAdc = adc;
}

// =====================================================================
//  Wi-Fi e portale di configurazione
// =====================================================================
void startPortal() {
  if (portalActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, strlen(AP_PASSWORD) ? AP_PASSWORD : nullptr);
  dns.start(53, "*", WiFi.softAPIP());
  portalActive = true;
  portalStartMs = millis();
  log_i("Portale attivo: rete %s, http://%s", AP_SSID, WiFi.softAPIP().toString().c_str());
}

static void stopPortal() {
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalActive = false;
  portalManual = false;
}

// nuovo tentativo "pulito": scollega e riparte da zero (più affidabile di reconnect())
static void wifiConnectAttempt() {
  if (wifiSsid.isEmpty()) return;
  WiFi.disconnect(false, false);
  delay(50);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
}

static void onWifiEvent(WiFiEvent_t ev, WiFiEventInfo_t info) {
  if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastDiscReason = info.wifi_sta_disconnected.reason;
    log_w("Wi-Fi disconnesso, motivo %u", lastDiscReason);
  } else if (ev == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    log_i("Wi-Fi connesso, IP %s", WiFi.localIP().toString().c_str());
  }
}

static void wifiMaintain() {
  static uint32_t lastRetry = 0, lostSince = 0;
  static bool wasConnected = false;
  bool connected = WiFi.status() == WL_CONNECTED;
  bool clients = portalActive && WiFi.softAPgetStationNum() > 0;
  if (portalActive) dns.processNextRequest();

  if (connected) {
    lostSince = 0;
    if (!wasConnected) {  // appena (ri)connesso: riavvia mDNS e aggiorna il meteo
      MDNS.end();
      MDNS.begin(MDNS_NAME);
      MDNS.addService("http", "tcp", 80);
      weatherRefreshRequested = true;
    }
    // chiude il portale se era automatico, o dopo 10 minuti se aperto a mano
    if (portalActive && !clients && (!portalManual || millis() - portalStartMs > 10UL * 60 * 1000)) stopPortal();
  } else if (wifiSsid.length() && !clients) {
    // non connesso: nuovo tentativo ogni 20 s (non mentre qualcuno usa il portale,
    // perché la ricerca della rete cambia canale e lo farebbe cadere)
    if (!lostSince) lostSince = millis();
    if (millis() - lostSince > 15000 && millis() - lastRetry > 20000) {
      lastRetry = millis();
      log_i("Nuovo tentativo di connessione a %s", wifiSsid.c_str());
      wifiConnectAttempt();
    }
    // dopo 3 minuti senza rete attiva anche il portale, per poter cambiare rete/password
    if (!portalActive && millis() - lostSince > 3UL * 60 * 1000) startPortal();
  }
  wasConnected = connected;
}

// =====================================================================
//  Ora senza internet: conservata tra i riavvii
// =====================================================================
// all'avvio: se è un riavvio "morbido" ripristina l'ora salvata; restituisce il fuso orario salvato
static const char* restoreTimeAfterReset() {
  esp_reset_reason_t r = esp_reset_reason();
  bool soft = r == ESP_RST_SW || r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
              r == ESP_RST_WDT;
  if (!soft || rtcKeep.magic != RTC_KEEP_MAGIC) return nullptr;
  int64_t elapsed = (int64_t)(esp_rtc_get_time_us() - rtcKeep.rtcUs);
  if (elapsed < 0 || elapsed > 86400LL * 1000000) return nullptr;
  timeval tv;
  tv.tv_sec = (time_t)(rtcKeep.epoch + elapsed / 1000000);
  tv.tv_usec = (suseconds_t)(elapsed % 1000000);
  settimeofday(&tv, nullptr);
  timeSource = TIME_RESTART;
  rtcKeep.tz[sizeof(rtcKeep.tz) - 1] = 0;
  log_i("Ora ripristinata dopo il riavvio (%lld s di pausa)", elapsed / 1000000);
  return rtcKeep.tz[0] ? rtcKeep.tz : nullptr;
}

// durante il funzionamento: aggiorna ogni secondo la copia nella memoria RTC
static void keepTimeForRestart() {
  static uint32_t last = 0;
  if (millis() - last < 1000 || !timeValid()) return;
  last = millis();
  timeval tv;
  gettimeofday(&tv, nullptr);
  rtcKeep.epoch = tv.tv_sec;
  rtcKeep.rtcUs = esp_rtc_get_time_us() - tv.tv_usec;
  const char* tz = getenv("TZ");
  strlcpy(rtcKeep.tz, tz ? tz : "", sizeof(rtcKeep.tz));
  rtcKeep.magic = RTC_KEEP_MAGIC;
}

// =====================================================================
//  Sveglie
// =====================================================================
// prossima sveglia: minuti mancanti (-1 se nessuna), indice e giorni di distanza
static int nextAlarm(const tm& now, int& idx, int& dayOffset) {
  int best = -1;
  int nowMin = now.tm_hour * 60 + now.tm_min;
  for (int i = 0; i < MAX_ALARMS; i++) {
    const Alarm& a = alarms[i];
    if (!a.enabled) continue;
    for (int d = 0; d < 8; d++) {
      int wday = (now.tm_wday + d) % 7;
      int bit = (wday + 6) % 7;  // lun=0 ... dom=6
      if (a.days && !(a.days >> bit & 1)) continue;
      int mins = d * 1440 + a.hour * 60 + a.minute - nowMin;
      if (mins <= 0) continue;
      if (best < 0 || mins < best) {
        best = mins;
        idx = i;
        dayOffset = d;
      }
      break;
    }
  }
  return best;
}

static void startRing(int idx) {
  ringState = RING_ACTIVE;
  ringingAlarm = idx;
  ringStartMs = millis();
  page = PAGE_CLOCK;
  audioStart();
  log_i("Sveglia %d!", idx);
}

static void snooze() {
  audioStop();
  ringState = RING_SNOOZED;
  snoozeUntil = time(nullptr) + settings.snoozeMin * 60;
  testStopAt = 0;
  char b[40];
  snprintf(b, sizeof(b), "Posticipata di %d min", settings.snoozeMin);
  showToast(b);
}

static void stopRing() {
  audioStop();
  ringState = RING_IDLE;
  ringingAlarm = -1;
  testStopAt = 0;
  showToast("Buongiorno!");
}

static void alarmLogic() {
  static int lastMinute = -1;
  if (testAlarmRequested) {
    testAlarmRequested = false;
    startRing(-1);
    testStopAt = millis() + 10000;
  }
  if (stopAlarmRequested) {
    stopAlarmRequested = false;
    if (ringState != RING_IDLE) stopRing();
  }
  if (testStopAt && ringState == RING_ACTIVE && (int32_t)(millis() - testStopAt) >= 0) {
    testStopAt = 0;
    audioStop();
    ringState = RING_IDLE;
  }
  if (ringState == RING_ACTIVE && millis() - ringStartMs > settings.ringMaxMin * 60000UL) stopRing();
  if (ringState == RING_SNOOZED && time(nullptr) >= snoozeUntil) startRing(ringingAlarm);

  if (!timeValid()) return;
  time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  if (t.tm_min == lastMinute) return;
  bool firstCheck = lastMinute < 0;
  lastMinute = t.tm_min;
  if (firstCheck || ringState != RING_IDLE) return;  // niente sveglia al minuto di accensione

  int bit = (t.tm_wday + 6) % 7;
  for (int i = 0; i < MAX_ALARMS; i++) {
    Alarm& a = alarms[i];
    if (!a.enabled || a.hour != t.tm_hour || a.minute != t.tm_min) continue;
    if (a.days && !(a.days >> bit & 1)) continue;
    if (!a.days) {  // sveglia singola: si disattiva dopo aver suonato
      a.enabled = false;
      saveConfig();
    }
    startRing(i);
    break;
  }
}

// =====================================================================
//  Disegno
// =====================================================================
static void txt(const char* s, int x, int y, const lgfx::IFont* f, uint16_t col,
                textdatum_t d = textdatum_t::middle_center) {
  canvas.setFont(f);
  canvas.setTextColor(col);
  canvas.setTextDatum(d);
  canvas.drawString(s, x, y);
}

// fonde un rettangolo arrotondato con un colore (alpha 0-255) direttamente nel buffer
static void blendRect(int x0, int y0, int w, int h, int r, uint16_t col, uint8_t alpha) {
  uint16_t* buf = (uint16_t*)canvas.getBuffer();
  int cr = col >> 11, cg = (col >> 5) & 63, cb = col & 31;
  int a256 = alpha + 1;
  for (int y = max(clipY0, y0); y < min(clipY1, y0 + h); y++) {
    for (int x = max(clipX0, x0); x < min(clipX1, x0 + w); x++) {
      int dx = x < x0 + r ? x0 + r - x : (x >= x0 + w - r ? x - (x0 + w - r - 1) : 0);
      int dy = y < y0 + r ? y0 + r - y : (y >= y0 + h - r ? y - (y0 + h - r - 1) : 0);
      if (dx && dy && dx * dx + dy * dy > r * r) continue;
      uint16_t v = buf[y * SCREEN_W + x];
      v = (v >> 8) | (v << 8);
      int pr = v >> 11, pg = (v >> 5) & 63, pb = v & 31;
      pr += ((cr - pr) * a256) >> 8;
      pg += ((cg - pg) * a256) >> 8;
      pb += ((cb - pb) * a256) >> 8;
      v = (pr << 11) | (pg << 5) | pb;
      buf[y * SCREEN_W + x] = (v >> 8) | (v << 8);
    }
  }
}

static void drawBell(int x, int y, uint16_t col, bool off, int s = 1) {
  canvas.fillCircle(x, y - 2 * s, 6 * s, col);
  canvas.fillRect(x - 6 * s, y - 2 * s, 12 * s + 1, 7 * s, col);
  canvas.fillRoundRect(x - 9 * s, y + 4 * s, 18 * s + 1, 3 * s, s, col);
  canvas.fillCircle(x, y + 9 * s, 2 * s, col);
  canvas.fillRect(x - s, y - 10 * s, 2 * s + 1, 3 * s, col);
  if (off) {
    for (int k = -1; k <= 1; k++) canvas.drawLine(x - 10 * s + k, y - 10 * s, x + 10 * s + k, y + 10 * s, C_RED);
  }
}

static void drawBolt(int x, int y, uint16_t col) {  // fulmine centrato in (x, y), alto 13 px
  canvas.fillTriangle(x + 2, y - 7, x - 4, y + 1, x + 1, y + 1, C_BLACK);
  canvas.fillTriangle(x - 2, y + 7, x + 4, y - 1, x - 1, y - 1, C_BLACK);
  canvas.fillTriangle(x + 1, y - 6, x - 3, y + 1, x + 1, y + 1, col);
  canvas.fillTriangle(x - 1, y + 6, x + 3, y - 1, x - 1, y - 1, col);
}

static void drawBattery(int x, int y) {
  if (batteryLevel < 0) return;
  canvas.drawRoundRect(x, y - 5, 20, 11, 2, C_WHITE);
  canvas.fillRect(x + 20, y - 2, 2, 5, C_WHITE);
  if (charging) {
    // in carica: il livello si riempie a ripetizione, con il fulmine
    int base = max(1, 16 * batteryLevel / 100);
    int w = base + (int)((millis() / 400) % 5) * (16 - base) / 4;
    canvas.fillRect(x + 2, y - 3, w, 7, C_GREEN);
    drawBolt(x + 10, y, C_YELLOW);
  } else if (usbPower) {
    // collegata all'USB e carica: piena con il fulmine
    canvas.fillRect(x + 2, y - 3, 16, 7, C_GREEN);
    drawBolt(x + 10, y, C_YELLOW);
  } else {
    uint16_t col = batteryLevel > 40 ? C_GREEN : batteryLevel > 15 ? C_YELLOW : C_RED;
    canvas.fillRect(x + 2, y - 3, max(1, 16 * batteryLevel / 100), 7, col);
  }
}

bool batteryInfo(int& level, bool& isCharging, bool& onUsb) {
  level = batteryLevel;
  isCharging = charging;
  onUsb = usbPower;
  return batteryLevel >= 0;
}

// gradi: numero + cerchietto
static void drawTemp(float v, int x, int y, const lgfx::IFont* f, uint16_t col) {
  char b[8];
  snprintf(b, sizeof(b), "%d", (int)lroundf(v));
  txt(b, x - 3, y, f, col);
  canvas.setFont(f);
  int w = canvas.textWidth(b), h = canvas.fontHeight();
  canvas.drawCircle(x - 3 + w / 2 + 5, y - h / 3, 3, col);
}

static void drawSun(int x, int y, int r) {
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    canvas.drawWideLine(x + cosf(a) * (r + 4), y + sinf(a) * (r + 4), x + cosf(a) * (r + 9), y + sinf(a) * (r + 9), 1.5f,
                        C_YELLOW);
  }
  canvas.fillCircle(x, y, r, C_YELLOW);
}

static void drawCloud(int x, int y, int s, uint16_t col) {
  canvas.fillCircle(x - s * 45 / 100, y + s * 10 / 100, s * 35 / 100, col);
  canvas.fillCircle(x, y - s * 15 / 100, s * 45 / 100, col);
  canvas.fillCircle(x + s * 45 / 100, y + s * 10 / 100, s * 33 / 100, col);
  canvas.fillRoundRect(x - s * 80 / 100, y + s * 5 / 100, s * 160 / 100, s * 42 / 100, s * 20 / 100, col);
}

static void drawWeatherIcon(int x, int y, int code) {
  const uint16_t cloud = rgb(225, 230, 240), dark = rgb(140, 145, 160);
  if (code < 0) {
    txt("?", x, y, &fonts::FreeSansBold18pt7b, C_GRAY);
  } else if (code == 0) {
    drawSun(x, y, 14);
  } else if (code <= 2) {
    drawSun(x + 9, y - 8, 11);
    drawCloud(x - 4, y + 6, 24, cloud);
  } else if (code == 3) {
    drawCloud(x, y, 30, cloud);
  } else if (code == 45 || code == 48) {
    for (int i = 0; i < 4; i++) canvas.fillRoundRect(x - 22 + (i & 1) * 6, y - 14 + i * 9, 38, 4, 2, cloud);
  } else if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    drawCloud(x, y - 6, 26, cloud);
    for (int i = 0; i < 3; i++) canvas.fillCircle(x - 12 + i * 12, y + 18 + (i & 1) * 4, 3, C_WHITE);
  } else if (code >= 95) {
    drawCloud(x, y - 6, 26, dark);
    canvas.fillTriangle(x + 2, y + 2, x - 8, y + 16, x, y + 16, C_YELLOW);
    canvas.fillTriangle(x - 2, y + 14, x + 6, y + 14, x - 4, y + 28, C_YELLOW);
  } else {  // pioggia / pioggerella / rovesci
    drawCloud(x, y - 6, 26, code >= 80 ? dark : cloud);
    for (int i = 0; i < 3; i++) canvas.drawWideLine(x - 10 + i * 10, y + 13, x - 14 + i * 10, y + 24, 1.5f, C_CYAN);
  }
}

static const char* weatherText(int code) {
  if (code < 0) return "";
  if (code == 0) return "Sereno";
  if (code <= 2) return "Poco nuvoloso";
  if (code == 3) return "Nuvoloso";
  if (code == 45 || code == 48) return "Nebbia";
  if (code <= 57) return "Pioggerella";
  if (code <= 67) return "Pioggia";
  if (code <= 77) return "Neve";
  if (code <= 82) return "Rovesci";
  if (code <= 86) return "Neve";
  return "Temporale";
}

static void drawPokeball(int x, int y, int r) {
  canvas.fillCircle(x, y, r, C_WHITE);
  canvas.fillArc(x, y, r, 0, 180, 360, C_RED);
  canvas.fillRect(x - r, y - r / 9, 2 * r + 1, r * 2 / 9 + 1, C_BLACK);
  canvas.fillCircle(x, y, r * 3 / 10, C_BLACK);
  canvas.fillCircle(x, y, r * 2 / 10, C_WHITE);
  canvas.drawCircle(x, y, r, C_BLACK);
}

// ------------------------------------------------------------- pagine
static void drawClockPage(const tm* t) {
  if (gifOk) {
    composeGif();
  } else {
    canvas.fillScreen(C_NAVY);
    drawPokeball(120, 95, 45);
    txt("Nessuna GIF", 120, 152, &fonts::FreeSans9pt7b, C_GRAY);
  }

  bool ringing = ringState == RING_ACTIVE;
  bool blink = (millis() / 500) & 1;

  // --- barra superiore: icona sveglia + prossimo orario + batteria
  blendRect(40, 20, 160, 28, 14, C_BLACK, 150);
  char nb[24] = "OFF";
  bool any = false;
  if (ringState == RING_SNOOZED) {
    tm st;
    localtime_r(&snoozeUntil, &st);
    snprintf(nb, sizeof(nb), "Zz %02d:%02d", st.tm_hour, st.tm_min);
    any = true;
  } else if (t) {
    int idx = 0, dOff = 0;
    if (nextAlarm(*t, idx, dOff) >= 0) {
      any = true;
      if (dOff == 0)
        snprintf(nb, sizeof(nb), "%02d:%02d", alarms[idx].hour, alarms[idx].minute);
      else
        snprintf(nb, sizeof(nb), "%s %02d:%02d", DOW[(t->tm_wday + dOff) % 7], alarms[idx].hour, alarms[idx].minute);
    }
  }
  drawBell(58, 34, any ? C_YELLOW : C_GRAY, !any);
  txt(nb, 116, 35, &fonts::FreeSansBold9pt7b, any ? C_WHITE : C_GRAY);
  drawBattery(166, 34);

  // --- fascia inferiore: ora e data
  if (ringing)
    blendRect(30, 156, 180, 78, 20, C_RED, blink ? 210 : 140);
  else
    blendRect(30, 156, 180, 78, 20, C_BLACK, 150);

  // ore e minuti disegnati separatamente: i due punti lampeggiano senza spostare le cifre
  char hh[4] = "--", mm[4] = "--";
  if (t) {
    snprintf(hh, sizeof(hh), "%02d", t->tm_hour);
    snprintf(mm, sizeof(mm), "%02d", t->tm_min);
  }
  txt(hh, 110, 182, &fonts::FreeSansBold24pt7b, C_WHITE, textdatum_t::middle_right);
  txt(mm, 130, 182, &fonts::FreeSansBold24pt7b, C_WHITE, textdatum_t::middle_left);
  if (!t || !(t->tm_sec & 1)) {
    canvas.fillCircle(120, 175, 3, C_WHITE);
    canvas.fillCircle(120, 190, 3, C_WHITE);
  }

  char db[32];
  if (ringing) {
    const char* lab = ringingAlarm >= 0 && alarms[ringingAlarm].label[0] ? alarms[ringingAlarm].label : "SVEGLIA!";
    if ((millis() / 2000) % 3 == 0)
      strlcpy(db, lab, sizeof(db));
    else if ((millis() / 2000) % 3 == 1)
      strlcpy(db, "Tocca: posticipa", sizeof(db));
    else
      strlcpy(db, "Tieni: spegni", sizeof(db));
  } else if (!t) {
    strlcpy(db, WiFi.status() == WL_CONNECTED ? "Sincronizzo l'ora..." : "Wi-Fi assente", sizeof(db));
  } else if (WiFi.status() != WL_CONNECTED && (millis() / 3000) & 1) {
    strlcpy(db, "Wi-Fi assente", sizeof(db));
  } else {
    snprintf(db, sizeof(db), "%s %d %s", DOW[t->tm_wday], t->tm_mday, MON[t->tm_mon]);
  }
  txt(db, 120, 214, &fonts::FreeSansBold9pt7b, ringing ? C_WHITE : C_YELLOW);

  if (ringing) {  // campanella che oscilla al centro in alto
    int dx = blink ? -3 : 3;
    blendRect(95, 56, 50, 50, 25, C_BLACK, 120);
    drawBell(120 + dx, 82, C_YELLOW, false, 2);
  }
}

static void drawWeatherPage() {
  canvas.fillScreen(C_NAVY);
  canvas.fillArc(120, 120, 119, 116, 0, 360, C_BLUE);
  WeatherData w;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  w = weather;
  xSemaphoreGive(dataMutex);

  if (!w.valid) {
    drawPokeball(120, 100, 32);
    txt(WiFi.status() == WL_CONNECTED ? "Meteo in arrivo..." : "Wi-Fi assente", 120, 158, &fonts::FreeSans9pt7b, C_WHITE);
    return;
  }
  char city[20];
  strlcpy(city, w.city[0] ? w.city : "Meteo", sizeof(city));
  txt(city, 120, 30, &fonts::FreeSansBold9pt7b, C_YELLOW);
  char now[32];
  snprintf(now, sizeof(now), "Ora %d   %s", (int)lroundf(w.tempNow), weatherText(w.codeNow));
  txt(now, 120, 52, &fonts::FreeSans9pt7b, C_WHITE);
  canvas.setFont(&fonts::FreeSans9pt7b);
  {  // simbolo dei gradi dopo la temperatura attuale
    char tb[8];
    snprintf(tb, sizeof(tb), "Ora %d", (int)lroundf(w.tempNow));
    int x = 120 - canvas.textWidth(now) / 2 + canvas.textWidth(tb) + 4;
    canvas.drawCircle(x, 47, 2, C_WHITE);
  }

  canvas.drawFastVLine(120, 72, 130, C_DGRAY);
  const char* hdr[] = {"OGGI", "DOMANI"};
  for (int i = 0; i < 2; i++) {
    int cx = i == 0 ? 72 : 168;
    txt(hdr[i], cx, 80, &fonts::FreeSansBold9pt7b, C_CYAN);
    drawWeatherIcon(cx, 112, w.day[i].code);
    drawTemp(w.day[i].tmax, cx, 154, &fonts::FreeSansBold12pt7b, C_ORANGE);
    drawTemp(w.day[i].tmin, cx, 178, &fonts::FreeSans9pt7b, C_CYAN);
    char rb[8];
    snprintf(rb, sizeof(rb), "%d%%", w.day[i].rain);
    canvas.fillCircle(cx - 16, 200, 4, C_CYAN);
    canvas.fillTriangle(cx - 20, 199, cx - 12, 199, cx - 16, 192, C_CYAN);
    txt(rb, cx + 4, 199, &fonts::FreeSans9pt7b, C_WHITE);
  }
  if (w.updated) {
    tm u;
    localtime_r(&w.updated, &u);
    char ub[24];
    snprintf(ub, sizeof(ub), "agg. %02d:%02d", u.tm_hour, u.tm_min);
    txt(ub, 120, 222, &fonts::Font0, C_GRAY);
  }
}

// riga della lista sveglie toccata (o -1)
static int alarmRowAt(int y) {
  int r = (y - 49) / 30;
  return (y >= 49 && r >= 0 && r < MAX_ALARMS) ? r : -1;
}

static void drawAlarmsPage() {
  canvas.fillScreen(rgb(24, 24, 40));
  canvas.fillArc(120, 120, 119, 116, 0, 360, C_RED);
  txt("SVEGLIE", 120, 30, &fonts::FreeSansBold12pt7b, C_YELLOW);
  static const char* D = "LMMGVSD";
  for (int i = 0; i < MAX_ALARMS; i++) {
    const Alarm& a = alarms[i];
    int y = 64 + i * 30;
    char hb[8];
    snprintf(hb, sizeof(hb), "%02d:%02d", a.hour, a.minute);
    txt(hb, 32, y, &fonts::FreeSansBold12pt7b, a.enabled ? C_WHITE : C_GRAY, textdatum_t::middle_left);
    canvas.setFont(&fonts::Font0);
    canvas.setTextDatum(textdatum_t::middle_center);
    if (a.days == 0) {
      canvas.setTextColor(a.enabled ? C_CYAN : C_DGRAY);
      canvas.drawString("una volta", 143, y);
    } else {
      for (int d = 0; d < 7; d++) {
        canvas.setTextColor((a.days >> d & 1) ? (a.enabled ? C_CYAN : C_GRAY) : C_DGRAY);
        char s[2] = {D[d], 0};
        canvas.drawString(s, 113 + d * 9, y);
      }
    }
    // interruttore
    int sx = 178;
    canvas.fillRoundRect(sx, y - 8, 30, 16, 8, a.enabled ? C_GREEN : C_DGRAY);
    canvas.fillCircle(a.enabled ? sx + 22 : sx + 8, y, 6, C_WHITE);
  }
  char ib[40];
  if (portalActive && WiFi.status() != WL_CONNECTED)
    snprintf(ib, sizeof(ib), "Wi-Fi: %s", AP_SSID);
  else if (WiFi.status() == WL_CONNECTED)
    snprintf(ib, sizeof(ib), "http://%s", ipString().c_str());
  else if (lastDiscReason == 201)
    strlcpy(ib, "Wi-Fi: rete non trovata", sizeof(ib));
  else if (lastDiscReason == 202 || lastDiscReason == 15 || lastDiscReason == 204)
    strlcpy(ib, "Wi-Fi: password errata?", sizeof(ib));
  else
    strlcpy(ib, "Wi-Fi: riconnessione...", sizeof(ib));
  txt(ib, 120, 216, &fonts::Font2, C_CYAN);
}

static void drawSetupPage() {
  canvas.fillScreen(C_NAVY);
  canvas.fillArc(120, 120, 119, 116, 0, 360, C_YELLOW);
  drawPokeball(120, 50, 24);
  txt("Configura il Wi-Fi", 120, 92, &fonts::FreeSansBold9pt7b, C_YELLOW);
  txt("Collegati alla rete:", 120, 118, &fonts::FreeSans9pt7b, C_WHITE);
  txt(AP_SSID, 120, 140, &fonts::FreeSansBold9pt7b, C_CYAN);
  if (strlen(AP_PASSWORD)) {
    char pb[40];
    snprintf(pb, sizeof(pb), "password: %s", AP_PASSWORD);
    txt(pb, 120, 162, &fonts::FreeSans9pt7b, C_WHITE);
  }
  txt("poi apri 192.168.4.1", 120, 190, &fonts::FreeSans9pt7b, C_WHITE);
}

// i font FreeSans contengono solo ASCII: le lettere accentate diventano "a'", "e'"...
static String asciiFold(const char* s) {
  String out;
  for (const uint8_t* p = (const uint8_t*)s; *p; p++) {
    if (*p < 0x80) {
      out += (char)*p;
    } else if (*p == 0xC3 && p[1]) {  // Latin-1: à è é ì ò ù ...
      uint8_t c = p[1] + 0x40;
      p++;
      const char* base = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
      char b = base[c - 0xC0];
      out += b;
      if (strchr("AEIOUaeiou", b)) out += '\'';
    } else if (*p == 0xE2 && p[1] == 0x80 && (p[2] == 0x98 || p[2] == 0x99)) {  // apostrofi tipografici
      out += '\'';
      p += 2;
    } else if ((*p & 0xC0) == 0xC0) {
      out += '?';  // altri caratteri (emoji...): saltati
      while ((p[1] & 0xC0) == 0x80) p++;
    }
  }
  return out;
}

static void drawEnvelope(int x, int y, uint16_t col) {
  canvas.drawRoundRect(x - 13, y - 9, 27, 19, 3, col);
  canvas.drawLine(x - 12, y - 8, x, y + 2, col);
  canvas.drawLine(x + 12, y - 8, x, y + 2, col);
}

static void drawMessagePage(const char* text, uint32_t since) {
  canvas.fillScreen(C_NAVY);
  canvas.fillArc(120, 120, 119, 116, 0, 360, C_YELLOW);
  drawEnvelope(120, 30, C_YELLOW);
  txt("MESSAGGIO", 120, 54, &fonts::FreeSansBold9pt7b, C_YELLOW);

  // a capo per parole, larghezza fissa
  String s = asciiFold(text);
  canvas.setFont(&fonts::FreeSans9pt7b);
  const int W = 184, LH = 21, VISIBLE = 6;
  static String lines[40];
  int nl = 0;
  String line;
  int i = 0;
  while (i <= (int)s.length() && nl < 40) {
    int j = i;
    while (j < (int)s.length() && s[j] != ' ' && s[j] != '\n') j++;
    String word = s.substring(i, j);
    String cand = line.length() ? line + " " + word : word;
    if (canvas.textWidth(cand) > W && line.length()) {
      lines[nl++] = line;
      line = word;
    } else {
      line = cand;
    }
    if (j < (int)s.length() && s[j] == '\n' && nl < 40) {
      lines[nl++] = line;
      line = "";
    }
    i = j + 1;
  }
  if (line.length() && nl < 40) lines[nl++] = line;

  int first = 0;
  if (nl > VISIBLE) {  // testo lungo: scorre una riga ogni 1,8 s
    int steps = nl - VISIBLE + 2;
    first = min(nl - VISIBLE, (int)((millis() - since) / 1800) % steps);
  }
  int shown = min(VISIBLE, nl - first);
  int y0 = 126 - (shown - 1) * LH / 2;
  for (int k = 0; k < shown; k++) txt(lines[first + k].c_str(), 120, y0 + k * LH, &fonts::FreeSans9pt7b, C_WHITE);

  const char* st = speechStatus();
  const char* foot = strcmp(st, "download") == 0 ? "preparo la voce..." : strncmp(st, "errore", 6) == 0 ? "voce non disponibile" : "tocca per chiudere";
  txt(foot, 120, 214, &fonts::Font2, C_CYAN);
}

static void drawToast() {
  if (!toastMsg[0] || (int32_t)(millis() - toastUntil) >= 0) return;
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  int w = min(200, (int)canvas.textWidth(toastMsg) + 24);
  blendRect(120 - w / 2, 104, w, 32, 16, C_BLACK, 210);
  txt(toastMsg, 120, 120, &fonts::FreeSansBold9pt7b, C_WHITE);
}

// partial = true: ridisegna e invia allo schermo solo la zona cambiata della GIF
static void render(bool partial = false) {
  if (partial) {
    int x0, y0, x1, y1;
    if (!gifDirtyOnScreen(x0, y0, x1, y1)) return;
    clipX0 = x0, clipY0 = y0, clipX1 = x1, clipY1 = y1;
    canvas.setClipRect(x0, y0, x1 - x0, y1 - y0);
  }
  tm tt;
  tm* t = nullptr;
  if (timeValid()) {
    time_t now = time(nullptr);
    localtime_r(&now, &tt);
    t = &tt;
  }
  static char msg[640];
  uint32_t msgSince;
  bool hasMsg = ringState != RING_ACTIVE && speechGetCurrent(msg, sizeof(msg), msgSince);
  if (hasMsg)
    drawMessagePage(msg, msgSince);
  else if (!t && portalActive && page == PAGE_CLOCK && ringState == RING_IDLE)
    drawSetupPage();
  else if (page == PAGE_WEATHER)
    drawWeatherPage();
  else if (page == PAGE_ALARMS)
    drawAlarmsPage();
  else
    drawClockPage(t);
  drawToast();
  lcd.startWrite();
  if (partial) lcd.setClipRect(clipX0, clipY0, clipX1 - clipX0, clipY1 - clipY0);
  canvas.pushSprite(0, 0);
  lcd.clearClipRect();
  lcd.endWrite();
  if (partial) {
    canvas.clearClipRect();
    clipX0 = clipY0 = 0, clipX1 = SCREEN_W, clipY1 = SCREEN_H;
  }
}

static void drawBootScreen(const char* line1, const char* line2) {
  canvas.fillScreen(C_NAVY);
  drawPokeball(120, 90, 36);
  txt(line1, 120, 150, &fonts::FreeSansBold9pt7b, C_YELLOW);
  txt(line2, 120, 175, &fonts::FreeSans9pt7b, C_WHITE);
  canvas.pushSprite(0, 0);
}

// =====================================================================
//  Luminosità
// =====================================================================
static void updateBacklight() {
  static int lastB = -1;
  int pct = settings.brightDay;
  if (timeValid() && settings.nightStart != settings.nightEnd) {
    time_t now = time(nullptr);
    tm t;
    localtime_r(&now, &t);
    int h = t.tm_hour, s = settings.nightStart, e = settings.nightEnd;
    bool night = s < e ? (h >= s && h < e) : (h >= s || h < e);
    if (night && (int32_t)(millis() - wakeUntil) >= 0) pct = settings.brightNight;
  }
  if (ringState == RING_ACTIVE) pct = 100;
  if (audioIsSpeaking()) pct = max(pct, (int)settings.brightDay);  // messaggio: schermo acceso
  int b = pct * 255 / 100;
  if (b != lastB) {
    lcd.setBrightness(b);
    lastB = b;
  }
}

// =====================================================================
//  Input: touch e tasto BOOT
// =====================================================================
static void onTap(int x, int y) {
  uint32_t since;
  static char tmp[4];
  if (ringState != RING_ACTIVE && speechGetCurrent(tmp, sizeof(tmp), since)) {
    speechDismiss();  // tocco sul messaggio: chiude e interrompe la voce
    return;
  }
  if (ringState == RING_ACTIVE) {
    snooze();
    return;
  }
  if (page == PAGE_ALARMS) {
    int r = alarmRowAt(y);
    if (r >= 0) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      alarms[r].enabled = !alarms[r].enabled;
      xSemaphoreGive(dataMutex);
      saveConfig();
    }
  }
}

static void onLongPress() {
  if (ringState != RING_IDLE) stopRing();
}

static void onSwipe(int dir) {  // +1 = dito verso destra
  if (ringState == RING_ACTIVE) return;
  page = constrain(page - dir, (int)PAGE_WEATHER, (int)PAGE_ALARMS);
}

static void pollTouch() {
  static uint32_t lastPoll = 0, t0 = 0;
  static bool down = false, longFired = false;
  static int sx, sy, lx, ly;
  if (millis() - lastPoll < 15) return;
  lastPoll = millis();
  int x, y;
  if (touchRead(x, y)) {
    if (!down) {
      down = true;
      longFired = false;
      sx = lx = x;
      sy = ly = y;
      t0 = millis();
      lastInteraction = millis();
      wakeUntil = millis() + 15000;  // di notte il tocco riaccende lo schermo per 15 s
    }
    lx = x;
    ly = y;
    if (!longFired && millis() - t0 > 1200 && abs(lx - sx) < 30 && abs(ly - sy) < 30) {
      longFired = true;
      onLongPress();
    }
  } else if (down) {
    down = false;
    lastInteraction = millis();
    if (longFired) return;
    int dx = lx - sx, dy = ly - sy;
    if (abs(dx) > 45 && abs(dx) > abs(dy))
      onSwipe(dx > 0 ? 1 : -1);
    else if (abs(dx) < 25 && abs(dy) < 25)
      onTap(sx, sy);
  }
}

static void pollButton() {
  static bool was = false, longDone = false;
  static uint32_t t0 = 0;
  bool pressed = digitalRead(PIN_BOOT_BTN) == LOW;
  if (pressed && !was) {
    t0 = millis();
    longDone = false;
    wakeUntil = millis() + 15000;
  }
  if (pressed && !longDone && millis() - t0 > 3000) {
    longDone = true;
    if (ringState == RING_IDLE) {
      portalManual = true;
      startPortal();
      showToast("Rete Setup attiva", 4000);
      page = PAGE_ALARMS;
      lastInteraction = millis();
    }
  }
  if (!pressed && was && !longDone && millis() - t0 > 40) {
    if (ringState != RING_IDLE)
      stopRing();
    else
      page = PAGE_CLOCK;
  }
  was = pressed;
}

// =====================================================================
//  Setup / loop
// =====================================================================
void setup() {
  // mantiene accesa l'alimentazione a batteria
  pinMode(PIN_POWER_HOLD, OUTPUT);
  digitalWrite(PIN_POWER_HOLD, HIGH);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);
  pinMode(PIN_CHARGING, INPUT_PULLUP);
  Serial.begin(115200);
  dataMutex = xSemaphoreCreateMutex();

  lcd.init();
  lcd.setRotation(LCD_ROTATION);
  lcd.setBrightness(0);
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvas.createSprite(SCREEN_W, SCREEN_H);
  gifBuf = (uint16_t*)ps_malloc(SCREEN_W * SCREEN_H * 2);
  drawBootScreen("PokeSveglia", "Avvio...");
  lcd.setBrightness(200);

  if (!LittleFS.begin(true)) log_e("LittleFS non disponibile");
  LittleFS.mkdir("/gifs");
  loadConfig();

  Wire.begin(PIN_CODEC_SDA, PIN_CODEC_SCL, 100000);
  touchBegin();
  audioBegin();
  accelProbe();
  updateBattery();

  gif.begin(GIF_PALETTE_RGB565_BE);
  loadGif();

  // --- Wi-Fi
  prefs.begin("wifi", true);
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  prefs.end();
  WiFi.persistent(false);
  WiFi.onEvent(onWifiEvent);
  WiFi.setHostname(MDNS_NAME);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // connessione più stabile (la sveglia è quasi sempre alimentata)
  WiFi.setAutoReconnect(true);
  if (wifiSsid.length()) {
    // fino a 3 tentativi: dopo una lunga pausa spesso il primo fallisce
    // (router lento a rispondere o handshake scaduto)
    for (int attempt = 1; attempt <= 3 && WiFi.status() != WL_CONNECTED; attempt++) {
      char l2[48];
      snprintf(l2, sizeof(l2), "%.28s (%d/3)", wifiSsid.c_str(), attempt);
      drawBootScreen("Connessione a", l2);
      if (attempt == 1)
        WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
      else
        wifiConnectAttempt();
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);
    }
    if (WiFi.status() != WL_CONNECTED) log_w("Connessione non riuscita (motivo %u): continuo a riprovare", lastDiscReason);
  } else {
    startPortal();  // nessuna rete salvata: serve la configurazione
  }

  // ora da internet (il fuso orario si aggiorna da solo in base alla posizione)
  const char* savedTz = restoreTimeAfterReset();
  sntp_set_time_sync_notification_cb([](timeval*) { timeSource = TIME_NTP; });
  configTzTime(savedTz ? savedTz : "CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com", "time.inrim.it");
  webBegin();
  netTaskStart();
  speechTaskStart();
  lastInteraction = millis();
}

void loop() {
  static uint32_t lastRender = 0;
  webLoop();
  wifiMaintain();
  pollButton();
  pollTouch();
  if (ringState == RING_ACTIVE && accelShaken()) stopRing();
  alarmLogic();
  keepTimeForRestart();
  updateBattery();
  updateBacklight();

  if (gifReloadRequested) {
    gifReloadRequested = false;
    loadGif();
  }
  // spostamento/zoom dalla pagina web: anteprima immediata, salvataggio dopo 2 s di pausa
  static uint32_t layoutSaveAt = 0;
  bool layoutRedraw = false;
  if (layoutChanged) {
    layoutChanged = false;
    buildLayoutMap();
    page = PAGE_CLOCK;
    lastInteraction = millis();
    layoutRedraw = true;
    layoutSaveAt = millis() + 2000;
  }
  if (layoutSaveAt && (int32_t)(millis() - layoutSaveAt) >= 0) {
    layoutSaveAt = 0;
    saveLayout();
  }
  if (page != PAGE_CLOCK && ringState == RING_IDLE && millis() - lastInteraction > 20000) page = PAGE_CLOCK;

  bool gifChanged = page == PAGE_CLOCK && gifStep();
  static int lastPage = 99;
  // disegno completo: cambio pagina, modifiche, e ogni mezzo secondo (ora, lampeggi, scritte).
  // Tra un disegno completo e l'altro si aggiorna solo la parte della GIF che si muove.
  if (layoutRedraw || page != lastPage || millis() - lastRender >= (ringState == RING_ACTIVE ? 250u : 500u)) {
    lastPage = page;
    lastRender = millis();
    render();
  } else if (gifChanged) {
    render(true);
  }
  delay(2);
}
