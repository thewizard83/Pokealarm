// =====================================================================
//  PokeSveglia M5 — sveglia a tema Pokémon per M5Stack Core2 for AWS IoT EduKit
//  (ESP32, schermo 320x240 touch, RTC BM8563, IMU, vibrazione, barra 10 LED SK6812)
//
//  Schermate:   [ Meteo ]  <— swipe a destra —  [ Orologio ]  — swipe a sinistra —>  [ Sveglie ]
//  Tasti touch sotto lo schermo:  A = meteo,  B = orologio,  C = sveglie
//  Sveglia che suona:  tocco breve = posticipa,  tocco lungo / tasto B / tasto accensione / scuotere = spegni
//  Tasto B tenuto premuto 3 s: attiva la rete di configurazione "PokeSveglia-M5"
// =====================================================================
#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <AnimatedGIF.h>
#include <Adafruit_NeoPixel.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include "app.h"

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
volatile uint8_t uiLang = 0;    // 0 = inglese (predefinita), 1 = italiano
static char detectedCountry[3] = "";
bool hasAccelerometer = false;
volatile uint8_t timeSource = TIME_NONE;

volatile bool rtcWriteRequested = false;  // copia l'ora di sistema nel chip RTC

#define lcd M5.Display
static M5Canvas canvas(&M5.Display);
static Adafruit_NeoPixel leds(LED_COUNT, PIN_LED_BAR, NEO_GRB + NEO_KHZ800);
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
static time_t lastRingEpoch = 0;  // ultima volta che una sveglia vera ha suonato
static int8_t nightForce = 0;      // luce notturna a mano (doppio tap): 1 = accesa, -1 = spenta, 0 = automatica

static int batteryLevel = -1;
static bool charging = false;   // batteria in carica
static bool usbPower = false;   // alimentata da USB (in carica o già carica)

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

static const char* DOW_IT[] = {"DOM", "LUN", "MAR", "MER", "GIO", "VEN", "SAB"};
static const char* MON_IT[] = {"GEN", "FEB", "MAR", "APR", "MAG", "GIU", "LUG", "AGO", "SET", "OTT", "NOV", "DIC"};
static const char* DOW_EN[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* MON_EN[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
#define DOW (uiLang == 1 ? DOW_IT : DOW_EN)
#define MON (uiLang == 1 ? MON_IT : MON_EN)

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
  s["nightLight"] = settings.nightLight;
  s["nightLightStart"] = settings.nightLightStart;
  s["nightLightLevel"] = settings.nightLightLevel;
  s["city"] = settings.city;
  s["gif"] = settings.gif;
  s["lang"] = settings.lang;
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
  settings.nightLight = s["nightLight"] | settings.nightLight;
  settings.nightLightStart = s["nightLightStart"] | settings.nightLightStart;
  settings.nightLightLevel = s["nightLightLevel"] | settings.nightLightLevel;
  strlcpy(settings.city, s["city"] | "", sizeof(settings.city));
  strlcpy(settings.gif, s["gif"] | settings.gif, sizeof(settings.gif));
  settings.lang = s["lang"] | 0;
}

// Lingua: italiano se la sveglia si trova in Italia, San Marino o Vaticano, altrimenti inglese
// (a meno che non sia scelta a mano nelle impostazioni)
void applyLanguage() {
  bool itCountry = !strcmp(detectedCountry, "IT") || !strcmp(detectedCountry, "SM") || !strcmp(detectedCountry, "VA");
  uiLang = settings.lang == 1 ? 0 : settings.lang == 2 ? 1 : (itCountry ? 1 : 0);
}

void setDetectedCountry(const char* cc) {
  if (cc && strlen(cc) == 2 && strcmp(cc, detectedCountry) != 0) {
    strlcpy(detectedCountry, cc, sizeof(detectedCountry));
    Preferences p;  // ricordato per i riavvii senza internet
    p.begin("clock", false);
    p.putString("cc", cc);
    p.end();
  }
  applyLanguage();
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
//  Accelerometro (M5Unified: MPU6886 o BMI270 a seconda della revisione)
// =====================================================================
// L'accelerometro viene letto 200 volte al secondo in un'attività separata (gli accessi al
// bus I2C sono protetti da M5Unified), così i colpetti brevi non vengono persi mentre si
// disegna lo schermo. Riconosce:
//  - "scuotere": molti movimenti forti in 1,5 s (spegne la sveglia che suona)
//  - "doppio colpetto": due colpi secchi sul case a 0,12-0,6 s di distanza,
//     preceduti e seguiti da un momento di quiete (accende/spegne la luce notturna)
static volatile bool shakeDetected = false;
static volatile bool shakeArmed = false;      // "scuoti per spegnere" attivo solo mentre la sveglia suona
static volatile uint32_t vibrationUntil = 0;  // la vibrazione scuote la sveglia: non va contata
static volatile bool knockDetected = false;
static volatile bool knockSuspended = false;  // durante sveglia/voce/vibrazione
static volatile uint32_t lastTouchMs = 0;     // un tocco sullo schermo non deve contare come colpetto

static void imuTask(void*) {
  float px = 0, py = 0, pz = 1;
  bool havePrev = false;
  uint32_t shakeWindow = 0;
  int shakeHits = 0;
  uint32_t lastPeak = 0, firstKnock = 0, secondKnock = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5));
    float ax, ay, az;
    if (!M5.Imu.getAccel(&ax, &ay, &az)) continue;
    uint32_t now = millis();

    // --- scuotere: contato solo mentre la sveglia suona. Prima i movimenti normali
    // (prenderla in mano, appoggiarla, i colpetti) restavano "in memoria" e spegnevano
    // all'istante la prima sveglia successiva.
    float dev = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
    if (!shakeArmed || (int32_t)(now - vibrationUntil) < 0) {
      shakeHits = 0;
      shakeWindow = now;
    } else {
      if (now - shakeWindow > 1500) {
        shakeWindow = now;
        shakeHits = 0;
      }
      if (dev > 0.8f && ++shakeHits >= 4) shakeDetected = true;
    }

    // --- colpetti: variazione brusca dell'accelerazione tra due letture
    float jerk = havePrev ? sqrtf((ax - px) * (ax - px) + (ay - py) * (ay - py) + (az - pz) * (az - pz)) : 0;
    px = ax, py = ay, pz = az;
    havePrev = true;
    if (knockSuspended) {
      firstKnock = secondKnock = 0;
      lastPeak = now;
      continue;
    }
    bool peak = jerk > KNOCK_THRESHOLD_G && now - lastPeak > 70;  // 70 ms: ignora il "rimbalzo" dello stesso colpo
    if (peak) {
      uint32_t quietBefore = now - lastPeak;
      lastPeak = now;
      if (!firstKnock) {
        if (quietBefore > 300) firstKnock = now;  // primo colpo dopo un momento di quiete
      } else if (!secondKnock && now - firstKnock >= 120 && now - firstKnock <= 600) {
        secondKnock = now;
      } else {
        firstKnock = secondKnock = 0;  // troppi colpi o ritmo sbagliato: non è un doppio colpetto
      }
      log_d("colpo %.2f g", jerk);
    }
    if (firstKnock && !secondKnock && now - firstKnock > 600) firstKnock = 0;  // il secondo non è arrivato
    if (secondKnock && now - secondKnock > 300) {  // quiete dopo il secondo colpo: confermato
      bool touched = (int32_t)(lastTouchMs - (firstKnock - 200)) >= 0;  // erano tocchi sullo schermo
      if (!touched) knockDetected = true;
      firstKnock = secondKnock = 0;
    }
  }
}

