// Messaggi inviati dalla pagina web: mostrati sullo schermo e letti ad alta voce.
// La voce è generata dal servizio TTS (non ufficiale) di Google Translate: gratuito,
// senza chiave, ma accetta circa 200 caratteri per richiesta, quindi i testi lunghi
// vengono divisi in pezzi e gli MP3 concatenati.
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "app.h"

static const int MAX_TEXT = 600;
static const int CHUNK = 180;  // byte per richiesta (Google ne accetta ~200)

struct HistItem {
  char text[MAX_TEXT + 1];
  time_t at;
};
static HistItem* history = nullptr;  // in PSRAM, il più recente per primo
static int histCount = 0;
static const int HIST_MAX = 5;

static QueueHandle_t queue;
static SemaphoreHandle_t mtx;
static char current[MAX_TEXT + 1];
static volatile bool showing = false;
static volatile uint32_t shownSince = 0, hideAt = 0;
static char status[64] = "idle";

static void setStatus(const char* s) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  strlcpy(status, s, sizeof(status));
  xSemaphoreGive(mtx);
}

// ------------------------------------------------------------------ download
// Stream che accumula i byte in un buffer in PSRAM (gestisce anche le risposte "chunked")
class PsramSink : public Stream {
 public:
  uint8_t* buf = nullptr;
  size_t len = 0, cap = 0;
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* d, size_t n) override {
    if (len + n > cap) {
      size_t nc = max(cap * 2, len + n + 16384);
      uint8_t* nb = (uint8_t*)ps_realloc(buf, nc);
      if (!nb) return 0;
      buf = nb;
      cap = nc;
    }
    memcpy(buf + len, d, n);
    len += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

static String urlEncode(const char* s, int n) {
  String out;
  for (int i = 0; i < n; i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.') {
      out += (char)c;
    } else {
      char b[4];
      snprintf(b, sizeof(b), "%%%02X", c);
      out += b;
    }
  }
  return out;
}

// fine del prossimo pezzo: preferisce fine frase, poi virgole, poi spazi
static int chunkEnd(const char* s, int start, int len) {
  if (len - start <= CHUNK) return len;
  const char* seps[] = {".!?\n", ",;:", " "};
  for (auto sep : seps) {
    int best = -1;
    for (int i = start + 20; i < start + CHUNK; i++)
      if (strchr(sep, s[i])) best = i + 1;
    if (best > start) return best;
  }
  int e = start + CHUNK;
  while (e > start && ((uint8_t)s[e] & 0xC0) == 0x80) e--;  // non spezzare un carattere UTF-8
  return e;
}

static bool fetchChunk(const char* text, int n, PsramSink& sink) {
  String url = String("https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=") + (uiLang == 1 ? "it" : "en") + "&q=" + urlEncode(text, n);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  http.setUserAgent("Mozilla/5.0");
  int code = http.GET();
  if (code != 200) {
    char b[48];
    snprintf(b, sizeof(b), "error: voice service replied %d", code);
    setStatus(b);
    http.end();
    return false;
  }
  int r = http.writeToStream(&sink);
  http.end();
  if (r <= 0) setStatus("error: download interrupted");
  return r > 0;
}

// ------------------------------------------------------------------ task
static void speechTask(void*) {
  for (;;) {
    char* text;
    xQueueReceive(queue, &text, portMAX_DELAY);
    while (audioIsPlaying()) vTaskDelay(pdMS_TO_TICKS(250));  // aspetta che la sveglia finisca

    xSemaphoreTake(mtx, portMAX_DELAY);
    strlcpy(current, text, sizeof(current));
    xSemaphoreGive(mtx);
    shownSince = millis();
    hideAt = 0;
    showing = true;

    bool ok = WiFi.status() == WL_CONNECTED;
    if (!ok) setStatus("error: no Wi-Fi, no voice");
    PsramSink sink;
    if (ok) {
      setStatus("download");
      int len = strlen(text);
      for (int start = 0; ok && start < len;) {
        int end = chunkEnd(text, start, len);
        while (start < end && text[start] == ' ') start++;
        if (end > start) ok = fetchChunk(text + start, end - start, sink);
        start = end;
      }
    }
    if (ok && sink.len && showing) {
      setStatus("speaking");
      audioSpeakMp3(sink.buf, sink.len);
      setStatus("idle");
    }
    free(sink.buf);
    free(text);
    hideAt = millis() + (ok ? 8000 : 15000);  // il testo resta ancora qualche secondo
  }
}

// ------------------------------------------------------------------ API
void speechTaskStart() {
  queue = xQueueCreate(4, sizeof(char*));
  mtx = xSemaphoreCreateMutex();
  history = (HistItem*)ps_calloc(HIST_MAX, sizeof(HistItem));
  xTaskCreatePinnedToCore(speechTask, "speech", 12288, nullptr, 2, nullptr, 0);
}

bool speechEnqueue(const char* t) {
  int n = strnlen(t, MAX_TEXT);
  while (n > 0 && ((uint8_t)t[n] & 0xC0) == 0x80) n--;  // taglio pulito UTF-8
  if (n == 0) return false;
  char* c = (char*)ps_malloc(n + 1);
  if (!c) return false;
  memcpy(c, t, n);
  c[n] = 0;
  if (xQueueSend(queue, &c, 0) != pdTRUE) {
    free(c);
    return false;
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  memmove(&history[1], &history[0], sizeof(HistItem) * (HIST_MAX - 1));
  strlcpy(history[0].text, c, sizeof(history[0].text));
  history[0].at = time(nullptr);
  histCount = min(histCount + 1, HIST_MAX);
  xSemaphoreGive(mtx);
  return true;
}

bool speechGetCurrent(char* buf, size_t len, uint32_t& since) {
  if (!showing) return false;
  if (hideAt && (int32_t)(millis() - hideAt) >= 0) {
    showing = false;
    return false;
  }
  xSemaphoreTake(mtx, portMAX_DELAY);
  strlcpy(buf, current, len);
  xSemaphoreGive(mtx);
  since = shownSince;
  return true;
}

void speechDismiss() {
  audioStopSpeech();
  showing = false;
}

const char* speechStatus() {
  static char copy[64];
  xSemaphoreTake(mtx, portMAX_DELAY);
  strlcpy(copy, status, sizeof(copy));
  xSemaphoreGive(mtx);
  return copy;
}

void speechHistoryJson(JsonArray arr) {
  xSemaphoreTake(mtx, portMAX_DELAY);
  for (int i = 0; i < histCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["text"] = history[i].text;
    o["at"] = (long)history[i].at;
  }
  xSemaphoreGive(mtx);
}
