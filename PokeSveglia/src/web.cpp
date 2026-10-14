// Server web: pagina di configurazione + API JSON.
// Raggiungibile all'IP della sveglia, a http://pokesveglia.local
// e, in modalità configurazione, a http://192.168.4.1 sulla rete "PokeSveglia-Setup".
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include "app.h"
#include "web_page.h"

static WebServer server(80);
static File uploadFile;
static String uploadName;
static bool uploadOk = false;

static String cleanGifName(String n) {
  n.toLowerCase();
  if (n.endsWith(".gif")) n.remove(n.length() - 4);
  String out;
  for (char c : n)
    if (isalnum((unsigned char)c) || c == '_' || c == '-') out += c;
  if (out.isEmpty()) return "";
  return out.substring(0, 30) + ".gif";
}

static void sendJson(JsonDocument& doc, int code = 200) {
  String s;
  serializeJson(doc, s);
  server.send(code, "application/json", s);
}

static void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }

static void handleState() {
  JsonDocument doc;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  JsonArray al = doc["alarms"].to<JsonArray>();
  for (int i = 0; i < MAX_ALARMS; i++) {
    JsonObject a = al.add<JsonObject>();
    a["en"] = alarms[i].enabled;
    a["h"] = alarms[i].hour;
    a["m"] = alarms[i].minute;
    a["days"] = alarms[i].days;
    a["label"] = alarms[i].label;
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
  doc["weather"]["valid"] = weather.valid;
  doc["weather"]["city"] = weather.city;
  xSemaphoreGive(dataMutex);

  JsonArray gifs = doc["gifs"].to<JsonArray>();
  File dir = LittleFS.open("/gifs");
  if (dir) {
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      String n = f.name();
      if (n.endsWith(".gif")) {
        JsonObject g = gifs.add<JsonObject>();
        g["name"] = n;
        g["size"] = f.size();
      }
    }
  }
  doc["fs"]["total"] = LittleFS.totalBytes();
  doc["fs"]["used"] = LittleFS.usedBytes();

  JsonObject w = doc["wifi"].to<JsonObject>();
  w["connected"] = WiFi.status() == WL_CONNECTED;
  w["ssid"] = WiFi.SSID();
  w["ip"] = WiFi.localIP().toString();
  w["portal"] = (bool)portalActive;

  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm t;
    localtime_r(&now, &t);
    char b[8];
    strftime(b, sizeof(b), "%H:%M", &t);
    doc["time"] = b;
  }
  doc["accelerometer"] = hasAccelerometer;
  doc["timeSrc"] = (int)timeSource;
  int bl;
  bool bc, bu;
  if (batteryInfo(bl, bc, bu)) {
    doc["battery"]["level"] = bl;
    doc["battery"]["charging"] = bc;
    doc["battery"]["usb"] = bu;
  }
  doc["speech"] = speechStatus();
  speechHistoryJson(doc["messages"].to<JsonArray>());
  doc["layout"]["x"] = gifLayX;
  doc["layout"]["y"] = gifLayY;
  doc["layout"]["s"] = gifLayS;
  doc["layout"]["v"] = gifLayV;
  sendJson(doc);
}

static void handleAlarms() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) || !doc.is<JsonArray>()) {
    server.send(400, "text/plain", "JSON non valido");
    return;
  }
  JsonArray arr = doc.as<JsonArray>();
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  for (int i = 0; i < MAX_ALARMS && i < (int)arr.size(); i++) {
    JsonObject a = arr[i];
    alarms[i].enabled = a["en"] | false;
    alarms[i].hour = constrain((int)(a["h"] | 7), 0, 23);
    alarms[i].minute = constrain((int)(a["m"] | 0), 0, 59);
    alarms[i].days = (a["days"] | 0) & 0x7F;
    strlcpy(alarms[i].label, a["label"] | "", sizeof(alarms[i].label));
  }
  xSemaphoreGive(dataMutex);
  saveConfig();
  sendOk();
}

static void handleSettings() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "JSON non valido");
    return;
  }
  bool cityChanged;
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  settings.volume = constrain((int)(doc["volume"] | settings.volume), 0, 100);
  settings.brightDay = constrain((int)(doc["brightDay"] | settings.brightDay), 1, 100);
  settings.brightNight = constrain((int)(doc["brightNight"] | settings.brightNight), 0, 100);
  settings.nightStart = constrain((int)(doc["nightStart"] | settings.nightStart), 0, 23);
  settings.nightEnd = constrain((int)(doc["nightEnd"] | settings.nightEnd), 0, 23);
  settings.snoozeMin = constrain((int)(doc["snoozeMin"] | settings.snoozeMin), 1, 30);
  settings.ringMaxMin = constrain((int)(doc["ringMaxMin"] | settings.ringMaxMin), 1, 60);
  const char* city = doc["city"] | settings.city;
  cityChanged = strcmp(city, settings.city) != 0;
  strlcpy(settings.city, city, sizeof(settings.city));
  xSemaphoreGive(dataMutex);
  saveConfig();
  if (cityChanged) weatherRefreshRequested = true;
  sendOk();
}

