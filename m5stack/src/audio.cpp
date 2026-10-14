// Audio con M5Unified (altoparlante NS4168 del Core2):
//  - sveglia: "bip-bip-bip-bip" classico con volume che cresce gradualmente
//  - voce: MP3 dei messaggi (ESP8266Audio) inviato all'altoparlante M5
#include <M5Unified.h>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioOutput.h>
#include "app.h"

static const int SAMPLE_RATE = 16000;
static const int CYCLE_MS = 1160;  // 4 bip da 90 ms ogni 160 ms, poi pausa
static const int CYCLE = CYCLE_MS * SAMPLE_RATE / 1000;
static const int CH_ALARM = 0, CH_VOICE = 1;

static int16_t* beepBuf = nullptr;
static volatile bool playing = false;
static volatile uint32_t playStartMs = 0;
static volatile bool speaking = false;
static volatile bool speechAbort = false;

static void buildBeep() {
  beepBuf = (int16_t*)ps_malloc(CYCLE * sizeof(int16_t));
  const float freq = 2400.0f, dPhase = 2.0f * PI * freq / SAMPLE_RATE;
  float phase = 0;
  for (int i = 0; i < CYCLE; i++) {
    uint32_t ms = (uint32_t)i * 1000 / SAMPLE_RATE;
    bool on = ms < 640 && (ms % 160) < 90;
    int16_t s = 0;
    if (on) {
      uint32_t inBeep = ms % 160;
      float env = inBeep < 5 ? inBeep / 5.0f : (inBeep > 85 ? (90 - inBeep) / 5.0f : 1.0f);
      s = (int16_t)((sinf(phase) + 0.33f * sinf(3 * phase)) * env * 0.75f * 32767 * 0.7f);
      phase += dPhase;
      if (phase > 2 * PI) phase -= 2 * PI;
    }
    beepBuf[i] = s;
  }
}

void audioBegin() {
  M5.Speaker.begin();
  M5.Speaker.setVolume(0);
  buildBeep();
}

// volume: parte dal 25% e arriva al 100% del volume impostato in 30 secondi
void audioLoop() {
  static uint32_t last = 0;
  if (!playing || millis() - last < 200) return;
  last = millis();
  float ramp = 0.25f + 0.75f * fminf((millis() - playStartMs) / 30000.0f, 1.0f);
  M5.Speaker.setVolume((uint8_t)(settings.volume * 2.55f * ramp));
}

void audioStart() {
  speechAbort = true;  // la sveglia ha la precedenza sulla voce
  playStartMs = millis();
  playing = true;
  M5.Speaker.stop(CH_VOICE);
  M5.Speaker.setVolume((uint8_t)(settings.volume * 2.55f * 0.25f));
  // ripete il ciclo all'infinito (in pratica): si ferma con audioStop()
  M5.Speaker.playRaw(beepBuf, CYCLE, SAMPLE_RATE, false, 100000, CH_ALARM, true);
}

void audioStop() {
  playing = false;
  M5.Speaker.stop(CH_ALARM);
}

bool audioIsPlaying() { return playing; }

// =====================================================================
//  Voce
// =====================================================================
// uscita per ESP8266Audio verso M5.Speaker (tre buffer a rotazione)
class AudioOutputM5Speaker : public AudioOutput {
  static constexpr size_t TRI = 1536;
  int16_t buf[3][TRI];
  size_t idx = 0, which = 0;

 public:
  bool begin() override { return true; }
  bool ConsumeSample(int16_t s[2]) override {
    if (idx + 2 > TRI) {
      flush();
      return false;  // il generatore riproverà questo campione
    }
    MakeSampleStereo16(s);
    buf[which][idx++] = s[0];
    buf[which][idx++] = s[1];
    return true;
  }
  void flush() override {
    if (!idx) return;
    M5.Speaker.playRaw(buf[which], idx, hertz, true, 1, CH_VOICE);
    which = (which + 1) % 3;
    idx = 0;
  }
  bool stop() override {
    flush();
    return true;
  }
};

// "din-don" prima del messaggio
static void playChime() {
  static int16_t* chime = nullptr;
  static int len = 0;
  if (!chime) {
    const float notes[2] = {1318.5f, 1046.5f};
    const int durMs[2] = {170, 380};
    len = SAMPLE_RATE * (durMs[0] + durMs[1]) / 1000;
    chime = (int16_t*)ps_malloc(len * 2);
    int p = 0;
    for (int k = 0; k < 2; k++) {
      int total = SAMPLE_RATE * durMs[k] / 1000;
      float ph = 0, dph = 2 * PI * notes[k] / SAMPLE_RATE;
      for (int i = 0; i < total; i++, ph += dph) {
        float env = expf(-4.0f * i / total) * (i < 80 ? i / 80.0f : 1.0f);
        chime[p++] = (int16_t)(sinf(ph) * env * 0.5f * 32767);
      }
    }
  }
  M5.Speaker.playRaw(chime, len, SAMPLE_RATE, false, 1, CH_VOICE);
  while (M5.Speaker.isPlaying(CH_VOICE) && !speechAbort && !playing) vTaskDelay(pdMS_TO_TICKS(10));
}

bool audioSpeakMp3(const uint8_t* data, size_t len) {
  if (playing) return false;  // la sveglia ha la precedenza
  speechAbort = false;
  speaking = true;
  M5.Speaker.setVolume((uint8_t)(settings.volume * 2.55f));
  playChime();

  AudioFileSourcePROGMEM src(data, len);
  AudioGeneratorMP3 mp3;
  static AudioOutputM5Speaker out;  // statica: i buffer devono restare validi durante la riproduzione
  bool ok = mp3.begin(&src, &out);
  while (ok && mp3.isRunning() && !speechAbort && !playing) {
    if (!mp3.loop()) mp3.stop();
  }
  if (mp3.isRunning()) mp3.stop();
  out.flush();
  while (M5.Speaker.isPlaying(CH_VOICE) && !speechAbort && !playing) vTaskDelay(pdMS_TO_TICKS(10));
  if (speechAbort || playing) M5.Speaker.stop(CH_VOICE);
  speaking = false;
  return ok;
}

void audioStopSpeech() { speechAbort = true; }

bool audioIsSpeaking() { return speaking; }