static bool accelShaken() {
  if (!shakeDetected) return false;
  shakeDetected = false;
  return true;
}

// =====================================================================
//  Batteria (chip di alimentazione AXP192)
// =====================================================================
static void updateBattery() {
  static uint32_t last = 0;
  if (last && millis() - last < 1000) return;
  last = millis();
  batteryLevel = M5.Power.getBatteryLevel();  // 0-100, -1 se non disponibile
  charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  usbPower = M5.Power.getVBUSVoltage() > 4000;  // tensione USB presente
}

// =====================================================================
//  RTC hardware (BM8563): tiene l'ora anche a sveglia spenta. Contiene l'ora UTC.
// =====================================================================
static time_t utcEpoch(int y, int mo, int d, int h, int mi, int s) {
  // giorni dal 1970 (algoritmo "days from civil" di H. Hinnant)
  y -= mo <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097L + (long)doe - 719468L;
  return (time_t)days * 86400 + h * 3600 + mi * 60 + s;
}

static void timeFromRtc() {
  if (!M5.Rtc.isEnabled()) return;
  auto dt = M5.Rtc.getDateTime();
  if (dt.date.year < 2024 || dt.date.year > 2099) return;  // RTC mai impostato
  timeval tv = {utcEpoch(dt.date.year, dt.date.month, dt.date.date, dt.time.hours, dt.time.minutes, dt.time.seconds), 0};
  settimeofday(&tv, nullptr);
  timeSource = TIME_RTC;
  log_i("Ora letta dal chip RTC");
}

static void timeToRtc() {
  if (!M5.Rtc.isEnabled() || !timeValid()) return;
  time_t now = time(nullptr);
  tm u;
  gmtime_r(&now, &u);
  m5::rtc_datetime_t dt;
  dt.date.year = u.tm_year + 1900;
  dt.date.month = u.tm_mon + 1;
  dt.date.date = u.tm_mday;
  dt.date.weekDay = u.tm_wday;
  dt.time.hours = u.tm_hour;
  dt.time.minutes = u.tm_min;
  dt.time.seconds = u.tm_sec;
  M5.Rtc.setDateTime(dt);
}

