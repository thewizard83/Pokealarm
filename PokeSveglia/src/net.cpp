// Attività di rete in background: posizione (da IP o città manuale),
// meteo Open-Meteo e fuso orario automatico.
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "app.h"

static const uint32_t WEATHER_EVERY_MS = 30UL * 60 * 1000;   // meteo ogni 30 minuti
static const uint32_t LOCATE_EVERY_MS = 12UL * 3600 * 1000;  // posizione ogni 12 ore
static const uint32_t RETRY_MS = 60UL * 1000;

static float lat = 0, lon = 0;
static bool haveLoc = false;
static char locCity[48] = "";
static char locForCity[48] = "\x01";  // città (impostazione) per cui è valida la posizione
static uint32_t lastLocate = 0;

// ---------------------------------------------------------------------------
// Fuso orario: Open-Meteo restituisce il nome IANA (es. "Europe/Rome"),
// l'ESP32 vuole una stringa POSIX con le regole dell'ora legale.
struct TzMap {
  const char* iana;
  const char* posix;
};
static const TzMap TZ_TABLE[] = {
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Atlantic/Canary", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Kiev", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Sofia", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Riga", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Tallinn", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Vilnius", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Istanbul", "TRT-3"},
    {"Europe/Moscow", "MSK-3"},
    {"Europe/Minsk", "MSK-3"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "MST7"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};

const char* tzFromIana(const char* iana, long offsetSec) {
  for (auto& t : TZ_TABLE)
    if (strcmp(iana, t.iana) == 0) return t.posix;
  // Tutto il resto dell'Europa continentale (Italia compresa) usa CET/CEST
  if (strncmp(iana, "Europe/", 7) == 0 || strcmp(iana, "Africa/Ceuta") == 0)
    return "CET-1CEST,M3.5.0,M10.5.0/3";
  // Altrove: offset fisso (si riallinea a ogni aggiornamento del meteo)
  static char buf[24];
  long m = -offsetSec / 60;  // POSIX ha il segno invertito
  snprintf(buf, sizeof(buf), "UTC%c%ld:%02ld", m < 0 ? '-' : '+', labs(m) / 60, labs(m) % 60);
  return buf;
}

// ---------------------------------------------------------------------------
static bool httpGetJson(const String& url, JsonDocument& doc) {
  HTTPClient http;
  WiFiClientSecure secure;
  WiFiClient plain;
  http.setTimeout(10000);
  bool ok;
  if (url.startsWith("https")) {
    secure.setInsecure();
    ok = http.begin(secure, url);
  } else {
    ok = http.begin(plain, url);
  }
  if (!ok) return false;
  int code = http.GET();
  if (code != 200) {
    log_w("HTTP %d per %s", code, url.c_str());
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  return !deserializeJson(doc, body);
}

static String urlEncode(const char* s) {
  String out;
  for (; *s; s++) {
    char c = *s;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') {
      out += c;
    } else {
      char b[4];
      snprintf(b, sizeof(b), "%%%02X", (uint8_t)c);
      out += b;
    }
  }
  return out;
}

static bool locate() {
  char city[48];
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  strlcpy(city, settings.city, sizeof(city));
  xSemaphoreGive(dataMutex);

  bool cityChanged = strcmp(city, locForCity) != 0;
  if (haveLoc && !cityChanged && millis() - lastLocate < LOCATE_EVERY_MS) return true;

  JsonDocument doc;
  if (city[0]) {
    // città scelta a mano nella pagina web
    String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&language=it&format=json&name=" + urlEncode(city);
    if (!httpGetJson(url, doc) || !doc["results"][0]) return false;
    JsonObject r = doc["results"][0];
    lat = r["latitude"];
    lon = r["longitude"];
    strlcpy(locCity, r["name"] | city, sizeof(locCity));
  } else {
    // posizione approssimativa dall'IP pubblico
    if (!httpGetJson("http://ip-api.com/json/?fields=status,city,lat,lon", doc)) return false;
    if (strcmp(doc["status"] | "", "success") != 0) return false;
    lat = doc["lat"];
    lon = doc["lon"];
    strlcpy(locCity, doc["city"] | "", sizeof(locCity));
  }
  haveLoc = true;
  lastLocate = millis();
  strlcpy(locForCity, city, sizeof(locForCity));
  log_i("Posizione: %s (%.3f, %.3f)", locCity, lat, lon);
  return true;
}

static bool fetchWeather() {
  char url[320];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&timezone=auto&forecast_days=2",
           lat, lon);
  JsonDocument doc;
  if (!httpGetJson(url, doc)) return false;

  // fuso orario automatico
  const char* tz = tzFromIana(doc["timezone"] | "", doc["utc_offset_seconds"] | 0L);
  if (strcmp(getenv("TZ") ? getenv("TZ") : "", tz) != 0) {
    setenv("TZ", tz, 1);
    tzset();
    log_i("Fuso orario: %s -> %s", (const char*)(doc["timezone"] | ""), tz);
  }

  xSemaphoreTake(dataMutex, portMAX_DELAY);
  weather.tempNow = doc["current"]["temperature_2m"] | 0.0f;
  weather.codeNow = doc["current"]["weather_code"] | -1;
  for (int i = 0; i < 2; i++) {
    weather.day[i].code = doc["daily"]["weather_code"][i] | -1;
    weather.day[i].tmax = doc["daily"]["temperature_2m_max"][i] | 0.0f;
    weather.day[i].tmin = doc["daily"]["temperature_2m_min"][i] | 0.0f;
    weather.day[i].rain = doc["daily"]["precipitation_probability_max"][i] | 0;
  }
  strlcpy(weather.city, locCity, sizeof(weather.city));
  weather.updated = time(nullptr);
  weather.valid = true;
  xSemaphoreGive(dataMutex);
  log_i("Meteo aggiornato: %.1f C, codice %d", weather.tempNow, weather.codeNow);
  return true;
}

static void netTask(void*) {
  uint32_t nextRun = 0;
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && (weatherRefreshRequested || (int32_t)(millis() - nextRun) >= 0)) {
      weatherRefreshRequested = false;
      bool ok = locate() && fetchWeather();
      nextRun = millis() + (ok ? WEATHER_EVERY_MS : RETRY_MS);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void netTaskStart() {
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}
