# PokeSveglia M5 🔥

Versione di PokeSveglia per **M5Stack Core2 for AWS IoT EduKit** (K010-AWS): ESP32, schermo 2" 320×240 touch, RTC, accelerometro, vibrazione e barra di 10 LED nella base.
Ha le stesse funzioni della versione per il robot XiaoZhi (cartella `../PokeSveglia`), adattate allo schermo rettangolare e all'hardware della Core2.

## Cosa fa

| Schermata | Come ci arrivi | Contenuto |
|---|---|---|
| **Orologio** | all'avvio, oppure tasto **B** | GIF animata di sfondo, ora, data, icona della sveglia, orario della prossima sveglia, batteria (con fulmine quando è collegata all'USB) |
| **Meteo** | swipe verso **destra**, oppure tasto **A** | città, temperatura attuale, oggi e domani |
| **Sveglie** | swipe verso **sinistra**, oppure tasto **C** | le 5 sveglie: tocca una riga per attivarla/disattivarla; in basso l'indirizzo web |

I tasti **A / B / C** sono i tre cerchi touch sotto lo schermo.

**Quando la sveglia suona**:
- bip-bip-bip-bip con volume che cresce;
- sulla barra LED una luce gira in tondo e a ogni giro cambia colore: giallo, rosso, verde;
- dopo 10 secondi parte anche la vibrazione.

Per gestirla:
- **tocco breve** sullo schermo, oppure tasto **A** o **C** → posticipa;
- **tocco lungo**, tasto **B**, tasto di accensione o **scuotere** la Core2 → spegne.

**Tasto B tenuto premuto 3 secondi** → attiva la rete di configurazione `PokeSveglia-M5`.

### Differenze rispetto al robot
- **Ora anche da spenta**: la Core2 ha un chip RTC. L'ora resta giusta anche dopo uno spegnimento senza Wi-Fi, quindi le sveglie suonano comunque. L'RTC si riallinea da solo con internet o, in alternativa, con l'ora del telefono quando apri la pagina web.
- **Luce notturna**: dalle 22 la barra LED si accende con una luce ambra tenue (si accende piano in un minuto) e resta accesa fino a quando suona la prima sveglia della notte. Se quella notte non ci sono sveglie, si spegne all'ora di fine notte (di default le 7). Accensione, orario e intensità si regolano in *Impostazioni → Luce notturna*. **Due colpetti sulla sveglia** (sul case, con un dito, a mezzo secondo di distanza) la accendono o la spengono a mano in qualsiasi momento: li riconosce l'accelerometro. Se serve, la sensibilità si regola con `KNOCK_THRESHOLD_G` in `include/board.h`. Il comando manuale resta valido fino al successivo cambio automatico (alle 22 o al suono della sveglia).
- **Accelerometro** sempre presente: scuotere la Core2 spegne la sveglia.
- **Barra LED**: luce che gira (giallo → rosso → verde) quando suona la sveglia e pulsa in blu durante i messaggi vocali.
- Rete e indirizzo diversi, così le due sveglie possono convivere:
  - rete di configurazione `PokeSveglia-M5`;
  - indirizzo `http://pokesveglia-m5.local`.
- Le GIF sono a **320×240**: la pagina web converte da sola quelle che carichi.

## Pagina web
Apri `http://pokesveglia-m5.local` oppure l'IP mostrato nella schermata Sveglie. Le schede sono:
- **Sveglie**;
- **Messaggi**: il testo viene letto ad alta voce;
- **Sfondo**: GIF, posizione e dimensione;
- **Impostazioni**;
- **Wi-Fi**.

## Primo utilizzo
1. Apri **questa cartella** (`m5stack`) in VS Code con PlatformIO.
2. **Platform → Upload Filesystem Image**: carica le GIF di `data/gifs`.
3. **General → Upload**: carica il firmware. La Core2 ha un convertitore USB-seriale, quindi non servono tasti per entrare in modalità di caricamento.
4. Al primo avvio:
   - collegati con il telefono alla rete **PokeSveglia-M5** (password `pokemon123`);
   - apri `http://192.168.4.1` → scheda **Wi-Fi**;
   - scegli la tua rete.

Per aggiungere GIF dal computer: `python3 tools/convert_gif.py mia.gif data/gifs/nome.gif --pixel` (produce 320×240).

## Se qualcosa non va
- **Barra LED spenta**: è nella base AWS (GPIO 25). Con altre basi non c'è.
- **Nessun suono**: controlla il volume in *Impostazioni*.
- **Ora sbagliata dopo uno spegnimento lungo**: apri la pagina web dal telefono oppure collega il Wi-Fi; l'RTC si riallinea da solo.
- **Altri problemi**: il monitor seriale (115200 baud) mostra posizione, fuso orario, meteo ed eventuali errori.