// =====================================================================
//  Barra LED e vibrazione: lampeggiano mentre suona la sveglia
// =====================================================================
// Luce notturna: dalle 22 (impostabile) fino al suono della prima sveglia della notte.
// Se quella notte non ci sono sveglie, si spegne all'ora di fine della luminosità notturna.
// Restituisce l'intensità 0-255 (0 = spenta), con accensione graduale in 60 secondi.
static uint8_t nightLightAuto() {
  if (!settings.nightLight || !timeValid()) return 0;
  time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  // inizio della notte in corso: oggi alle 22 oppure ieri alle 22
  tm ns = t;
  if (t.tm_hour < settings.nightLightStart) ns.tm_mday -= 1;
  ns.tm_hour = settings.nightLightStart;
  ns.tm_min = ns.tm_sec = 0;
  ns.tm_isdst = -1;
  time_t nightStart = mktime(&ns);
  if (now < nightStart || lastRingEpoch >= nightStart) return 0;  // già suonata stanotte

  // fine: prima sveglia attiva dopo l'inizio della notte (entro le 14 ore successive)
  time_t end = 0;
  for (const Alarm& a : alarms) {
    if (!a.enabled) continue;
    for (int d = 0; d <= 1; d++) {
      tm at = ns;
      at.tm_mday += d;
      at.tm_hour = a.hour;
      at.tm_min = a.minute;
      at.tm_isdst = -1;
      time_t e = mktime(&at);  // normalizza anche tm_wday
      int bit = (at.tm_wday + 6) % 7;
      if (e <= nightStart || e > nightStart + 14 * 3600) continue;
      if (a.days && !(a.days >> bit & 1)) continue;
      if (!end || e < end) end = e;
    }
  }
  if (!end) {  // nessuna sveglia: fino all'ora di fine notte (di default le 7)
    tm at = ns;
    at.tm_mday += 1;
    at.tm_hour = settings.nightEnd < 12 ? settings.nightEnd : 7;
    at.tm_isdst = -1;
    end = mktime(&at);
  }
  if (now >= end) return 0;
  int lvl = settings.nightLightLevel * 255 / 100;
  long sinceStart = now - nightStart;
  if (sinceStart < 60) lvl = lvl * (sinceStart + 1) / 60;  // accensione dolce
  return (uint8_t)max(1, lvl);
}

// livello effettivo: automatico, oppure forzato con il doppio tap finché l'automatico non cambia stato
static uint8_t nightLightLevel() {
  static bool lastAutoOn = false;
  uint8_t autoLvl = nightLightAuto();
  if ((autoLvl > 0) != lastAutoOn) {
    lastAutoOn = autoLvl > 0;
    nightForce = 0;
  }
  if (nightForce > 0) return (uint8_t)max(26, settings.nightLightLevel * 255 / 100);
  if (nightForce < 0) return 0;
  return autoLvl;
}

static void toggleNightLight() {
  bool on = nightLightLevel() > 0;
  nightForce = on ? -1 : 1;
  showToast(on ? T("Night light off", "Luce notturna spenta") : T("Night light on", "Luce notturna accesa"));
}

static void updateLedsAndVibration() {
  static uint32_t last = 0;
  static bool wasActive = false;
  if (millis() - last < 30) return;
  last = millis();
  bool ringing = ringState == RING_ACTIVE;
  bool speaking = audioIsSpeaking();
  if ((ringing || speaking) && !wasActive) M5.Power.setExtOutput(true);  // 5 V per la barra LED a batteria
  if (ringing) {
    uint32_t t = millis() - ringStartMs;
    // luce che gira lungo la barra con una scia; a ogni giro cambia colore:
    // giallo -> rosso -> verde -> giallo ...
    static const uint8_t COLORS[3][3] = {{255, 190, 0}, {255, 0, 0}, {0, 220, 40}};
    const uint32_t STEP_MS = 90;  // tempo per passare al LED successivo
    uint32_t step = t / STEP_MS;
    int head = step % LED_COUNT;
    const uint8_t* c = COLORS[(step / LED_COUNT) % 3];
    for (int i = 0; i < LED_COUNT; i++) {
      int dist = (head - i + LED_COUNT) % LED_COUNT;  // 0 = testa, poi la scia
      int level = dist == 0 ? 255 : dist == 1 ? 110 : dist == 2 ? 45 : dist == 3 ? 15 : 0;
      leds.setPixelColor(i, leds.Color(c[0] * level / 255, c[1] * level / 255, c[2] * level / 255));
    }
    leds.setBrightness(150);
    leds.show();
    // vibrazione a impulsi dopo i primi 10 secondi
    bool vib = t > 10000 && (t % 1160) < 400;
    if (vib) vibrationUntil = millis() + 250;  // ignora anche il motorino che si ferma
    M5.Power.setVibration(vib ? 180 : 0);
    wasActive = true;
  } else if (speaking) {
    uint8_t b = 30 + (uint8_t)(40 * (1 + sinf(millis() / 300.0f)));
    for (int i = 0; i < LED_COUNT; i++) leds.setPixelColor(i, leds.Color(40, 90, 255));
    leds.setBrightness(b);
    leds.show();
    wasActive = true;
  } else if (uint8_t nl = nightLightLevel()) {
    // luce notturna: ambra calda (niente blu, che disturba il sonno)
    if (!wasActive) M5.Power.setExtOutput(true);
    for (int i = 0; i < LED_COUNT; i++) leds.setPixelColor(i, leds.Color(255, 70, 0));
    leds.setBrightness(nl);
    leds.show();
    M5.Power.setVibration(0);
    wasActive = true;
  } else if (wasActive) {
    leds.clear();
    leds.show();
    M5.Power.setVibration(0);
    M5.Power.setExtOutput(false);
    wasActive = false;
  }
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
  shakeDetected = false;  // conta solo una scossa fatta mentre suona
  shakeArmed = true;
  ringState = RING_ACTIVE;
  ringingAlarm = idx;
  ringStartMs = millis();
  if (idx >= 0) lastRingEpoch = time(nullptr);
  nightForce = 0;
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
  snprintf(b, sizeof(b), T("Snoozed for %d min", "Posticipata di %d min"), settings.snoozeMin);
  showToast(b);
}

