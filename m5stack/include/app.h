#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "board.h"

// Giorni: bit0 = lunedì ... bit6 = domenica. 0 = una volta sola.
struct Alarm {
  bool enabled = false;
  uint8_t hour = 7;
  uint8_t minute = 0;
  uint8_t days = 0x1F;  // lun-ven
  char label[24] = "";
};

struct Settings {
  uint8_t volume = 80;       // 0-100
  uint8_t brightDay = 100;   // 0-100
  uint8_t brightNight = 10;  // 0-100
  uint8_t nightStart = 23;   // ora inizio luminosità notturna
  uint8_t nightEnd = 7;      // ora fine luminosità notturna
  uint8_t snoozeMin = 5;
  uint8_t ringMaxMin = 10;   // dopo quanti minuti la sveglia si spegne da sola
  bool nightLight = true;    // luce notturna sulla barra LED
  uint8_t nightLightStart = 22;  // ora di accensione
  uint8_t nightLightLevel = 25;  // intensità 1-100
  char city[48] = "";        // vuoto = automatico dall'IP
  char gif[40] = "charizard.gif";
  uint8_t lang = 0;          // lingua: 0 = automatica (dal paese), 1 = inglese, 2 = italiano
};

struct DayWeather {
  int code = -1;
  float tmax = 0, tmin = 0;
  int rain = 0;  // probabilità di pioggia %
};

struct WeatherData {
  bool valid = false;
  char city[48] = "";
  float tempNow = 0;
  int codeNow = -1;
  DayWeather day[2];
  time_t updated = 0;
};

extern Alarm alarms[MAX_ALARMS];
extern Settings settings;
extern WeatherData weather;
extern SemaphoreHandle_t dataMutex;  // protegge alarms / settings / weather

extern volatile bool gifReloadRequested;
extern volatile bool weatherRefreshRequested;
extern volatile bool testAlarmRequested;
extern volatile bool stopAlarmRequested;
extern volatile bool portalActive;
extern bool hasAccelerometer;

// da dove arriva l'ora attuale
enum { TIME_NONE = 0, TIME_NTP = 1, TIME_RTC = 2, TIME_PHONE = 3 };
extern volatile uint8_t timeSource;
extern volatile bool rtcWriteRequested;  // copia l'ora attuale nel chip RTC

// posizione e dimensione della GIF sullo schermo (salvate per ogni GIF)
extern volatile int gifLayX;   // spostamento orizzontale in pixel (+ = destra)
extern volatile int gifLayY;   // spostamento verticale in pixel (+ = giù)
extern volatile int gifLayS;   // dimensione in % (100 = originale)
extern volatile int gifLayV;   // velocità in % (100 = originale)
extern volatile bool layoutChanged;

// storage (main.cpp)
void saveConfig();
void loadConfig();
void saveWifi(const String& ssid, const String& pass);

// web.cpp
void webBegin();
void webLoop();

// net.cpp
void netTaskStart();
const char* tzFromIana(const char* iana, long offsetSec);

// audio.cpp
void audioBegin();
void audioLoop();  // volume crescente della sveglia
void audioStart();
void audioStop();
bool audioIsPlaying();
bool audioSpeakMp3(const uint8_t* data, size_t len);  // bloccante, da chiamare in un task
void audioStopSpeech();
bool audioIsSpeaking();

// speech.cpp: messaggi letti ad alta voce
bool speechEnqueue(const char* text);  // false se la coda è piena
void speechTaskStart();
bool speechGetCurrent(char* buf, size_t len, uint32_t& shownSince);  // messaggio da mostrare
void speechDismiss();
const char* speechStatus();     // "idle" / "download" / "voce" / "errore: ..."
void speechHistoryJson(JsonArray arr);

// lingua dell'interfaccia: 0 = inglese (predefinita), 1 = italiano
extern volatile uint8_t uiLang;
static inline const char* T(const char* en, const char* it) { return uiLang == 1 ? it : en; }
void applyLanguage();                    // ricalcola uiLang da impostazione e paese
void setDetectedCountry(const char* cc);  // codice paese ISO (es. "IT") rilevato dalla posizione

// main.cpp
void startPortal();
String ipString();
bool batteryInfo(int& level, bool& charging, bool& usb);