static void handleGifSelect() {
  String name = cleanGifName(server.arg("name"));
  if (name.isEmpty() || !LittleFS.exists("/gifs/" + name)) {
    server.send(404, "text/plain", "GIF non trovata");
    return;
  }
  xSemaphoreTake(dataMutex, portMAX_DELAY);
  strlcpy(settings.gif, name.c_str(), sizeof(settings.gif));
  xSemaphoreGive(dataMutex);
  saveConfig();
  gifReloadRequested = true;
  sendOk();
}

// ora inviata dal browser del telefono: usata solo se non c'è l'ora da internet
static void handleTime() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "JSON non valido");
    return;
  }
  time_t epoch = doc["epoch"] | (time_t)0;
  bool applied = false;
  if (epoch > 1700000000 && timeSource != TIME_NTP) {
    timeval tv = {epoch, 0};
    settimeofday(&tv, nullptr);
    timeSource = TIME_PHONE;
    applied = true;
    if (!weather.valid) {  // fuso orario non ancora noto dal meteo: usa quello del telefono
      long offsetSec = -(long)(doc["offset"] | 0) * 60;
      const char* tz = tzFromIana(doc["tz"] | "", offsetSec);
      setenv("TZ", tz, 1);
      tzset();
    }
    log_i("Ora impostata dal telefono");
  }
  server.send(200, "application/json", applied ? "{\"applied\":true}" : "{\"applied\":false}");
}

static void handleMessage() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "JSON non valido");
    return;
  }
  String text = doc["text"] | "";
  text.trim();
  if (text.isEmpty()) {
    server.send(400, "text/plain", "Messaggio vuoto");
    return;
  }
  if (!speechEnqueue(text.c_str())) {
    server.send(503, "text/plain", "Troppi messaggi in coda, riprova tra poco");
    return;
  }
  sendOk();
}

static void handleGifLayout() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "JSON non valido");
    return;
  }
  gifLayX = constrain((int)(doc["x"] | 0), -240, 240);
  gifLayY = constrain((int)(doc["y"] | 0), -240, 240);
  gifLayS = constrain((int)(doc["s"] | 100), 20, 400);
  gifLayV = constrain((int)(doc["v"] | 100), 25, 400);
  layoutChanged = true;
  sendOk();
}

static void handleGifDelete() {
  String name = cleanGifName(server.arg("name"));
  if (name.isEmpty() || name == settings.gif) {
    server.send(400, "text/plain", "Non puoi eliminare la GIF in uso");
    return;
  }
  LittleFS.remove("/gifs/" + name);
  LittleFS.remove("/gifs/" + name + ".json");
  sendOk();
}

static void handleGifUploadChunk() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadName = cleanGifName(server.arg("name"));
    uploadOk = !uploadName.isEmpty();
    if (uploadOk) {
      LittleFS.mkdir("/gifs");
      uploadFile = LittleFS.open("/gifs/.upload.tmp", "w");
      uploadOk = (bool)uploadFile;
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadOk && uploadFile.write(up.buf, up.currentSize) != up.currentSize) uploadOk = false;
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) uploadFile.close();
    if (up.status == UPLOAD_FILE_ABORTED) uploadOk = false;
    if (uploadOk) {
      String path = "/gifs/" + uploadName;
      LittleFS.remove(path);
      uploadOk = LittleFS.rename("/gifs/.upload.tmp", path);
      if (uploadOk && uploadName == settings.gif) gifReloadRequested = true;
    } else {
      LittleFS.remove("/gifs/.upload.tmp");
    }
  }
}

static void handleGifUploadDone() {
  if (uploadOk)
    sendOk();
  else
    server.send(500, "text/plain", "Caricamento fallito (memoria piena?)");
}

static void handleScan() {
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (JsonObject o : arr)
      if (ssid == (const char*)o["ssid"]) dup = true;
    if (dup) continue;
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  sendJson(doc);
}

static void handleWifiSave() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) || !(doc["ssid"] | "")[0]) {
    server.send(400, "text/plain", "SSID mancante");
    return;
  }
  saveWifi(doc["ssid"] | "", doc["pass"] | "");
  sendOk();
  delay(800);
  ESP.restart();
}

static void handleNotFound() {
  if (portalActive) {
    // portale "captive": qualsiasi indirizzo porta alla pagina di configurazione
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Non trovato");
  }
}

void webBegin() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/alarms", HTTP_POST, handleAlarms);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/test", HTTP_POST, [] { testAlarmRequested = true; sendOk(); });
  server.on("/api/stop", HTTP_POST, [] { stopAlarmRequested = true; sendOk(); });
  server.on("/api/gif/select", HTTP_POST, handleGifSelect);
  server.on("/api/gif/delete", HTTP_POST, handleGifDelete);
  server.on("/api/gif/layout", HTTP_POST, handleGifLayout);
  server.on("/api/time", HTTP_POST, handleTime);
  server.on("/api/message", HTTP_POST, handleMessage);
  server.on("/api/message/stop", HTTP_POST, [] { speechDismiss(); sendOk(); });
  server.on("/api/gif/upload", HTTP_POST, handleGifUploadDone, handleGifUploadChunk);
  server.on("/api/wifi/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.serveStatic("/gifs/", LittleFS, "/gifs/", "no-cache");
  server.onNotFound(handleNotFound);
  server.begin();
}

void webLoop() { server.handleClient(); }
