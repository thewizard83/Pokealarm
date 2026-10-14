#pragma once
// M5Stack Core2 for AWS IoT EduKit (K010-AWS)
// ESP32-D0WDQ6-V3, 16 MB flash, 8 MB PSRAM, schermo 2" 320x240 con touch capacitivo,
// RTC BM8563, IMU (MPU6886 o BMI270), altoparlante NS4168, motore di vibrazione,
// barra di 10 LED SK6812 nella base.
// Schermo, touch, audio, RTC, IMU e alimentazione sono gestiti dalla libreria M5Unified.

#define PIN_LED_BAR 25   // barra LED SK6812 nella base AWS
#define LED_COUNT   10

// Nome della rete di configurazione (diverso dal robot, così possono convivere)
#define AP_SSID     "PokeSveglia-M5"
#define AP_PASSWORD "pokemon123"     // almeno 8 caratteri; "" = rete aperta
#define MDNS_NAME   "pokesveglia-m5" // http://pokesveglia-m5.local

#define SCREEN_W 320
#define SCREEN_H 240
#define MAX_ALARMS 5

// Doppio colpetto sulla sveglia (accende/spegne la luce notturna):
// sensibilità in g. Più basso = basta un colpetto più leggero (ma più falsi allarmi).
#define KNOCK_THRESHOLD_G 0.45f
