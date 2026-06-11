# MQTT Remote — ESP8266 NodeMCU

> Télécommande MQTT à 3 boutons physiques, interface web dark mode, appuis longs configurables, mise à jour OTA et reconnexion automatique.

---

## Sommaire / Table of Contents

- [Français](#français)
  - [Présentation](#présentation)
  - [Matériel nécessaire](#matériel-nécessaire)
  - [Câblage](#câblage)
  - [Premier démarrage](#premier-démarrage)
  - [Interface web — page d'accueil](#interface-web--page-daccueil)
  - [Interface web — page de configuration](#interface-web--page-de-configuration)
  - [Appuis courts et appuis longs](#appuis-courts-et-appuis-longs)
  - [Personnalisation des boutons](#personnalisation-des-boutons)
  - [LED de statut réseau](#led-de-statut-réseau)
  - [Reconnexion automatique](#reconnexion-automatique)
  - [Mise à jour OTA](#mise-à-jour-ota)
  - [Bibliothèques requises](#bibliothèques-requises)
- [English](#english)
  - [Overview](#overview)
  - [Required hardware](#required-hardware)
  - [Wiring](#wiring)
  - [First boot](#first-boot)
  - [Web interface — home page](#web-interface--home-page)
  - [Web interface — configuration page](#web-interface--configuration-page)
  - [Short press and long press](#short-press-and-long-press)
  - [Button customisation](#button-customisation)
  - [Network status LED](#network-status-led)
  - [Automatic reconnection](#automatic-reconnection)
  - [OTA firmware update](#ota-firmware-update)
  - [Required libraries](#required-libraries)

---

# Français

## Présentation

**MQTT Remote** transforme un NodeMCU ESP8266 en télécommande MQTT autonome à 3 boutons physiques. Chaque bouton envoie un message MQTT configurable sur un topic de votre choix. L'appareil est entièrement paramétrable via une interface web embarquée, accessible depuis n'importe quel navigateur sur le réseau local.

Conçu à l'origine pour piloter des volets roulants Somfy via [ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS), le projet est générique et fonctionne avec tout broker MQTT (Mosquitto, Home Assistant, Jeedom, Node-RED, etc.).

**Fonctionnalités principales :**

- 3 boutons physiques avec appui court et appui long indépendants
- Interface web dark mode responsive (mobile et desktop)
- Noms, couleurs et topics MQTT entièrement configurables par bouton
- Indication visuelle de l'état WiFi et MQTT (pastilles vertes/rouges)
- Reconnexion WiFi et MQTT automatique avec backoff exponentiel
- LED onboard codant l'état réseau par clignotement
- Mise à jour du firmware en ligne (OTA) via fichier `.bin`
- Configuration persistante dans LittleFS (résiste aux coupures de courant)

---

## Matériel nécessaire

| Composant | Détail |
|---|---|
| ESP8266 NodeMCU V3 Lolin | ou toute carte ESP8266 compatible |
| 3 boutons poussoirs | contact NO (normalement ouvert) |
| Câbles de connexion | — |
| Alimentation 5V micro-USB | ou alimentation externe 3,3 V |

---

## Câblage

Chaque bouton est câblé entre la broche et la masse (GND). Les résistances de pull-up internes sont activées par le firmware.

| Bouton | Broche NodeMCU | GPIO |
|--------|---------------|------|
| Bouton 1 (UP par défaut) | D1 | GPIO 5 |
| Bouton 2 (MY par défaut) | D2 | GPIO 4 |
| Bouton 3 (DOWN par défaut) | D5 | GPIO 14 |
| LED de statut | D4 (onboard) | GPIO 2 |

La LED D4 est la LED bleue intégrée au module NodeMCU. Aucun câblage supplémentaire n'est nécessaire pour elle.

> **Important :** les boutons doivent relier la broche à GND lors de l'appui. Ne pas mettre de tension sur les broches.

---

## Premier démarrage

Au tout premier démarrage (mémoire vierge), aucun réseau WiFi n'est configuré. L'ESP passe automatiquement en **mode Point d'Accès** :

1. Sur votre téléphone ou ordinateur, connectez-vous au réseau WiFi nommé **`button_mqtt`** (pas de mot de passe).
2. Ouvrez un navigateur et accédez à **`http://192.168.4.1`**.
3. Cliquez sur **Configurer** pour accéder à la page de configuration.
4. Renseignez votre réseau WiFi et votre broker MQTT, puis sauvegardez.
5. L'ESP redémarre et se connecte à votre réseau.

Si la connexion WiFi échoue (mauvais mot de passe, réseau absent), l'ESP repasse automatiquement en mode Point d'Accès pour vous permettre de reconfigurer.

---

## Interface web — page d'accueil

Une fois connecté sur votre réseau, accédez à l'ESP via son adresse IP (affichée dans le moniteur série, ou retrouvable dans votre box/routeur).

La page d'accueil présente :

- **Deux pastilles de statut** en haut : WiFi (vert = connecté, rouge = déconnecté) et MQTT (vert = connecté, rouge = déconnecté). L'adresse IP locale est affichée sous les pastilles.
- **Un compteur de reconnexions** apparaît si des coupures réseau ont eu lieu depuis le dernier démarrage.
- **Les 3 boutons** en pleine largeur, avec leur nom et leur couleur personnalisés. Un clic déclenche l'envoi du message MQTT associé, exactement comme un appui physique sur le bouton correspondant.
- **Un lien ⚙ configuration** en bas de page.

---

## Interface web — page de configuration

Accessible via `http://<IP>/setup` ou le lien en bas de la page d'accueil.

La page est divisée en cinq sections :

### ① Réseau WiFi

- **Réseau détecté :** liste déroulante des réseaux WiFi visibles, rafraîchie à chaque chargement de la page. Sélectionnez votre réseau dans la liste.
- **Mot de passe WPA2 :** le mot de passe de votre réseau WiFi.

### ② Broker MQTT

- **Serveur :** adresse IP ou nom d'hôte du broker MQTT (ex. `192.168.1.10` ou `homeassistant.local`).
- **Port :** port du broker (1883 par défaut).
- **Login / Mot de passe :** identifiants si votre broker requiert une authentification. Laisser vide si le broker est anonyme.

### ③ Boutons

Pour chacun des 3 boutons, les champs suivants sont disponibles :

| Champ | Description |
|-------|-------------|
| **Nom affiché** | Texte du bouton sur la page d'accueil (ex. `OUVRE`, `STOP`, `FERME`). Maximum 12 caractères. |
| **Couleur** | Couleur du bouton : Vert, Blanc, Rouge, Bleu, Orange. La pastille de prévisualisation se met à jour en temps réel. |
| **Topic court** | Topic MQTT sur lequel publier lors d'un appui court. |
| **Payload court** | Message envoyé lors d'un appui court. |
| **Topic long** | Topic MQTT pour l'appui long. Laisser vide pour désactiver l'appui long sur ce bouton. |
| **Payload long** | Message envoyé lors d'un appui long. |

### ④ Robustesse réseau

- **Seuil appui long (ms) :** durée en millisecondes à partir de laquelle un appui est considéré comme long (valeur entre 300 et 3000 ms, 800 ms par défaut). Ce réglage s'applique aux 3 boutons simultanément.
- Un récapitulatif visuel des patterns de clignotement de la LED de statut est affiché dans cette section.

### ⑤ Mise à jour firmware

Visible uniquement en mode station (non disponible en mode AP). Voir la section [Mise à jour OTA](#mise-à-jour-ota).

### Sauvegarde

Le bouton **💾 sauvegarder et redémarrer** enregistre tous les paramètres dans la mémoire flash (LittleFS) et redémarre l'ESP. Les paramètres sont conservés même en cas de coupure de courant.

---

## Appuis courts et appuis longs

Chaque bouton physique supporte deux actions indépendantes :

### Appui court
L'action courte se déclenche **immédiatement à l'appui** (front descendant). Le message MQTT configuré dans "Topic court" / "Payload court" est publié.

### Appui long
Si le bouton reste enfoncé au-delà du seuil configuré (800 ms par défaut), l'action longue se déclenche **une seule fois**, sans attendre le relâchement. Si le topic long est vide, rien n'est envoyé.

> **Exemple d'utilisation Somfy :**
> - Appui court UP → `{"command":"up"}` → monte le volet
> - Appui long UP → `{"command":"my"}` → position mémorisée (montée)
> - Appui court DOWN → `{"command":"down"}` → descend le volet
> - Appui long DOWN → `{"command":"my"}` → position mémorisée (descente)
> - Appui court MY → `{"command":"my"}` → stop / position MY
> - Appui long MY → `{"command":"reset_motor"}` → recalibration moteur

---

## Personnalisation des boutons

Depuis la page de configuration, section ③ :

- **Nom :** tapez le texte à afficher (ex. `SALON`, `TOUT`, `BUREAU`). Le nom est mis à jour sur la page d'accueil après sauvegarde.
- **Couleur :** choisissez parmi 5 couleurs. La pastille de prévisualisation dans le setup reflète le choix en temps réel avant sauvegarde.
- **Topic et payload :** adaptez à votre infrastructure MQTT. Exemples compatibles ESPSomfy-RTS :
  - Topic : `/shades/1/command`
  - Payload court : `up` / `stop` / `down`
  - Payload long : `my`

---

## LED de statut réseau

La LED bleue intégrée (D4, GPIO 2) indique l'état réseau par son comportement :

| État | Clignotement | Fréquence |
|------|-------------|-----------|
| WiFi + MQTT connectés | Éteinte fixe | — |
| MQTT déconnecté (WiFi OK) | Lent | ~0,6 Hz (800 ms) |
| WiFi déconnecté | Rapide | ~3,3 Hz (150 ms) |
| Reconnexion en cours | Moyen | ~1,6 Hz (300 ms) |

La LED est à logique inversée (active LOW) : elle s'allume quand le GPIO est à l'état bas.

---

## Reconnexion automatique

L'ESP surveille en permanence l'état du WiFi et du MQTT. En cas de coupure :

**WiFi perdu :**
La reconnexion est tentée avec un backoff exponentiel : 5 s, puis 10 s, 20 s, et plafonne à 30 s entre chaque tentative. L'ESP ne redémarre jamais automatiquement pour cause de perte réseau.

**MQTT déconnecté (WiFi intact) :**
Une tentative de reconnexion au broker est effectuée toutes les 5 secondes.

À chaque reconnexion réussie après une coupure, le compteur de reconnexions est incrémenté et affiché sur la page d'accueil, pour tracer les instabilités réseau.

---

## Mise à jour OTA

La mise à jour du firmware se fait sans démontage ni câble USB, directement depuis le navigateur.

**Générer le fichier `.bin` :**

Avec PlatformIO :
```
pio run
```
Le fichier est généré dans `.pio/build/<env>/firmware.bin`.

Avec Arduino IDE : menu **Croquis → Exporter les binaires compilées**. Le `.bin` apparaît dans le dossier du sketch.

**Procédure de mise à jour :**

1. Accédez à la page de configuration (`/setup`).
2. Faites défiler jusqu'à la section ⑤ **Mise à jour firmware**.
3. Cliquez sur **choisir un .bin** et sélectionnez le fichier.
4. Cliquez sur **↑ flasher le firmware**.
5. La barre de progression indique l'avancement de l'upload.
6. À 100 %, le message **✓ Flash OK — redémarrage...** confirme la réussite. L'ESP redémarre automatiquement sur le nouveau firmware.

> La mise à jour OTA n'est disponible que lorsque l'ESP est connecté à un réseau WiFi (mode station). Elle n'est pas accessible en mode Point d'Accès.

---

## Bibliothèques requises

À installer depuis le gestionnaire de bibliothèques Arduino IDE ou via `platformio.ini` :

| Bibliothèque | Auteur |
|---|---|
| `ESP8266WiFi` | ESP8266 Arduino Core |
| `ESP8266WebServer` | ESP8266 Arduino Core |
| `ESP8266HTTPUpdateServer` | ESP8266 Arduino Core |
| `ArduinoJson` | Benoit Blanchon |
| `PubSubClient` | Nick O'Leary |
| `LittleFS` | ESP8266 Arduino Core |

Dans `platformio.ini` :
```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
lib_deps =
    bblanchon/ArduinoJson @ ^6
    knolleary/PubSubClient @ ^2
```

---
---

# English

## Overview

**MQTT Remote** turns an ESP8266 NodeMCU into a standalone 3-button MQTT remote control. Each button publishes a configurable MQTT message to a topic of your choice. The device is fully configured through a built-in web interface, accessible from any browser on the local network.

Originally designed to control Somfy roller blinds via [ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS), the project is generic and works with any MQTT broker (Mosquitto, Home Assistant, Jeedom, Node-RED, etc.).

**Key features:**

- 3 physical buttons with independent short press and long press actions
- Responsive dark mode web interface (mobile and desktop)
- Button names, colours and MQTT topics fully configurable per button
- Visual WiFi and MQTT status indicators (green/red badges)
- Automatic WiFi and MQTT reconnection with exponential backoff
- Onboard LED encoding network status by blink pattern
- Over-the-air firmware update (OTA) via `.bin` file
- Persistent configuration in LittleFS (survives power loss)

---

## Required hardware

| Component | Details |
|---|---|
| ESP8266 NodeMCU V3 Lolin | or any compatible ESP8266 board |
| 3 push buttons | NO (normally open) contact |
| Jumper wires | — |
| 5V micro-USB power supply | or 3.3 V external supply |

---

## Wiring

Each button is wired between the pin and GND. Internal pull-up resistors are enabled by the firmware.

| Button | NodeMCU pin | GPIO |
|--------|------------|------|
| Button 1 (UP by default) | D1 | GPIO 5 |
| Button 2 (MY by default) | D2 | GPIO 4 |
| Button 3 (DOWN by default) | D5 | GPIO 14 |
| Status LED | D4 (onboard) | GPIO 2 |

The D4 LED is the blue LED built into the NodeMCU module. No additional wiring is needed for it.

> **Important:** buttons must connect the pin to GND when pressed. Do not apply voltage to the pins.

---

## First boot

On the very first boot (blank flash), no WiFi network is configured. The ESP automatically enters **Access Point mode**:

1. On your phone or computer, connect to the WiFi network named **`button_mqtt`** (no password).
2. Open a browser and go to **`http://192.168.4.1`**.
3. Click **Configure** to access the configuration page.
4. Enter your WiFi network and MQTT broker details, then save.
5. The ESP reboots and connects to your network.

If the WiFi connection fails (wrong password, network not found), the ESP automatically switches back to Access Point mode so you can reconfigure.

---

## Web interface — home page

Once connected to your network, access the ESP via its IP address (shown in the serial monitor, or found in your router's device list).

The home page shows:

- **Two status badges** at the top: WiFi (green = connected, red = disconnected) and MQTT (green = connected, red = disconnected). The local IP address is displayed below the badges.
- **A reconnection counter** appears if network drops have occurred since the last boot.
- **The 3 buttons** full-width, with their custom names and colours. A click triggers the associated MQTT message, exactly like pressing the corresponding physical button.
- **A ⚙ configuration link** at the bottom of the page.

---

## Web interface — configuration page

Accessible via `http://<IP>/setup` or the link at the bottom of the home page.

The page is divided into five sections:

### ① WiFi network

- **Detected network:** dropdown list of visible WiFi networks, refreshed on every page load. Select your network from the list.
- **WPA2 password:** your WiFi network password.

### ② MQTT broker

- **Server:** IP address or hostname of the MQTT broker (e.g. `192.168.1.10` or `homeassistant.local`).
- **Port:** broker port (1883 by default).
- **Login / Password:** credentials if your broker requires authentication. Leave blank for anonymous brokers.

### ③ Buttons

For each of the 3 buttons, the following fields are available:

| Field | Description |
|-------|-------------|
| **Display name** | Button label on the home page (e.g. `OPEN`, `STOP`, `CLOSE`). Maximum 12 characters. |
| **Colour** | Button colour: Green, White, Red, Blue, Orange. The preview dot updates in real time. |
| **Short topic** | MQTT topic to publish to on a short press. |
| **Short payload** | Message sent on a short press. |
| **Long topic** | MQTT topic for the long press. Leave blank to disable long press on this button. |
| **Long payload** | Message sent on a long press. |

### ④ Network robustness

- **Long press threshold (ms):** duration in milliseconds above which a press is considered long (between 300 and 3000 ms, default 800 ms). This setting applies to all 3 buttons simultaneously.
- A visual summary of the status LED blink patterns is displayed in this section.

### ⑤ Firmware update

Visible only in station mode (not available in AP mode). See the [OTA firmware update](#ota-firmware-update) section.

### Saving

The **💾 save and reboot** button writes all settings to flash memory (LittleFS) and reboots the ESP. Settings are preserved even after a power cut.

---

## Short press and long press

Each physical button supports two independent actions:

### Short press
The short action fires **immediately on press** (falling edge). The MQTT message configured in "Short topic" / "Short payload" is published.

### Long press
If the button is held beyond the configured threshold (800 ms by default), the long action fires **once**, without waiting for release. If the long topic is empty, nothing is sent.

> **Example Somfy usage:**
> - Short press UP → `{"command":"up"}` → raise the blind
> - Long press UP → `{"command":"my"}` → memorised position (up side)
> - Short press DOWN → `{"command":"down"}` → lower the blind
> - Long press DOWN → `{"command":"my"}` → memorised position (down side)
> - Short press MY → `{"command":"my"}` → stop / MY position
> - Long press MY → `{"command":"reset_motor"}` → motor recalibration

---

## Button customisation

From the configuration page, section ③:

- **Name:** type the label to display (e.g. `LIVING`, `ALL`, `OFFICE`). The name is updated on the home page after saving.
- **Colour:** choose from 5 colours. The preview dot in the setup reflects the choice in real time before saving.
- **Topic and payload:** adapt to your MQTT infrastructure. ESPSomfy-RTS compatible examples:
  - Topic: `/shades/1/command`
  - Short payload: `up` / `stop` / `down`
  - Long payload: `my`

---

## Network status LED

The built-in blue LED (D4, GPIO 2) indicates network status by its blink behaviour:

| State | Blink | Frequency |
|-------|-------|-----------|
| WiFi + MQTT connected | Steady off | — |
| MQTT disconnected (WiFi OK) | Slow blink | ~0.6 Hz (800 ms) |
| WiFi disconnected | Fast blink | ~3.3 Hz (150 ms) |
| Reconnection in progress | Medium blink | ~1.6 Hz (300 ms) |

The LED is active LOW: it lights up when the GPIO is driven low.

---

## Automatic reconnection

The ESP continuously monitors WiFi and MQTT status. On a drop:

**WiFi lost:**
Reconnection is attempted with exponential backoff: 5 s, then 10 s, 20 s, capped at 30 s between attempts. The ESP never automatically reboots due to a network loss.

**MQTT disconnected (WiFi intact):**
A reconnection attempt to the broker is made every 5 seconds.

Each successful reconnection after a drop increments the reconnection counter displayed on the home page, helping track network instability.

---

## OTA firmware update

Firmware updates are done without disassembly or USB cable, directly from the browser.

**Generating the `.bin` file:**

With PlatformIO:
```
pio run
```
The file is generated at `.pio/build/<env>/firmware.bin`.

With Arduino IDE: menu **Sketch → Export Compiled Binary**. The `.bin` appears in the sketch folder.

**Update procedure:**

1. Go to the configuration page (`/setup`).
2. Scroll down to section ⑤ **Firmware update**.
3. Click **choose a .bin** and select the file.
4. Click **↑ flash firmware**.
5. The progress bar shows upload progress.
6. At 100%, the message **✓ Flash OK — rebooting...** confirms success. The ESP automatically reboots on the new firmware.

> OTA updates are only available when the ESP is connected to a WiFi network (station mode). They are not accessible in Access Point mode.

---

## Required libraries

Install via the Arduino IDE library manager or `platformio.ini`:

| Library | Author |
|---|---|
| `ESP8266WiFi` | ESP8266 Arduino Core |
| `ESP8266WebServer` | ESP8266 Arduino Core |
| `ESP8266HTTPUpdateServer` | ESP8266 Arduino Core |
| `ArduinoJson` | Benoit Blanchon |
| `PubSubClient` | Nick O'Leary |
| `LittleFS` | ESP8266 Arduino Core |

In `platformio.ini`:
```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
lib_deps =
    bblanchon/ArduinoJson @ ^6
    knolleary/PubSubClient @ ^2
```

---

*MQTT Remote — ESP8266 NodeMCU — licence MIT*
