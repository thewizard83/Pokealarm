# PokeSveglia 🔥

Sveglia a tema Pokémon per il robot **XiaoZhi DeepSeek AI** (ESP32-S3, schermo rotondo da 1,28", versione touch con batteria).
Corrisponde alla scheda **Spotpear ESP32-S3-1.28-BOX** del firmware open source Xiaozhi.

## Cosa fa

| Schermata | Come ci arrivi | Contenuto |
|---|---|---|
| **Orologio** | all'avvio | GIF animata di sfondo, ora, data, icona della sveglia (gialla = attiva, barrata = nessuna), orario della prossima sveglia, batteria |
| **Meteo** | swipe verso **destra** | città, temperatura attuale, oggi e domani (icona, max/min, probabilità di pioggia) |
| **Sveglie** | swipe verso **sinistra** | le 5 sveglie: tocca una riga per attivarla/disattivarla; in basso l'indirizzo web |

Dopo 20 secondi senza tocchi torna da sola all'orologio.

**Quando la sveglia suona** (bip-bip-bip-bip classico, volume che cresce in 30 s):
- **tocco breve** → posticipa (5 minuti, regolabile)
- **tocco lungo (1 s)**, **tasto BOOT** o **scuotere il robot** (solo se ha l'accelerometro) → spegne
- si spegne da sola dopo 10 minuti (regolabile)

**Tasto BOOT tenuto premuto 3 secondi** → attiva la rete di configurazione `PokeSveglia-Setup`.

### Ora, posizione e meteo
- Ora da internet (NTP). Se il Wi-Fi cade, l'orologio interno continua e le sveglie suonano lo stesso.
- Senza internet:
  - dopo un **riavvio** (crash, cambio rete) l'ora viene mantenuta;
  - dopo uno **spegnimento** basta aprire la pagina web della sveglia dal telefono (anche dalla rete `PokeSveglia-Setup`) e l'ora del telefono viene copiata sulla sveglia.
- Posizione ricavata dall'IP della connessione (ip-api.com). Il meteo arriva da Open-Meteo. Nessuno dei due servizi richiede registrazione o chiavi API.
- Il fuso orario, ora legale compresa, si imposta da solo in base alla posizione.
- Se la città rilevata è sbagliata (per esempio con una VPN), puoi scriverla a mano in *Impostazioni → Meteo*.

## Pagina web

Collegati a **http://pokesveglia.local** oppure all'IP mostrato nella schermata Sveglie. Le sezioni sono:
- **Sveglie**: orario, giorni della settimana, etichetta, attiva/disattiva, prova suono.
- **Messaggi**: scrivi un testo (fino a 600 caratteri). La sveglia suona un "din-don", lo mostra sullo schermo e lo legge ad alta voce. La voce arriva dal servizio di sintesi vocale di Google Translate: è gratuito, ma non ufficiale e Google potrebbe cambiarlo. Tocca lo schermo per chiudere il messaggio.
- **Sfondo**: le GIF in memoria (con anteprima animata), *Usa* / *Elimina*, e **Carica una nuova GIF**. Il browser la converte da solo a 240×240 per lo schermo rotondo. Opzioni: intera o a riempimento, sfondo, pixel art.
- **Impostazioni**: volume, posticipo, luminosità di giorno e di notte con i relativi orari, città del meteo.
- **Wi-Fi**: ricerca delle reti e cambio rete.

## Primo utilizzo

1. **Backup del firmware originale** (consigliato, per poter tornare indietro). Collega il robot via USB ed esegui:
   ```bash
   ~/.platformio/penv/bin/python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 read_flash 0 0x1000000 backup_xiaozhi.bin
   ```
   Per ripristinarlo usa lo stesso comando con `write_flash 0 backup_xiaozhi.bin` al posto di `read_flash ...`
2. Apri questa cartella in **VS Code** con l'estensione **PlatformIO**.
3. Nella barra di PlatformIO (icona della formica) → *pokesveglia* → **Platform → Upload Filesystem Image**. In questo modo carichi le GIF della cartella `data/gifs`.
4. Poi **General → Upload** per caricare il firmware.
   Se il caricamento non parte, tieni premuto **BOOT**, collega il cavo USB e rilascia: così il chip entra in modalità download.
5. Al primo avvio compare "Configura il Wi-Fi":
   - collegati con il telefono alla rete **PokeSveglia-Setup** (password `pokemon123`);
   - si apre la pagina di configurazione (altrimenti vai su `http://192.168.4.1`);
   - nella scheda **Wi-Fi** scegli la tua rete, inserisci la password e tocca *Connetti e riavvia*.

## Aggiungere GIF dal computer (facoltativo)
Oltre che dalla pagina web, puoi mettere le GIF nella cartella `data/gifs/` e ripetere il passo 3. Per convertirle:
```bash
python3 tools/convert_gif.py mia.gif data/gifs/pikachu.gif --pixel
```

## Se qualcosa non va
Tutte le regolazioni sono in `include/board.h`:

| Problema | Soluzione |
|---|---|
| Immagine specchiata o capovolta | cambia `LCD_ROTATION` (prova 0, 2, 4…) |
| Rosso e blu scambiati (Charizard azzurro) | `LCD_BGR` → `false` |
| Colori "in negativo" | `LCD_INVERT` → `false` |
| Gli swipe vanno al contrario | `TOUCH_FLIP_X` → `true` |
| Toccando una riga si attiva un'altra sveglia | `TOUCH_FLIP_Y` → `true` |
| Non si connette al Wi-Fi | all'accensione fa 3 tentativi, poi riprova da sola ogni 20 s; dopo 3 minuti attiva anche la rete `PokeSveglia-Setup`. Nella schermata Sveglie compare il motivo ("rete non trovata", "password errata?") |
| Nessun suono | controlla il volume nella pagina web; da *Monitor* verifica che non ci siano errori I2C |

Il monitor seriale (115200 baud) mostra posizione, fuso orario, meteo ed eventuali errori.