static void stopRing() {
  audioStop();
  ringState = RING_IDLE;
  ringingAlarm = -1;
  testStopAt = 0;
  showToast(T("Good morning!", "Buongiorno!"));
}

static void alarmLogic() {
  static time_t lastChecked = 0;  // inizio dell'ultimo minuto già controllato
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
  // Controlla TUTTI i minuti trascorsi dall'ultimo controllo (fino a 15 minuti), non solo
  // quello attuale: così una sveglia non viene saltata se l'ora fa un salto in avanti
  // (correzione da internet, dal chip RTC o dal telefono) o se l'avvio è stato lento.
  time_t now = time(nullptr);
  time_t minuteNow = now - now % 60;
  if (!lastChecked || minuteNow < lastChecked || minuteNow - lastChecked > 15 * 60)
    lastChecked = minuteNow - 60;  // primo controllo, ora tornata indietro o salto enorme: solo il minuto attuale
  while (lastChecked < minuteNow) {
    lastChecked += 60;
    if (ringState != RING_IDLE) continue;
    tm t;
    localtime_r(&lastChecked, &t);
    int bit = (t.tm_wday + 6) % 7;
    for (int i = 0; i < MAX_ALARMS; i++) {
      Alarm& a = alarms[i];
      if (!a.enabled || a.hour != t.tm_hour || a.minute != t.tm_min) continue;
      if (a.days && !(a.days >> bit & 1)) continue;
      if (!a.days) {  // sveglia singola: si disattiva dopo aver suonato
        a.enabled = false;
        saveConfig();
      }
      log_i("Sveglia %d delle %02d:%02d", i, a.hour, a.minute);
      startRing(i);
      break;
    }
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
  if (code == 0) return T("Clear", "Sereno");
  if (code <= 2) return T("Partly cloudy", "Poco nuvoloso");
  if (code == 3) return T("Cloudy", "Nuvoloso");
  if (code == 45 || code == 48) return T("Fog", "Nebbia");
  if (code <= 57) return T("Drizzle", "Pioggerella");
  if (code <= 67) return T("Rain", "Pioggia");
  if (code <= 77) return T("Snow", "Neve");
  if (code <= 82) return T("Showers", "Rovesci");
  if (code <= 86) return T("Snow", "Neve");
  return T("Thunderstorm", "Temporale");
}

static void drawPokeball(int x, int y, int r) {
  canvas.fillCircle(x, y, r, C_WHITE);
  canvas.fillArc(x, y, r, 0, 180, 360, C_RED);
  canvas.fillRect(x - r, y - r / 9, 2 * r + 1, r * 2 / 9 + 1, C_BLACK);
  canvas.fillCircle(x, y, r * 3 / 10, C_BLACK);
  canvas.fillCircle(x, y, r * 2 / 10, C_WHITE);
  canvas.drawCircle(x, y, r, C_BLACK);
}

// ------------------------------------------------------------- pagine (320x240)
static constexpr int CX = SCREEN_W / 2;

static void drawFrame(uint16_t col) {
  canvas.drawRoundRect(0, 0, SCREEN_W, SCREEN_H, 14, col);
  canvas.drawRoundRect(1, 1, SCREEN_W - 2, SCREEN_H - 2, 13, col);
}

static const char* DOW_FULL_IT[] = {"DOMENICA", "LUNEDI'", "MARTEDI'", "MERCOLEDI'", "GIOVEDI'", "VENERDI'", "SABATO"};
static const char* MON_FULL_IT[] = {"GENNAIO", "FEBBRAIO", "MARZO",  "APRILE",  "MAGGIO",   "GIUGNO",
                                    "LUGLIO",  "AGOSTO",   "SETTEMBRE", "OTTOBRE", "NOVEMBRE", "DICEMBRE"};
static const char* DOW_FULL_EN[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
static const char* MON_FULL_EN[] = {"JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
                                    "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
#define DOW_FULL (uiLang == 1 ? DOW_FULL_IT : DOW_FULL_EN)
#define MON_FULL (uiLang == 1 ? MON_FULL_IT : MON_FULL_EN)

// testo dentro una "pillola" semitrasparente; align: -1 = ancorata a sinistra, +1 = a destra, 0 = centrata
static void pillText(const char* s, int x, int y, int align, uint16_t col, uint16_t bg = C_BLACK, uint8_t alpha = 150) {
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  int w = canvas.textWidth(s) + 24;
  int x0 = align < 0 ? x : align > 0 ? x - w : x - w / 2;
  blendRect(x0, y - 15, w, 30, 15, bg, alpha);
  txt(s, x0 + w / 2, y + 1, &fonts::FreeSansBold9pt7b, col);
}

static void drawClockPage(const tm* t) {
  if (gifOk) {
    composeGif();
  } else {
    canvas.fillScreen(C_NAVY);
    drawPokeball(CX, 75, 40);
  }

  bool ringing = ringState == RING_ACTIVE;
  bool blink = (millis() / 500) & 1;

  // --- in alto a sinistra: icona sveglia + prossimo orario (o OFF)
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
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  int aw = canvas.textWidth(nb) + 52;
  blendRect(6, 6, aw, 32, 16, C_BLACK, 150);
  drawBell(25, 21, any ? C_YELLOW : C_GRAY, !any);
  txt(nb, 42, 23, &fonts::FreeSansBold9pt7b, any ? C_WHITE : C_GRAY, textdatum_t::middle_left);

  // --- in alto a destra: batteria con percentuale
  if (batteryLevel >= 0) {
    char pb[8];
    snprintf(pb, sizeof(pb), "%d%%", batteryLevel);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    int bw = canvas.textWidth(pb) + 50;
    blendRect(SCREEN_W - 6 - bw, 6, bw, 32, 16, C_BLACK, 150);
    drawBattery(SCREEN_W - bw + 6, 22);
    txt(pb, SCREEN_W - 18, 23, &fonts::FreeSansBold9pt7b, C_WHITE, textdatum_t::middle_right);
  }

  // --- orario grande al centro in basso
  if (ringing)
    blendRect(16, 122, SCREEN_W - 32, 84, 24, C_RED, blink ? 210 : 140);
  else
    blendRect(16, 122, SCREEN_W - 32, 84, 24, C_BLACK, 140);
  char hh[4] = "--", mm[4] = "--";
  if (t) {
    snprintf(hh, sizeof(hh), "%02d", t->tm_hour);
    snprintf(mm, sizeof(mm), "%02d", t->tm_min);
  }
  // ore e minuti separati: i due punti lampeggiano senza spostare le cifre
  txt(hh, CX - 14, 170, &fonts::DejaVu72, C_WHITE, textdatum_t::middle_right);
  txt(mm, CX + 14, 170, &fonts::DejaVu72, C_WHITE, textdatum_t::middle_left);
  if (!t || !(t->tm_sec & 1)) {
    canvas.fillCircle(CX, 153, 5, C_WHITE);
    canvas.fillCircle(CX, 175, 5, C_WHITE);
  }

  // --- in basso: giorno della settimana a sinistra, numero e mese a destra
  const int by = 222;
  if (ringing) {
    const char* lab = ringingAlarm >= 0 && alarms[ringingAlarm].label[0] ? alarms[ringingAlarm].label : T("ALARM!", "SVEGLIA!");
    const char* msgs[3] = {lab, T("Tap: snooze", "Tocca: posticipa"), T("Hold to stop", "Tieni premuto: spegni")};
    pillText(msgs[(millis() / 2000) % 3], CX, by, 0, C_WHITE, C_RED, 200);
  } else if (!t) {
    pillText(WiFi.status() == WL_CONNECTED ? T("Syncing time...", "Sincronizzo l'ora...") : T("No Wi-Fi", "Wi-Fi assente"), CX, by, 0, C_YELLOW);
  } else {
    pillText(DOW_FULL[t->tm_wday], 6, by, -1, C_YELLOW);
    char db[24];
    if (WiFi.status() != WL_CONNECTED && (millis() / 3000) & 1)
      strlcpy(db, "NO WI-FI", sizeof(db));
    else
      snprintf(db, sizeof(db), "%d %s", t->tm_mday, MON_FULL[t->tm_mon]);
    pillText(db, SCREEN_W - 6, by, 1, C_YELLOW);
  }

  if (ringing) {  // campanella che oscilla al centro in alto
    int dx = blink ? -3 : 3;
    blendRect(CX - 28, 40, 56, 56, 28, C_BLACK, 120);
    drawBell(CX + dx, 68, C_YELLOW, false, 2);
  }
}

static void drawWeatherPage() {
  canvas.fillScreen(C_NAVY);
  drawFrame(C_BLUE);
  WeatherData w;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  w = weather;
  xSemaphoreGive(dataMutex);

  if (!w.valid) {
    drawPokeball(CX, 95, 34);
    txt(WiFi.status() == WL_CONNECTED ? T("Loading weather...", "Meteo in arrivo...") : T("No Wi-Fi", "Wi-Fi assente"), CX, 158, &fonts::FreeSans9pt7b, C_WHITE);
    return;
  }
  char city[28];
  strlcpy(city, w.city[0] ? w.city : T("Weather", "Meteo"), sizeof(city));
  txt(city, CX, 20, &fonts::FreeSansBold12pt7b, C_YELLOW);
  char now[40];
  snprintf(now, sizeof(now), T("Now %d   %s", "Ora %d   %s"), (int)lroundf(w.tempNow), weatherText(w.codeNow));
  txt(now, CX, 46, &fonts::FreeSans9pt7b, C_WHITE);
  canvas.setFont(&fonts::FreeSans9pt7b);
  {  // simbolo dei gradi dopo la temperatura attuale
    char tb[12];
    snprintf(tb, sizeof(tb), T("Now %d", "Ora %d"), (int)lroundf(w.tempNow));
    int x = CX - canvas.textWidth(now) / 2 + canvas.textWidth(tb) + 4;
    canvas.drawCircle(x, 41, 2, C_WHITE);
  }

  canvas.drawFastVLine(CX, 64, 146, C_DGRAY);
  const char* hdr[] = {T("TODAY", "OGGI"), T("TOMORROW", "DOMANI")};
  for (int i = 0; i < 2; i++) {
    int cx = i == 0 ? CX - 80 : CX + 80;
    txt(hdr[i], cx, 74, &fonts::FreeSansBold9pt7b, C_CYAN);
    drawWeatherIcon(cx, 108, w.day[i].code);
    drawTemp(w.day[i].tmax, cx, 152, &fonts::FreeSansBold12pt7b, C_ORANGE);
    drawTemp(w.day[i].tmin, cx, 177, &fonts::FreeSans9pt7b, C_CYAN);
    char rb[8];
    snprintf(rb, sizeof(rb), "%d%%", w.day[i].rain);
    canvas.fillCircle(cx - 16, 201, 4, C_CYAN);
    canvas.fillTriangle(cx - 20, 200, cx - 12, 200, cx - 16, 193, C_CYAN);
    txt(rb, cx + 4, 200, &fonts::FreeSans9pt7b, C_WHITE);
  }
  if (w.updated) {
    tm u;
    localtime_r(&w.updated, &u);
    char ub[24];
    snprintf(ub, sizeof(ub), T("updated at %02d:%02d", "aggiornato alle %02d:%02d"), u.tm_hour, u.tm_min);
    txt(ub, CX, 226, &fonts::Font0, C_GRAY);
  }
}

// riga della lista sveglie toccata (o -1)
static int alarmRowAt(int y) {
  int r = (y - 40) / 32;
  return (y >= 40 && r >= 0 && r < MAX_ALARMS) ? r : -1;
}

static void drawAlarmsPage() {
  canvas.fillScreen(rgb(24, 24, 40));
  drawFrame(C_RED);
  txt(T("ALARMS", "SVEGLIE"), CX, 20, &fonts::FreeSansBold12pt7b, C_YELLOW);
  const char* D = T("MTWTFSS", "LMMGVSD");
  for (int i = 0; i < MAX_ALARMS; i++) {
    const Alarm& a = alarms[i];
    int y = 56 + i * 32;
    if (i) canvas.drawFastHLine(20, y - 16, SCREEN_W - 40, rgb(40, 40, 60));
    char hb[8];
    snprintf(hb, sizeof(hb), "%02d:%02d", a.hour, a.minute);
    txt(hb, 22, y, &fonts::FreeSansBold12pt7b, a.enabled ? C_WHITE : C_GRAY, textdatum_t::middle_left);
    canvas.setFont(&fonts::Font2);
    canvas.setTextDatum(textdatum_t::middle_center);
    if (a.days == 0) {
      canvas.setTextColor(a.enabled ? C_CYAN : C_DGRAY);
      canvas.drawString(T("once", "una volta"), 172, y);
    } else {
      for (int d = 0; d < 7; d++) {
        canvas.setTextColor((a.days >> d & 1) ? (a.enabled ? C_CYAN : C_GRAY) : C_DGRAY);
        char s[2] = {D[d], 0};
        canvas.drawString(s, 130 + d * 14, y);
      }
    }
    // etichetta (se c'è), piccola sotto i giorni
    // interruttore
    int sx = 258;
    canvas.fillRoundRect(sx, y - 10, 40, 20, 10, a.enabled ? C_GREEN : C_DGRAY);
    canvas.fillCircle(a.enabled ? sx + 30 : sx + 10, y, 8, C_WHITE);
  }
  char ib[40];
  if (portalActive && WiFi.status() != WL_CONNECTED)
    snprintf(ib, sizeof(ib), "Wi-Fi: %s", AP_SSID);
  else if (WiFi.status() == WL_CONNECTED)
    snprintf(ib, sizeof(ib), "http://%s", ipString().c_str());
  else if (lastDiscReason == 201)
    strlcpy(ib, T("Wi-Fi: network not found", "Wi-Fi: rete non trovata"), sizeof(ib));
  else if (lastDiscReason == 202 || lastDiscReason == 15 || lastDiscReason == 204)
    strlcpy(ib, T("Wi-Fi: wrong password?", "Wi-Fi: password errata?"), sizeof(ib));
  else
    strlcpy(ib, T("Wi-Fi: reconnecting...", "Wi-Fi: riconnessione..."), sizeof(ib));
  txt(ib, CX, 222, &fonts::Font2, C_CYAN);
}

static void drawSetupPage() {
  canvas.fillScreen(C_NAVY);
  drawFrame(C_YELLOW);
  drawPokeball(CX, 44, 26);
  txt(T("Set up Wi-Fi", "Configura il Wi-Fi"), CX, 92, &fonts::FreeSansBold12pt7b, C_YELLOW);
  txt(T("Join the network:", "Collegati alla rete:"), CX, 124, &fonts::FreeSans9pt7b, C_WHITE);
  txt(AP_SSID, CX, 148, &fonts::FreeSansBold9pt7b, C_CYAN);
  if (strlen(AP_PASSWORD)) {
    char pb[40];
    snprintf(pb, sizeof(pb), "password: %s", AP_PASSWORD);
    txt(pb, CX, 172, &fonts::FreeSans9pt7b, C_WHITE);
  }
  txt(T("then open 192.168.4.1", "poi apri 192.168.4.1"), CX, 204, &fonts::FreeSans9pt7b, C_WHITE);
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
  drawFrame(C_YELLOW);
  drawEnvelope(CX, 20, C_YELLOW);  // busta in alto, scritta sotto
  txt(T("MESSAGE", "MESSAGGIO"), CX, 46, &fonts::FreeSansBold12pt7b, C_YELLOW);

  // a capo per parole, larghezza fissa
  String s = asciiFold(text);
  canvas.setFont(&fonts::FreeSans9pt7b);
  const int W = 286, LH = 21, VISIBLE = 6;
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
  int y0 = 136 - (shown - 1) * LH / 2;
  for (int k = 0; k < shown; k++) txt(lines[first + k].c_str(), CX, y0 + k * LH, &fonts::FreeSans9pt7b, C_WHITE);

  const char* st = speechStatus();
  const char* foot = strcmp(st, "download") == 0 ? T("preparing voice...", "preparo la voce...") : strncmp(st, "error", 5) == 0 ? T("voice unavailable", "voce non disponibile") : T("tap to close", "tocca per chiudere");
  txt(foot, CX, 224, &fonts::Font2, C_CYAN);
}

static void drawToast() {
  if (!toastMsg[0] || (int32_t)(millis() - toastUntil) >= 0) return;
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  int w = min(280, (int)canvas.textWidth(toastMsg) + 28);
  blendRect(CX - w / 2, 104, w, 34, 17, C_BLACK, 210);
  txt(toastMsg, CX, 121, &fonts::FreeSansBold9pt7b, C_WHITE);
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
  if (partial) lcd.setClipRect(clipX0, clipY0, clipX1 - clipX0, clipY1 - clipY0);
  canvas.pushSprite(0, 0);
  if (partial) {
    lcd.clearClipRect();
    canvas.clearClipRect();
    clipX0 = clipY0 = 0, clipX1 = SCREEN_W, clipY1 = SCREEN_H;
  }
}

static void drawBootScreen(const char* line1, const char* line2) {
  canvas.fillScreen(C_NAVY);
  drawPokeball(CX, 85, 38);
  txt(line1, CX, 148, &fonts::FreeSansBold12pt7b, C_YELLOW);
  txt(line2, CX, 176, &fonts::FreeSans9pt7b, C_WHITE);
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
//  Input: touch e tasti
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
  static bool down = false, longFired = false;
  static uint32_t t0 = 0;
  static int sx, sy, lx, ly;
  // i tocchi sotto lo schermo (y >= 240) sono i tasti A/B/C, gestiti a parte
  bool touched = false;
  int x = 0, y = 0;
  if (M5.Touch.getCount()) {
    lastTouchMs = millis();
    auto d = M5.Touch.getDetail();
    if (d.isPressed() && d.y < SCREEN_H) {
      touched = true;
      x = d.x;
      y = d.y;
    }
  }
  if (touched) {
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
    if (abs(dx) > 50 && abs(dx) > abs(dy))
      onSwipe(dx > 0 ? 1 : -1);
    else if (abs(dx) < 25 && abs(dy) < 25)
      onTap(sx, sy);
  }
}

// Tasti touch A/B/C sotto lo schermo e tasto di accensione
static void pollButtons() {
  bool any = M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed() || M5.BtnPWR.wasClicked();
  if (M5.BtnA.isPressed() || M5.BtnB.isPressed() || M5.BtnC.isPressed() || M5.BtnPWR.isPressed()) lastTouchMs = millis();
  if (any) {
    wakeUntil = millis() + 15000;
    lastInteraction = millis();
  }
  if (ringState != RING_IDLE) {
    if (M5.BtnB.wasClicked() || M5.BtnPWR.wasClicked()) stopRing();
    else if (ringState == RING_ACTIVE && (M5.BtnA.wasClicked() || M5.BtnC.wasClicked())) snooze();
    return;
  }
  uint32_t since;
  static char tmp[4];
  if (any && speechGetCurrent(tmp, sizeof(tmp), since)) {
    speechDismiss();
    return;
  }
  if (M5.BtnA.wasClicked()) page = PAGE_WEATHER;
  if (M5.BtnB.wasClicked() || M5.BtnPWR.wasClicked()) page = PAGE_CLOCK;
  if (M5.BtnC.wasClicked()) page = PAGE_ALARMS;
  if (M5.BtnB.wasHold()) {  // 3 secondi: rete di configurazione
    portalManual = true;
    startPortal();
    showToast(T("Setup network on", "Rete Setup attiva"), 4000);
    page = PAGE_ALARMS;
  }
}

// salva il fuso orario quando cambia, così dopo lo spegnimento l'ora del chip RTC
// viene mostrata subito nel fuso giusto anche senza internet
static void persistTimezone() {
  static uint32_t last = 0;
  static String saved;
  if (millis() - last < 10000) return;
  last = millis();
  const char* tz = getenv("TZ");
  if (!tz || saved == tz) return;
  prefs.begin("clock", false);
  if (prefs.getString("tz", "") != tz) prefs.putString("tz", tz);
  prefs.end();
  saved = tz;
}

// =====================================================================
//  Setup / loop
// =====================================================================
void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = true;
  cfg.internal_rtc = true;
  cfg.internal_spk = true;
  cfg.internal_mic = false;
  M5.begin(cfg);
  M5.BtnB.setHoldThresh(3000);
  dataMutex = xSemaphoreCreateMutex();

  lcd.setBrightness(0);
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvas.createSprite(SCREEN_W, SCREEN_H);
  gifBuf = (uint16_t*)ps_malloc(SCREEN_W * SCREEN_H * 2);
  drawBootScreen("PokeSveglia", T("Starting...", "Avvio..."));
  lcd.setBrightness(200);

  leds.begin();
  leds.clear();
  leds.show();

  if (!LittleFS.begin(true)) log_e("LittleFS non disponibile");
  LittleFS.mkdir("/gifs");
  loadConfig();
  {  // paese rilevato l'ultima volta -> lingua già giusta dall'avvio
    Preferences p;
    p.begin("clock", true);
    strlcpy(detectedCountry, p.getString("cc", "").c_str(), sizeof(detectedCountry));
    p.end();
    applyLanguage();
  }

  audioBegin();
  hasAccelerometer = M5.Imu.isEnabled();
  if (hasAccelerometer) xTaskCreatePinnedToCore(imuTask, "imu", 3072, nullptr, 3, nullptr, 0);
  updateBattery();

  // ora dal chip RTC (funziona anche senza internet), nel fuso orario salvato
  prefs.begin("clock", true);
  String savedTz = prefs.getString("tz", "CET-1CEST,M3.5.0,M10.5.0/3");
  prefs.end();
  setenv("TZ", savedTz.c_str(), 1);
  tzset();
  timeFromRtc();

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
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  if (wifiSsid.length()) {
    // fino a 3 tentativi: dopo una lunga pausa spesso il primo fallisce
    for (int attempt = 1; attempt <= 3 && WiFi.status() != WL_CONNECTED; attempt++) {
      char l2[48];
      snprintf(l2, sizeof(l2), "%.28s (%d/3)", wifiSsid.c_str(), attempt);
      drawBootScreen(T("Connecting to", "Connessione a"), l2);
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

  // ora da internet: quando arriva aggiorna anche il chip RTC
  sntp_set_time_sync_notification_cb([](timeval*) {
    timeSource = TIME_NTP;
    rtcWriteRequested = true;
  });
  configTzTime(savedTz.c_str(), "pool.ntp.org", "time.google.com", "time.inrim.it");
  webBegin();
  netTaskStart();
  speechTaskStart();
  lastInteraction = millis();
}

void loop() {
  static uint32_t lastRender = 0;
  M5.update();
  webLoop();
  wifiMaintain();
  pollButtons();
  pollTouch();
  shakeArmed = ringState == RING_ACTIVE;
  if (ringState == RING_ACTIVE && accelShaken()) stopRing();
  // doppio colpetto sulla sveglia: luce notturna (non mentre suona, parla o vibra)
  knockSuspended = ringState == RING_ACTIVE || audioIsSpeaking();
  if (knockDetected) {
    knockDetected = false;
    if (ringState == RING_IDLE) {
      toggleNightLight();
      wakeUntil = millis() + 15000;
    }
  }
  alarmLogic();
  audioLoop();
  updateBattery();
  updateBacklight();
  updateLedsAndVibration();
  persistTimezone();
  if (rtcWriteRequested) {
    rtcWriteRequested = false;
    timeToRtc();
  }

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
