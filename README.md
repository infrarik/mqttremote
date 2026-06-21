# MQTT & POST Remote — ESP8266 NodeMCU

> Télécommande à 3 boutons configurables (physiques ou tactiles TTP223), interface web dark mode, appuis simples, doubles et longs, gestion personnalisée des GPIO, sauvegarde/restauration de la configuration, mise à jour OTA et reconnexion automatique. Chaque bouton peut envoyer une trame MQTT, une requête HTTP POST, ou les deux simultanément. Le MQTT peut être désactivé indépendamment.

<img width="480" height="512" alt="image" src="https://github.com/user-attachments/assets/de9eaeff-8d85-443f-a588-02ca9dd466e4" />

---

## Sommaire / Table of Contents

- [Français](#français)
  - [Présentation](#présentation)
  - [Matériel nécessaire](#matériel-nécessaire)
  - [Câblage & Types de Boutons](#câblage--types-de-boutons)
  - [Premier démarrage](#premier-démarrage)
  - [Interface web — page d'accueil](#interface-web--page-daccueil)
  - [Interface web — page de configuration](#interface-web--page-de-configuration)
  - [Gestion des Appuis (Simple, Double, Long)](#gestion-des-appuis-simple-double-long)
  - [Sauvegarde et Restauration de la Configuration](#sauvegarde-et-restauration-de-la-configuration)
  - [Modes d'envoi : MQTT, POST ou les deux](#modes-denvoi--mqtt-post-ou-les-deux)
  - [LED de statut réseau](#led-de-statut-réseau)
  - [Mise à jour OTA](#mise-à-jour-ota)
  - [Bibliothèques requises](#bibliothèques-requises)
- [English](#english)
  - [Overview](#overview)
  - [Required hardware](#required-hardware)
  - [Wiring & Button Types](#wiring--button-types)
  - [First boot](#first-boot)
  - [Web interface — home page](#web-interface--home-page)
  - [Web interface — configuration page](#web-interface--configuration-page)
  - [Press Management (Single, Double, Long)](#press-management-single-double-long)
  - [Configuration Backup and Restore](#configuration-backup-and-restore)
  - [Send modes: MQTT, POST or both](#send-modes-mqtt-post-or-both)
  - [Network status LED](#network-status-led)
  - [OTA firmware update](#ota-firmware-update)
  - [Required libraries](#required-libraries)

---

# Français

## Présentation

**MQTT Remote** transforme un NodeMCU ESP8266 en télécommande autonome à 3 boutons. Chaque bouton peut être configuré individuellement pour envoyer un message **MQTT**, une requête **HTTP POST**, ou **les deux simultanément**. L'appareil est entièrement paramétrable via une interface web embarquée.

**Nouvelles fonctionnalités et améliorations :**
- **Choix du type de bouton :** support des boutons poussoirs physiques traditionnels ou des capteurs tactiles TTP223.
- **Assignation dynamique des GPIO :** configuration des broches (GPIO) de chaque bouton directement depuis l'interface web.
- **Sauvegarde & Restauration :** exportez votre configuration complète dans un fichier JSON pour la sauvegarder ou la dupliquer sur un autre appareil.
- **Gestion avancée des appuis :** détection précise des clics simples, doubles clics et appuis longs.

---

## Matériel nécessaire

| Composant | Détail |
|---|---|
| ESP8266 NodeMCU V3 Lolin | ou toute carte ESP8266 compatible |
| 3 boutons au choix | Boutons poussoirs (contact NO) ou modules tactiles TTP223 |
| Câbles de connexion | — |
| Alimentation 5V micro-USB | ou alimentation externe 3,3 V |

---

## Câblage & Types de Boutons

Le firmware s'adapte au type de bouton sélectionné dans l'interface web :
- **Bouton Poussoir Physique (NO) :** câblé entre la broche GPIO choisie et la **masse (GND)**. La résistance de pull-up interne est activée par le firmware. L'appui ferme le circuit vers GND (actif BAS).
- **Module Tactile TTP223 :** alimenté en VCC (3.3V) et GND. La broche I/O du TTP223 est reliée au GPIO choisi. Par défaut, le TTP223 envoie un signal haut lors du contact (actif HAUT). Le firmware adapte automatiquement la logique de détection selon votre choix.

| Bouton (Par défaut) | Broche NodeMCU | GPIO par défaut |
|--------|---------------|------|
| Bouton 1 (UP) | D1 | GPIO 5 |
| Bouton 2 (MY) | D2 | GPIO 4 |
| Bouton 3 (DOWN) | D5 | GPIO 14 |
| LED de statut | D4 (onboard) | GPIO 2 |

---

## Premier démarrage

Au tout premier démarrage (mémoire vierge), aucun réseau WiFi n'est configuré. L'ESP passe automatiquement en **mode Point d'Accès** :

1. Connectez-vous au réseau WiFi nommé **`button_mqtt`** (sans mot de passe).
2. Ouvrez un navigateur et accédez à **`http://192.168.4.1`**.
3. Cliquez sur **Configurer** pour accéder à la page de configuration.
4. Renseignez votre réseau WiFi, votre broker MQTT, configurez vos boutons et sauvegardez.
5. L'ESP redémarre et se connecte à votre réseau.

---

## Interface web — page d'accueil

La page d'accueil présente :
- **Deux pastilles de statut** en haut : WiFi et MQTT (vert = connecté, rouge = déconnecté, gris = désactivé). L'adresse IP locale est affichée juste en dessous.
- **Un compteur de reconnexions** en cas d'instabilité du réseau.
- **Les 3 boutons configurés** en pleine largeur avec leurs noms et couleurs personnalisés. Un clic virtuel sur l'interface déclenche instantanément l'action configurée, tout comme un appui réel.
- **Un lien ⚙ configuration** en bas de page.

---

## Interface web — page de configuration

L'interface de configuration (`http://<IP>/setup`) est divisée en plusieurs sections :

### ① Réseau WiFi & Broker MQTT
- Sélection du réseau WiFi détecté et saisie du mot de passe WPA2.
- Activation/Désactivation globale du MQTT, adresse du serveur, port, login et mot de passe.

### ② Configuration Matérielle & GPIO (Nouveau)
Pour chaque bouton (1, 2 et 3), vous pouvez définir :
- **Type de Bouton :** Menu déroulant pour choisir entre `Bouton Poussoir (Physique)` et `TTP223 (Tactile)`.
- **GPIO :** Choix du numéro de GPIO assigné à ce bouton (ex: 5, 4, 14, etc.).

### ③ Assignation des Actions par Bouton
Pour chaque bouton, personnalisez le nom (max 12 caractères), la couleur d'affichage, ainsi que les commandes pour les trois types d'appuis :
- **Clic simple :** Topic MQTT, Payload, URL HTTP POST et Body du POST.
- **Double clic :** Topic MQTT, Payload, URL HTTP POST et Body du POST.
- **Appui long :** Topic MQTT, Payload, URL HTTP POST et Body du POST.

### ④ Robustesse réseau & Seuil de temps
- **Seuil appui long (ms) :** durée (300 à 3000 ms, 800 ms par défaut) pour valider un appui long.
- Récapitulatif visuel des patterns de clignotement de la LED.

---

## Gestion des Appuis (Simple, Double, Long)

Chaque bouton physique ou tactile supporte trois actions indépendantes :

- **Clic simple :** Se déclenche immédiatement après l'appui (ou après une légère temporisation pour s'assurer qu'un double clic n'est pas en cours, selon l'implémentation de la bibliothèque).
- **Double clic :** Deux impulsions rapides successives déclenchent cette action spécifique.
- **Appui long :** Si le bouton reste maintenu au-delà du seuil configuré (ex: 800 ms), l'action longue se déclenche une seule fois, sans attendre le relâchement.

---

## Sauvegarde et Restauration de la Configuration

Vous pouvez désormais gérer la configuration de l'appareil sous forme de fichier :
- **Sauvegarder :** Un bouton permet de télécharger un fichier `config.json` contenant l'intégralité de vos réglages (WiFi, MQTT, GPIO, Types de boutons, Actions).
- **Restaurer :** Un champ d'importation vous permet de téléverser un fichier `config.json` précédemment sauvegardé. L'appareil applique les paramètres, les enregistre dans LittleFS et redémarre automatiquement.

---

## Modes d'envoi : MQTT, POST ou les deux

| Mode | Configuration |
|------|--------------|
| **MQTT seul** | Topic renseigné, URL POST vide |
| **HTTP POST seul** | URL POST renseignée, topic vide |
| **MQTT + POST simultanés** | Topic et URL POST tous les deux renseignés |
| **Aucun** | Topic et URL POST vides |

La désactivation globale du MQTT permet de couper les tentatives de connexion au broker sans effacer vos topics configurés.

---

## LED de statut réseau

La LED bleue intégrée (D4, GPIO 2) utilise les cycles suivants :
- **WiFi + MQTT connectés / MQTT désactivé :** Éteinte fixe
- **MQTT déconnecté (WiFi OK) :** Clignotement lent (~0,6 Hz)
- **WiFi déconnecté :** Clignotement rapide (~3,3 Hz)
- **Reconnexion en cours :** Clignotement moyen (~1,6 Hz)

---

## Mise à jour OTA

La mise à jour du firmware se fait directement depuis le navigateur (uniquement disponible en mode Station WiFi) :
1. Générez le fichier `.bin` via PlatformIO (`pio run`) ou Arduino IDE (**Croquis → Exporter les binaires compilées**).
2. Allez dans la section **Mise à jour firmware** sur la page `/setup`.
3. Sélectionnez le fichier `.bin` et cliquez sur **↑ flasher le firmware**.
4. L'ESP redémarre automatiquement une fois le flash validé.

---

## Bibliothèques requises

Ajoutez à votre gestionnaire de bibliothèques ou dans votre `platformio.ini` :

```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
lib_deps =
    bblanchon/ArduinoJson @ ^6
    knolleary/PubSubClient @ ^2
    ftrias/TeenyClickyButtons ; Ou toute bibliothèque équivalente gérant les boutons/TTP223 avec simple/double/long press
