# AmuletOS – regole del progetto

Firmware ESP-IDF 5.4.2 + LVGL 9.2.2 per la Waveshare ESP32-S3-Touch-AMOLED-1.75 (anche -B / -G):
AMOLED tondo 466×466 (CO5300 in QSPI), touch CST9217, PMU AXP2101, IO expander TCA9554 (tasto PWR,
reset del GPS), RTC PCF85063, IMU QMI8658, microSD in SDMMC 1-bit. Il ciondolo mostra foto, GIF e
video caricati dal telefono.

## Dove sta cosa

- `main/board.*`, `main/display.*`, `main/input.*`: hardware, schermo (rotazione applicata all'invio
  dei pixel, modalità "direct" per il player), gesti e tasti.
- `main/ui.*`, `main/menu.c`: pila di schermate, overlay (orologio, toast, anelli dei tasti tenuti),
  accensione/blocco schermo, menu tondo (`menu_item_t`, `MENU_SCREEN`).
- `main/apps/`: una schermata per file (home, gallery, pendant = player, upload, clock, settings_app,
  wifi, keyboard, ota_app, quick, info, dice, calendar). Le app nuove vanno in fondo all'elenco di
  `home.c` (la scorciatoia salva l'indice).
- `main/core.*`: logica pura (formato AMJ, nomi file, versioni, fascia notte, query string, dadi,
  batteria, date e festività), coperta
  dai test in `tests/`. `main/media.*`: elenco media sulla SD e decodifica JPEG (`esp_new_jpeg`).
- `main/web.*` + `main/www/*.html`: server e pagine servite dal ciondolo (caricamento con PIN,
  configurazione Wi-Fi). La conversione (ritaglio, GIF, video → AMJ) la fa il browser.
- `main/wifi_mgr.*`, `main/ota.*`: Wi-Fi, hotspot con DNS captive, NTP; aggiornamenti dalle release.
- `main/i18n.*`: testi in italiano e inglese.
- `tools/sim/`: simulatore (il vero codice dell'interfaccia con hardware finto). `sh tools/sim/run.sh`
  scrive in `tools/sim/out/` le schermate e `layout-report.md` (testi fuori dal cerchio). Le modifiche
  all'interfaccia si controllano qui prima dell'hardware; le nuove schermate vanno aggiunte alle scene.
- `tools/mock_device.py`: la pagina di caricamento sul PC con la stessa API (PIN 1234).
- `main/packs.*` + `tools/packs/`: i file delle app che stanno sulla microSD (immagini, testi) sono
  "pacchetti" (`<nome>.amp`, formato in `core.h`) generati dalla CI e allegati alla release; il
  ciondolo li scarica in `/amulet/apps/<nome>/` la prima volta. Grafica e testi nuovi delle app
  vanno lì, non nel firmware. Se cambia un pacchetto si alza la sua versione nell'app.
- `.github/workflows/build.yml`: test + build a ogni push; su `main` release (`amulet.bin`,
  `amulet-app.bin`, `version.json`) e installer web su GitHub Pages.

## Versioni

- Release automatiche `0.1.<numero build>`: la CI scrive `version.txt` (mai in git).
- Build locale senza `version.txt`: `0.1.0-dev`.

## Regole

- Le impostazioni vivono nell'NVS in fondo alla flash (`partitions.csv`): mai cancellare la flash negli
  aggiornamenti; i campi nuovi di `settings_t` vanno sempre in fondo (un blob vecchio si legge sopra i
  default). La tabella partizioni non si cambia senza un piano per le impostazioni.
- I media stanno solo sulla microSD (`/amulet`, anteprime in `/amulet/.thumb`).
- Nuova logica pura: prima in `core.c` con i test, poi interfaccia e hardware.
- Ogni stringa nuova va in `i18n.h` e in `i18n.c` in tutte e due le lingue. Il font copre Latin-1:
  accenti sì, emoji no. Le icone nuove vanno aggiunte in `tools/gen_fonts.py` e in `ui.h`.
- Niente trasformazioni LVGL (`transform_*`, `image_set_scale`), niente `opa` su oggetti interi e
  niente `clip_corner`: creano layer extra, strappano sul pannello (senza TE) e consumano. Usare
  `text_opa` / `bg_opa` / `image_opa`, dimensioni vere e immagini già pronte in memoria.
- Batteria: la CPU va a 240 MHz solo dentro `power_boost_begin/end`; chi scrive la SD da altri task
  (server, OTA) tiene `power_hold`. Niente timer periodici veloci (impediscono il light sleep).
- Le LVGL image che puntano a buffer statici: niente `free` in `leave()` (la vecchia schermata viene
  ancora disegnata durante la dissolvenza).
- Ogni PR aggiunge le sue modifiche in `CHANGELOG.md` (sezione "Next", in inglese). README in inglese.
- Push, merge e release solo quando l'utente lo chiede (ogni merge su `main` diventa una release OTA).

## Build e test

```bash
sh tools/fetch_deps.sh && idf.py set-target esp32s3 && idf.py build
```

```bash
make -C tests && sh tools/sim/run.sh
```

L'hardware (touch, gesti reali, fps del player, consumi, SD, Wi-Fi) resta la prova finale.
