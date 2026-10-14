// Suono della sveglia: codec ES8311 via I2S, "bip-bip-bip-bip" classico
// con volume che cresce gradualmente.
#include <Wire.h>
#include <driver/i2s.h>
#include <AudioGeneratorMP3.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioOutput.h>
#include "app.h"

static const int SAMPLE_RATE = 16000;
static volatile bool playing = false;
static volatile uint32_t playStartMs = 0;
static volatile bool speaking = false;      // la voce sta usando l'I2S
static volatile bool speechAbort = false;

static void es8311Write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static uint8_t es8311Read(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

// Sequenza di inizializzazione ES8311 in modalità slave, MCLK = 256 * 16 kHz,
// derivata dal driver es8311 di Espressif (ESP-ADF).
static void es8311Init() {
  es8311Write(0x45, 0x00);
  es8311Write(0x01, 0x30);
  es8311Write(0x02, 0x00);
  es8311Write(0x03, 0x10);
  es8311Write(0x16, 0x24);
  es8311Write(0x04, 0x10);
  es8311Write(0x05, 0x00);
  es8311Write(0x0B, 0x00);
  es8311Write(0x0C, 0x00);
  es8311Write(0x10, 0x1F);
  es8311Write(0x11, 0x7F);
  es8311Write(0x00, 0x80);
  es8311Write(0x00, es8311Read(0x00) & 0xBF);  // slave
  es8311Write(0x01, 0x3F);                     // clock dal pin MCLK, tutti attivi
  // coefficienti di clock per MCLK 4.096 MHz / 16 kHz
  es8311Write(0x02, 0x00);
  es8311Write(0x05, 0x00);
  es8311Write(0x03, 0x10);
  es8311Write(0x04, 0x10);
  es8311Write(0x07, 0x00);
  es8311Write(0x08, 0xFF);
  es8311Write(0x06, 0x03);
  es8311Write(0x13, 0x10);
  es8311Write(0x1B, 0x0A);
  es8311Write(0x1C, 0x6A);
  // formato I2S 16 bit per DAC (0x09) e ADC (0x0A)
  es8311Write(0x09, 0x0C);
  es8311Write(0x0A, 0x0C);
  // avvio
  es8311Write(0x17, 0xBF);
  es8311Write(0x0E, 0x02);
  es8311Write(0x12, 0x00);
  es8311Write(0x14, 0x1A);
  es8311Write(0x0D, 0x01);
  es8311Write(0x15, 0x40);
  es8311Write(0x37, 0x08);
  es8311Write(0x45, 0x00);
  es8311Write(0x32, 0xBF);  // volume DAC 0 dB (il volume vero è software)
  es8311Write(0x31, 0x00);  // DAC non in muto
}

// Forma d'onda di un "bip": onda quadra arrotondata (fondamentale + 3a armonica).
static inline float beepWave(float phase) {
  return sinf(phase) + 0.33f * sinf(3.0f * phase);
}

static void audioTask(void*) {
  const size_t N = 256;
  static int16_t buf[N * 2];
  const float freq = 2400.0f;
  const float dPhase = 2.0f * PI * freq / SAMPLE_RATE;
  float phase = 0;
  uint32_t sampleIdx = 0;
  bool wasPlaying = false;

  for (;;) {
    if (speaking) {  // la voce ha l'I2S: la sveglia aspetta che si fermi
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    if (!playing) {
      if (wasPlaying) {
        i2s_zero_dma_buffer(I2S_NUM_0);
        digitalWrite(PIN_PA_EN, LOW);
        wasPlaying = false;
      }
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (!wasPlaying) {
      digitalWrite(PIN_PA_EN, HIGH);
      sampleIdx = 0;
      wasPlaying = true;
    }

    // volume: parte dal 25% e arriva al 100% del volume impostato in 30 secondi
    float elapsed = (millis() - playStartMs) / 1000.0f;
    float ramp = 0.25f + 0.75f * fminf(elapsed / 30.0f, 1.0f);
    float gain = (settings.volume / 100.0f) * ramp * 0.55f * 32767.0f;

    for (size_t i = 0; i < N; i++) {
      // schema: 4 bip da 90 ms separati da 70 ms, poi 520 ms di pausa (ciclo 1160 ms)
      uint32_t ms = sampleIdx * 1000UL / SAMPLE_RATE;
      bool on = ms < 640 && (ms % 160) < 90;
      int16_t s = 0;
      if (on) {
        // brevissimo fade per evitare "click"
        uint32_t inBeep = ms % 160;
        float env = inBeep < 5 ? inBeep / 5.0f : (inBeep > 85 ? (90 - inBeep) / 5.0f : 1.0f);
        s = (int16_t)(beepWave(phase) * gain * env * 0.75f);
        phase += dPhase;
        if (phase > 2 * PI) phase -= 2 * PI;
      }
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
      sampleIdx = (sampleIdx + 1) % (1160UL * SAMPLE_RATE / 1000);
    }
    size_t written;
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
  }
}

void audioBegin() {
  pinMode(PIN_PA_EN, OUTPUT);
  digitalWrite(PIN_PA_EN, LOW);

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 6;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);

  i2s_pin_config_t pins = {};
  pins.mck_io_num = PIN_I2S_MCLK;
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_WS;
  pins.data_out_num = PIN_I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);

  delay(10);
  es8311Init();
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 5, nullptr, 0);
}

