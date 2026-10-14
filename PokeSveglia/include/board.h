#pragma once
// Piedinatura del robot XiaoZhi "Ball" (Spotpear ESP32-S3-1.28-BOX).
// Fonte: firmware open source xiaozhi-esp32, main/boards/spotpear/sp-esp32-s3-1.28-box/config.h

// --- Schermo rotondo GC9A01 240x240 (SPI) ---
#define PIN_LCD_SCLK 4
#define PIN_LCD_MOSI 2
#define PIN_LCD_CS   5
#define PIN_LCD_DC   47
#define PIN_LCD_RST  38
#define PIN_LCD_BL   42      // retroilluminazione, attiva BASSA

// --- Touch CST816D (I2C) ---
#define PIN_TP_SDA 11
#define PIN_TP_SCL 7
#define PIN_TP_RST 6
#define PIN_TP_INT 12
#define TP_ADDR    0x15

// --- Audio: codec ES8311 + amplificatore ---
#define PIN_I2S_MCLK 16
#define PIN_I2S_BCLK 9
#define PIN_I2S_WS   45
#define PIN_I2S_DOUT 8       // ESP32 -> codec (altoparlante)
#define PIN_I2S_DIN  10      // codec -> ESP32 (microfono, non usato)
#define PIN_PA_EN    46      // abilita l'amplificatore
#define PIN_CODEC_SDA 15
#define PIN_CODEC_SCL 14
#define ES8311_ADDR  0x18

// --- Alimentazione ---
#define PIN_POWER_HOLD 3     // va tenuto ALTO per restare accesi a batteria
#define PIN_BAT_ADC    1     // tensione batteria (ADC1 canale 0)
#define PIN_CHARGING   41    // ALTO = in carica
#define PIN_BOOT_BTN   0

// ----------------------------------------------------------------------
// Regolazioni dello schermo e del touch.
// Se l'immagine appare specchiata o con rosso e blu invertiti
// (Charizard azzurro), modifica questi valori e ricarica il firmware.
// ----------------------------------------------------------------------
#define LCD_ROTATION   0     // 6 = specchiato in orizzontale come il firmware originale; prova 0 se è al contrario
#define LCD_BGR        false // verificato sul robot: con true rosso e blu erano scambiati
#define LCD_INVERT     true  // metti false se i colori sembrano un negativo
#define TOUCH_FLIP_X   false // metti true se gli swipe vanno nella direzione sbagliata
#define TOUCH_FLIP_Y   false // metti true se toccando una sveglia si attiva quella "speculare"

// Nome della rete di configurazione
#define AP_SSID     "PokeSveglia-Setup"
#define AP_PASSWORD "pokemon123"   // almeno 8 caratteri; "" = rete aperta
#define MDNS_NAME   "pokesveglia"  // http://pokesveglia.local

#define SCREEN_W 240
#define SCREEN_H 240
#define MAX_ALARMS 5