void audioStart() {
  playStartMs = millis();
  playing = true;
}

void audioStop() { playing = false; }

bool audioIsPlaying() { return playing; }

// =====================================================================
//  Voce: riproduzione di un MP3 in memoria (testo letto da Google TTS)
// =====================================================================
// uscita per ESP8266Audio che scrive direttamente sul nostro I2S
class I2SBridgeOut : public AudioOutput {
  int16_t buf[256 * 2];
  int n = 0;

 public:
  bool begin() override {
    n = 0;
    return true;
  }
  bool SetRate(int hz) override {
    hertz = hz;
    i2s_set_sample_rates(I2S_NUM_0, hz);  // MCLK resta 256*fs: il codec non va riconfigurato
    return true;
  }
  bool ConsumeSample(int16_t s[2]) override {
    MakeSampleStereo16(s);
    buf[n * 2] = Amplify(s[0]);
    buf[n * 2 + 1] = Amplify(s[1]);
    if (++n == 256) flush();
    return true;
  }
  void flush() override {
    if (!n) return;
    size_t w;
    i2s_write(I2S_NUM_0, buf, n * 4, &w, portMAX_DELAY);
    n = 0;
  }
  bool stop() override {
    flush();
    return true;
  }
};

// "din-don" prima del messaggio, a 16 kHz
static void playChime() {
  static int16_t buf[256 * 2];
  const float notes[2] = {1318.5f, 1046.5f};
  const int durMs[2] = {170, 380};
  float gain = settings.volume / 100.0f * 0.45f * 32767;
  for (int k = 0; k < 2 && !speechAbort && !playing; k++) {
    int total = SAMPLE_RATE * durMs[k] / 1000;
    float ph = 0, dph = 2 * PI * notes[k] / SAMPLE_RATE;
    for (int i = 0; i < total;) {
      int n = min(256, total - i);
      for (int j = 0; j < n; j++, i++) {
        float env = expf(-4.0f * i / total) * (i < 80 ? i / 80.0f : 1.0f);
        int16_t v = (int16_t)(sinf(ph) * gain * env);
        ph += dph;
        buf[j * 2] = buf[j * 2 + 1] = v;
      }
      size_t w;
      i2s_write(I2S_NUM_0, buf, n * 4, &w, portMAX_DELAY);
    }
  }
}

bool audioSpeakMp3(const uint8_t* data, size_t len) {
  if (playing) return false;  // la sveglia ha la precedenza
  speechAbort = false;
  speaking = true;
  digitalWrite(PIN_PA_EN, HIGH);
  playChime();

  AudioFileSourcePROGMEM src(data, len);
  AudioGeneratorMP3 mp3;
  I2SBridgeOut out;
  out.SetGain(settings.volume / 100.0f * 1.6f);
  bool ok = mp3.begin(&src, &out);
  while (ok && mp3.isRunning() && !speechAbort && !playing) {
    if (!mp3.loop()) mp3.stop();
  }
  if (mp3.isRunning()) mp3.stop();
  out.flush();
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2s_set_sample_rates(I2S_NUM_0, SAMPLE_RATE);
  if (!playing) digitalWrite(PIN_PA_EN, LOW);
  speaking = false;
  return ok;
}

void audioStopSpeech() { speechAbort = true; }

bool audioIsSpeaking() { return speaking; }
