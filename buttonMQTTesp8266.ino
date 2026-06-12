#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <PubSubClient.h>

// ------------------------------------------------------------------ broches
#define PIN_D1  5
#define PIN_D2  4
#define PIN_D5  14
#define PIN_LED 2    // D4 = GPIO2, LED onboard NodeMCU (active LOW)

const int btn1_pin = PIN_D1;
const int btn2_pin = PIN_D2;
const int btn3_pin = PIN_D5;

// ------------------------------------------------------------------ config
char wifi_ssid[32]  = "";
char wifi_pass[64]  = "";
char mqtt_server[40]= "";
char mqtt_port[6]   = "1883";
char mqtt_user[40]  = "";
char mqtt_pass[40]  = "";

// Bouton 1
char btn1_topic[60]        = "esp/btn1/topic";
char btn1_payload[100]     = "message_btn1";
char btn1_name[20]         = "UP";
char btn1_color[10]        = "green";
char btn1_long_topic[60]   = "";
char btn1_long_payload[100]= "";
char btn1_long_mode[6]     = "both";   // "both" = court+long, "long" = long seul

// Bouton 2
char btn2_topic[60]        = "esp/btn2/topic";
char btn2_payload[100]     = "message_btn2";
char btn2_name[20]         = "MY";
char btn2_color[10]        = "white";
char btn2_long_topic[60]   = "";
char btn2_long_payload[100]= "";
char btn2_long_mode[6]     = "both";

// Bouton 3
char btn3_topic[60]        = "esp/btn3/topic";
char btn3_payload[100]     = "message_btn3";
char btn3_name[20]         = "DOWN";
char btn3_color[10]        = "red";
char btn3_long_topic[60]   = "";
char btn3_long_payload[100]= "";
char btn3_long_mode[6]     = "both";

// Seuil appui long en ms (configurable)
unsigned int long_press_ms = 800;

// ------------------------------------------------------------------ état réseau
enum NetState { NET_OK, NET_MQTT_KO, NET_WIFI_KO, NET_RECONNECTING };
NetState net_state = NET_RECONNECTING;

unsigned long last_wifi_attempt  = 0;
unsigned long last_mqtt_attempt  = 0;
unsigned long wifi_backoff_ms    = 5000;
unsigned int  reconnect_count    = 0;

// Clignotement LED non bloquant
unsigned long last_led_toggle    = 0;
bool          led_state          = false;

// ------------------------------------------------------------------ debounce & états filtrés
bool last_raw_btn1_state = HIGH;
bool last_raw_btn2_state = HIGH;
bool last_raw_btn3_state = HIGH;

bool debounced_btn1_state = HIGH;
bool debounced_btn2_state = HIGH;
bool debounced_btn3_state = HIGH;

unsigned long last_debounce_time1 = 0;
unsigned long last_debounce_time2 = 0;
unsigned long last_debounce_time3 = 0;
const unsigned long debounce_delay = 50;

// Timestamps d'appui pour détection appui long
unsigned long press_start1 = 0;
unsigned long press_start2 = 0;
unsigned long press_start3 = 0;

bool long_fired1 = false;
bool long_fired2 = false;
bool long_fired3 = false;

bool pressed1 = false;
bool pressed2 = false;
bool pressed3 = false;

// ------------------------------------------------------------------ objets
ESP8266WebServer         server(80);
ESP8266HTTPUpdateServer  httpUpdater;
WiFiClient               esp_client;
PubSubClient             mqtt_client(esp_client);

bool is_ap_mode = false;

// ================================================================== helpers couleur

String color_styles(const char* c) {
  String s = String(c);
  if (s == "green")  return "background:#1b4332;border:1px solid #2d6a4f;color:#52c98a;";
  if (s == "red")    return "background:#3b0d0d;border:1px solid #6b1717;color:#e05555;";
  if (s == "blue")   return "background:#0d1b3e;border:1px solid #1a3a7a;color:#5b8dee;";
  if (s == "orange") return "background:#2b1500;border:1px solid #6b3700;color:#e09b40;";
  return "background:#2a2a2a;border:1px solid #555;color:#eeeeee;";
}

String color_hex(const char* c) {
  String s = String(c);
  if (s == "green")  return "#52c98a";
  if (s == "red")    return "#e05555";
  if (s == "blue")   return "#5b8dee";
  if (s == "orange") return "#e09b40";
  return "#cccccc";
}

String btn_icon(int n) {
  if (n == 1) return "&#9650;";
  if (n == 3) return "&#9660;";
  return "&#9632;";
}

// ================================================================== config JSON

void load_config() {
  if (!LittleFS.begin()) return;
  if (!LittleFS.exists("/config.json")) return;
  File f = LittleFS.open("/config.json", "r");
  if (!f) return;
  StaticJsonDocument<2048> json;
  if (deserializeJson(json, f) == DeserializationError::Ok) {
    strlcpy(wifi_ssid,           json["wifi_ssid"]           | "",              sizeof(wifi_ssid));
    strlcpy(wifi_pass,           json["wifi_pass"]           | "",              sizeof(wifi_pass));
    strlcpy(mqtt_server,         json["mqtt_server"]         | "",              sizeof(mqtt_server));
    strlcpy(mqtt_port,           json["mqtt_port"]           | "1883",          sizeof(mqtt_port));
    strlcpy(mqtt_user,           json["mqtt_user"]           | "",              sizeof(mqtt_user));
    strlcpy(mqtt_pass,           json["mqtt_pass"]           | "",              sizeof(mqtt_pass));
    strlcpy(btn1_topic,          json["btn1_topic"]          | "esp/btn1/topic",sizeof(btn1_topic));
    strlcpy(btn1_payload,        json["btn1_payload"]        | "message_btn1",  sizeof(btn1_payload));
    strlcpy(btn1_name,           json["btn1_name"]           | "UP",            sizeof(btn1_name));
    strlcpy(btn1_color,          json["btn1_color"]          | "green",         sizeof(btn1_color));
    strlcpy(btn1_long_topic,     json["btn1_long_topic"]     | "",              sizeof(btn1_long_topic));
    strlcpy(btn1_long_payload,   json["btn1_long_payload"]   | "",              sizeof(btn1_long_payload));
    strlcpy(btn1_long_mode,      json["btn1_long_mode"]      | "both",          sizeof(btn1_long_mode));
    strlcpy(btn2_topic,          json["btn2_topic"]          | "esp/btn2/topic",sizeof(btn2_topic));
    strlcpy(btn2_payload,        json["btn2_payload"]        | "message_btn2",  sizeof(btn2_payload));
    strlcpy(btn2_name,           json["btn2_name"]           | "MY",            sizeof(btn2_name));
    strlcpy(btn2_color,          json["btn2_color"]          | "white",         sizeof(btn2_color));
    strlcpy(btn2_long_topic,     json["btn2_long_topic"]     | "",              sizeof(btn2_long_topic));
    strlcpy(btn2_long_payload,   json["btn2_long_payload"]   | "",              sizeof(btn2_long_payload));
    strlcpy(btn2_long_mode,      json["btn2_long_mode"]      | "both",          sizeof(btn2_long_mode));
    strlcpy(btn3_topic,          json["btn3_topic"]          | "esp/btn3/topic",sizeof(btn3_topic));
    strlcpy(btn3_payload,        json["btn3_payload"]        | "message_btn3",  sizeof(btn3_payload));
    strlcpy(btn3_name,           json["btn3_name"]           | "DOWN",          sizeof(btn3_name));
    strlcpy(btn3_color,          json["btn3_color"]          | "red",           sizeof(btn3_color));
    strlcpy(btn3_long_topic,     json["btn3_long_topic"]     | "",              sizeof(btn3_long_topic));
    strlcpy(btn3_long_payload,   json["btn3_long_payload"]   | "",              sizeof(btn3_long_payload));
    strlcpy(btn3_long_mode,      json["btn3_long_mode"]      | "both",          sizeof(btn3_long_mode));
    long_press_ms = json["long_press_ms"] | 800;
  }
  f.close();
}

void save_config() {
  StaticJsonDocument<2048> json;
  json["wifi_ssid"]         = wifi_ssid;
  json["wifi_pass"]         = wifi_pass;
  json["mqtt_server"]       = mqtt_server;
  json["mqtt_port"]         = mqtt_port;
  json["mqtt_user"]         = mqtt_user;
  json["mqtt_pass"]         = mqtt_pass;
  json["btn1_topic"]        = btn1_topic;
  json["btn1_payload"]      = btn1_payload;
  json["btn1_name"]         = btn1_name;
  json["btn1_color"]        = btn1_color;
  json["btn1_long_topic"]   = btn1_long_topic;
  json["btn1_long_payload"] = btn1_long_payload;
  json["btn1_long_mode"]    = btn1_long_mode;
  json["btn2_topic"]        = btn2_topic;
  json["btn2_payload"]      = btn2_payload;
  json["btn2_name"]         = btn2_name;
  json["btn2_color"]        = btn2_color;
  json["btn2_long_topic"]   = btn2_long_topic;
  json["btn2_long_payload"] = btn2_long_payload;
  json["btn2_long_mode"]    = btn2_long_mode;
  json["btn3_topic"]        = btn3_topic;
  json["btn3_payload"]      = btn3_payload;
  json["btn3_name"]         = btn3_name;
  json["btn3_color"]        = btn3_color;
  json["btn3_long_topic"]   = btn3_long_topic;
  json["btn3_long_payload"] = btn3_long_payload;
  json["btn3_long_mode"]    = btn3_long_mode;
  json["long_press_ms"]     = long_press_ms;
  File f = LittleFS.open("/config.json", "w");
  if (f) { serializeJson(json, f); f.close(); }
}

// ================================================================== LED statut

void update_led() {
  unsigned long now = millis();
  unsigned long interval = 0;

  switch (net_state) {
    case NET_OK:           interval = 0;           break;
    case NET_MQTT_KO:      interval = 800;         break;
    case NET_WIFI_KO:      interval = 150;         break;
    case NET_RECONNECTING: interval = 300;         break;
  }

  if (interval == 0) {
    digitalWrite(PIN_LED, HIGH);
    led_state = false;
    return;
  }

  if (now - last_led_toggle >= interval) {
    last_led_toggle = now;
    led_state = !led_state;
    digitalWrite(PIN_LED, led_state ? LOW : HIGH);
  }
}

// ================================================================== pages web

void handle_root() {
  bool wifi_ok = (WiFi.status() == WL_CONNECTED);
  bool mqtt_ok = mqtt_client.connected();

  String html = F("<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>t&eacute;l&eacute;commande</title>"
    "<style>"
    "body{margin:0;padding:0;background:#111827;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;display:flex;flex-direction:column;align-items:center;min-height:100vh;}"
    ".card{width:92%;max-width:340px;margin:0 auto;}"
    ".hdr{padding:14px 0 10px;width:92%;max-width:340px;display:flex;flex-direction:column;align-items:center;gap:6px;}"
    ".status-row{display:flex;gap:8px;}"
    ".pill{display:flex;align-items:center;gap:5px;font-size:12px;padding:4px 10px;border-radius:99px;font-weight:500;}"
    ".pill-ok{background:#0d2b1a;color:#52c98a;border:1px solid #1e5c33;}"
    ".pill-err{background:#2b0d0d;color:#e05555;border:1px solid #5c1e1e;}"
    ".dot{width:7px;height:7px;border-radius:50%;}"
    ".dot-ok{background:#52c98a;}"
    ".dot-err{background:#e05555;}"
    ".ip{font-size:11px;color:#4b5563;margin-top:2px;}"
    ".recon{font-size:10px;color:#374151;margin-top:1px;}"
    ".btns{display:flex;flex-direction:column;gap:12px;padding:8px 0 16px;width:100%;}"
    ".btn{width:100%;padding:22px 0;border-radius:14px;border:none;cursor:pointer;font-size:24px;font-weight:500;letter-spacing:0.02em;display:flex;flex-direction:column;align-items:center;gap:4px;box-sizing:border-box;}"
    ".btn-icon{font-size:13px;opacity:0.5;}"
    ".btn:active{opacity:0.75;transform:scale(0.98);}"
    ".footer{padding:12px 0 24px;border-top:1px solid #1f2937;width:100%;text-align:center;}"
    ".footer a{color:#374151;font-size:13px;text-decoration:none;}"
    "</style></head><body>");
  html += F("<div class='hdr'><div class='status-row'>");
  html += "<div class='pill " + String(wifi_ok ? "pill-ok" : "pill-err") + "'>";
  html += "<div class='dot " + String(wifi_ok ? "dot-ok" : "dot-err") + "'></div>WiFi</div>";
  html += "<div class='pill " + String(mqtt_ok ? "pill-ok" : "pill-err") + "'>";
  html += "<div class='dot " + String(mqtt_ok ? "dot-ok" : "dot-err") + "'></div>MQTT</div>";
  html += F("</div>");
  if (wifi_ok) html += "<div class='ip'>" + WiFi.localIP().toString() + "</div>";
  if (reconnect_count > 0)
    html += "<div class='recon'>reconnexions : " + String(reconnect_count) + "</div>";
  html += F("</div>");
  html += F("<div class='card'><div class='btns'>");
  html += "<button class='btn' style='" + color_styles(btn1_color) + "' onclick='pub(1)'>";
  html += "<span class='btn-icon'>" + btn_icon(1) + "</span><span>" + String(btn1_name) + "</span></button>";
  html += "<button class='btn' style='" + color_styles(btn2_color) + "' onclick='pub(2)'>";
  html += "<span class='btn-icon'>" + btn_icon(2) + "</span><span>" + String(btn2_name) + "</span></button>";
  html += "<button class='btn' style='" + color_styles(btn3_color) + "' onclick='pub(3)'>";
  html += "<span class='btn-icon'>" + btn_icon(3) + "</span><span>" + String(btn3_name) + "</span></button>";
  html += F("</div>");
  html += F("<div class='footer'><a href='/setup'>&#9881; configuration</a></div>");
  html += F("</div>");
  html += F("<script>function pub(id){var x=new XMLHttpRequest();x.open('GET','/trigger?id='+id,true);x.send();}</script>");
  html += F("</body></html>");
  server.send(200, "text/html", html);
}

// ---------- helpers setup ----------

String color_option(const char* val, const char* lbl, const char* cur) {
  return "<option value='" + String(val) + "'" + (strcmp(val, cur) == 0 ? " selected" : "") + ">" + String(lbl) + "</option>";
}

String btn_card(int n, const char* pin_lbl, const char* name, const char* color, const char* topic, const char* payload, const char* ltopic, const char* lpayload, const char* lmode) {
  String p   = String(n);
  String dot = "dot" + p;
  String s;
  s += "<div class='btn-card'>";
  s += "<div class='btn-card-hdr'><div class='clr-dot' id='" + dot + "' style='background:" + color_hex(color) + "'></div>";
  s += "Bouton " + p + " &mdash; " + String(pin_lbl) + "</div>";

  s += "<div class='grid2'>";
  s += "<div class='row'><label>Nom affich&eacute;</label><input type='text' name='b" + p + "_name' value='" + String(name) + "' maxlength='12'></div>";
  s += "<div class='row'><label>Couleur</label><select name='b" + p + "_color' onchange='upd(this,\"" + dot + "\")'>";
  s += color_option("green",  "Vert",   color);
  s += color_option("white",  "Blanc",  color);
  s += color_option("red",    "Rouge",  color);
  s += color_option("blue",   "Bleu",   color);
  s += color_option("orange", "Orange", color);
  s += "</select></div></div>";

  s += "<div class='row'><label>Topic court</label><input type='text' name='b" + p + "_t' value='" + String(topic) + "'></div>";
  s += "<div class='row'><label>Payload court</label><input type='text' name='b" + p + "_p' value='" + String(payload) + "'></div>";
  s += "<div class='long-hdr'>&#128336; appui long</div>";
  s += "<div class='row'><label>Topic long <span class='opt'>(optionnel)</span></label><input type='text' name='b" + p + "_lt' value='" + String(ltopic) + "' placeholder='laisser vide pour d&eacute;sactiver'></div>";
  s += "<div class='row'><label>Payload long</label><input type='text' name='b" + p + "_lp' value='" + String(lpayload) + "'></div>";
  s += "<div class='row'><label>Mode</label><select name='b" + p + "_lm'>";
  s += "<option value='both'" + String(strcmp(lmode,"both")==0?" selected":"") + ">Court + Long (deux actions)</option>";
  s += "<option value='long'" + String(strcmp(lmode,"long")==0?" selected":"") + ">Long seul (le court attend le rel&acirc;chement)</option>";
  s += "</select></div>";
  s += "</div>";
  return s;
}

void handle_setup() {
  int n = WiFi.scanNetworks();
  String html = F("<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>configuration</title>"
    "<style>"
    "body{margin:0;padding:0 0 40px;background:#f5f5f3;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#111;}"
    ".topbar{background:#fff;border-bottom:1px solid #e5e7eb;padding:12px 16px;display:flex;align-items:center;justify-content:space-between;position:sticky;top:0;z-index:10;}"
    ".topbar-title{font-size:15px;font-weight:500;}"
    ".back{font-size:13px;color:#3b82f6;text-decoration:none;}"
    ".section{background:#fff;border-radius:12px;margin:14px 12px 0;padding:14px 16px;border:1px solid #e5e7eb;}"
    ".sec-hdr{font-size:11px;font-weight:600;letter-spacing:0.07em;text-transform:uppercase;margin-bottom:12px;padding-bottom:8px;border-bottom:1px solid #f0f0f0;}"
    ".sec-net{color:#3b82f6;}.sec-mqtt{color:#f59e0b;}.sec-btns{color:#10b981;}.sec-rob{color:#ec4899;}.sec-ota{color:#8b5cf6;}"
    ".row{margin-bottom:12px;}"
    "label{display:block;font-size:12px;color:#6b7280;margin-bottom:4px;}"
    ".opt{color:#9ca3af;font-style:italic;}"
    "input,select{width:100%;box-sizing:border-box;padding:9px 10px;border:1px solid #d1d5db;border-radius:8px;font-size:14px;background:#fafafa;color:#111;appearance:auto;}"
    "input:focus,select:focus{outline:none;border-color:#3b82f6;background:#fff;}"
    ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px;}"
    ".btn-card{background:#f9f9f7;border:1px solid #e5e7eb;border-radius:10px;padding:12px 14px;margin-bottom:10px;}"
    ".btn-card-hdr{display:flex;align-items:center;gap:8px;margin-bottom:10px;font-size:13px;font-weight:500;}"
    ".clr-dot{width:10px;height:10px;border-radius:50%;flex-shrink:0;}"
    ".long-hdr{font-size:11px;font-weight:600;color:#ec4899;letter-spacing:0.05em;text-transform:uppercase;margin:10px 0 8px;padding-top:10px;border-top:1px dashed #e5e7eb;}"
    ".save{display:block;width:calc(100% - 24px);margin:16px 12px 0;padding:14px;background:#10b981;color:#fff;border:none;border-radius:10px;font-size:15px;font-weight:500;cursor:pointer;text-align:center;}"
    ".save:active{background:#059669;}"
    "</style></head><body>");
  html += F("<div class='topbar'><a class='back' href='/'>&#8592; retour</a><span class='topbar-title'>Configuration</span><span></span></div>");

  html += F("<div class='section'><div class='sec-hdr sec-net'>&#9312; r&eacute;seau wifi</div>");
  html += F("<div class='row'><label>R&eacute;seau d&eacute;tect&eacute;</label><select name='wifi_ssid' id='ssid_sel'>");
  if (n == 0) {
    html += F("<option value=''>aucun r&eacute;seau trouv&eacute;</option>");
  } else {
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      String sel  = (ssid == String(wifi_ssid)) ? " selected" : "";
      html += "<option value='" + ssid + "'" + sel + ">" + ssid + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
    }
  }
  html += F("</select></div>");
  html += "<div class='row'><label>Mot de passe WPA2</label><input type='password' name='wifi_pass' id='wp' value='" + String(wifi_pass) + "'></div>";
  html += F("</div>");

  html += F("<div class='section'><div class='sec-hdr sec-mqtt'>&#9313; broker mqtt</div>");
  html += F("<div class='grid2'>");
  html += "<div class='row'><label>Serveur</label><input type='text' name='mqtt_server' value='" + String(mqtt_server) + "'></div>";
  html += "<div class='row'><label>Port</label><input type='text' name='mqtt_port' value='" + String(mqtt_port) + "'></div>";
  html += F("</div><div class='grid2'>");
  html += "<div class='row'><label>Login</label><input type='text' name='mqtt_user' value='" + String(mqtt_user) + "'></div>";
  html += "<div class='row'><label>Mot de passe</label><input type='password' name='mqtt_pass' value='" + String(mqtt_pass) + "'></div>";
  html += F("</div></div>");

  html += F("<div class='section'><div class='sec-hdr sec-btns'>&#9314; boutons</div>");
  html += btn_card(1, "D1", btn1_name, btn1_color, btn1_topic, btn1_payload, btn1_long_topic, btn1_long_payload, btn1_long_mode);
  html += btn_card(2, "D2", btn2_name, btn2_color, btn2_topic, btn2_payload, btn2_long_topic, btn2_long_payload, btn2_long_mode);
  html += btn_card(3, "D5", btn3_name, btn3_color, btn3_topic, btn3_payload, btn3_long_topic, btn3_long_payload, btn3_long_mode);
  html += F("</div>");

  html += F("<div class='section'><div class='sec-hdr sec-rob'>&#9315; robustesse r&eacute;seau</div>");
  html += "<div class='row'><label>Seuil appui long (ms)</label><input type='number' name='long_ms' value='" + String(long_press_ms) + "' min='300' max='3000'></div>";
  html += F("<div class='row'><label style='color:#6b7280;font-size:12px;'>LED de statut sur D4 (onboard) :</label>");
  html += F("<div style='font-size:12px;color:#374151;background:#f3f4f6;border-radius:6px;padding:8px 10px;'>");
  html += F("&#9679; WiFi+MQTT OK &rarr; LED &eacute;teinte<br>");
  html += F("&#9679; MQTT d&eacute;connect&eacute; &rarr; clignotement lent (0.6 Hz)<br>");
  html += F("&#9679; WiFi coup&eacute; &rarr; clignotement rapide (3 Hz)<br>");
  html += F("&#9679; Reconnexion &rarr; clignotement moyen (1.6 Hz)");
  html += F("</div></div></div>");

  if (!is_ap_mode) {
    html += F("<div class='section'><div class='sec-hdr sec-ota'>&#9316; mise &agrave; jour firmware</div>");
    html += F("<p style='font-size:13px;color:#6b7280;margin:0 0 12px;'>Chargez un fichier <b>.bin</b> g&eacute;n&eacute;r&eacute; par PlatformIO ou Arduino IDE.</p>");
    html += F("<div id='ota-area'>");
    html += F("<div style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;'>");
    html += F("<label for='ota-file' style='display:inline-flex;align-items:center;gap:6px;padding:9px 14px;"
              "background:#f0f4ff;border:1px solid #c7d7fd;border-radius:8px;cursor:pointer;font-size:13px;color:#3b5bdb;'>"
              "&#128193; choisir un .bin</label>");
    html += F("<input type='file' id='ota-file' accept='.bin' style='display:none;'>");
    html += F("<span id='ota-fname' style='font-size:12px;color:#9ca3af;'>aucun fichier s&eacute;lectionn&eacute;</span>");
    html += F("</div>");
    html += F("<div id='ota-progress-wrap' style='display:none;margin-top:12px;'>");
    html += F("<div style='background:#e5e7eb;border-radius:99px;height:8px;overflow:hidden;'>");
    html += F("<div id='ota-bar' style='background:#3b82f6;height:8px;width:0%;transition:width 0.3s;'></div></div>");
    html += F("<div id='ota-status' style='font-size:12px;color:#6b7280;margin-top:6px;text-align:center;'>0%</div></div>");
    html += F("<button id='ota-btn' onclick='doOta()' style='margin-top:12px;width:100%;padding:11px;background:#3b82f6;"
              "color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:500;cursor:pointer;'>"
              "&#8593; flasher le firmware</button>");
    html += F("</div></div>");
  }

  html += F("<button class='save' onclick='doSave()'>&#128190; sauvegarder et red&eacute;marrer</button>");
  html += F("<script>"
    "var colorMap={green:'#52c98a',white:'#cccccc',red:'#e05555',blue:'#5b8dee',orange:'#e09b40'};"
    "function upd(sel,dotId){var d=document.getElementById(dotId);if(d)d.style.background=colorMap[sel.value]||'#ccc';}"

    "var fi=document.getElementById('ota-file');"
    "if(fi)fi.addEventListener('change',function(){"
      "var fn=document.getElementById('ota-fname');"
      "if(fn)fn.textContent=this.files[0]?this.files[0].name:'aucun fichier s\u00e9lectionn\u00e9';"
    "});"

    "function doOta(){"
      "var fi=document.getElementById('ota-file');"
      "if(!fi||!fi.files||!fi.files[0]){alert('S\u00e9lectionnez un fichier .bin');return;}"
      "var f=fi.files[0];"
      "if(!f.name.endsWith('.bin')){alert('Le fichier doit \u00eatre un .bin');return;}"
      "document.getElementById('ota-btn').disabled=true;"
      "document.getElementById('ota-btn').textContent='Envoi en cours...';"
      "document.getElementById('ota-progress-wrap').style.display='block';"
      "var fd=new FormData();fd.append('firmware',f,f.name);"
      "var x=new XMLHttpRequest();x.open('POST','/update',true);"
      "x.upload.onprogress=function(e){"
        "if(e.lengthComputable){"
          "var pct=Math.round(e.loaded/e.total*100);"
          "document.getElementById('ota-bar').style.width=pct+'%';"
          "document.getElementById('ota-status').textContent=pct+'%';"
        "}"
      "};"
      "x.onload=function(){"
        "if(x.status==200){"
          "document.getElementById('ota-area').innerHTML="
            "'<div style=\"color:#10b981;font-size:15px;font-weight:500;text-align:center;padding:12px 0\">"
            "&#10003; Flash OK &mdash; red\u00e9marrage...</div>';"
        "}else{"
          "document.getElementById('ota-status').textContent='Erreur HTTP '+x.status;"
          "document.getElementById('ota-btn').disabled=false;"
          "document.getElementById('ota-btn').textContent='\u2191 flasher le firmware';"
        "}"
      "};"
      "x.onerror=function(){"
        "document.getElementById('ota-status').textContent='Erreur r\u00e9seau';"
        "document.getElementById('ota-btn').disabled=false;"
        "document.getElementById('ota-btn').textContent='\u2191 flasher le firmware';"
      "};"
      "x.send(fd);"
    "}"

    "function doSave(){"
      "var params=["
        "'wifi_ssid='+encodeURIComponent(document.getElementById('ssid_sel').value),"
        "'wifi_pass='+encodeURIComponent(document.getElementById('wp').value)"
      "];"
      "var names=['mqtt_server','mqtt_port','mqtt_user','mqtt_pass','long_ms',"
        "'b1_name','b1_color','b1_t','b1_p','b1_lt','b1_lp','b1_lm',"
        "'b2_name','b2_color','b2_t','b2_p','b2_lt','b2_lp','b2_lm',"
        "'b3_name','b3_color','b3_t','b3_p','b3_lt','b3_lp','b3_lm'];"
      "names.forEach(function(n){"
        "var el=document.querySelector('[name='+n+']');"
        "if(el)params.push(n+'='+encodeURIComponent(el.value));"
      "});"
      "var x=new XMLHttpRequest();"
      "x.open('POST','/save',true);"
      "x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');"
      "x.onload=function(){document.body.innerHTML="
        "'<div style=\"color:#10b981;font-size:18px;text-align:center;margin-top:40px\">"
        "&#10003; Sauvegard\u00e9 &mdash; red\u00e9marrage...</div>';};"
      "x.send(params.join('&'));"
    "}"
    "</script>");

  html += F("</body></html>");
  server.send(200, "text/html", html);
}

void handle_trigger() {
  int id = server.arg("id").toInt();
  bool sent = false;
  if (mqtt_client.connected()) {
    if (id == 1) sent = mqtt_client.publish(btn1_topic, btn1_payload);
    if (id == 2) sent = mqtt_client.publish(btn2_topic, btn2_payload);
    if (id == 3) sent = mqtt_client.publish(btn3_topic, btn3_payload);
  }
  server.send(sent ? 200 : 500, "text/plain", sent ? "ok" : "erreur mqtt");
}

void handle_save() {
  strlcpy(wifi_ssid,           server.arg("wifi_ssid").c_str(),  sizeof(wifi_ssid));
  strlcpy(wifi_pass,           server.arg("wifi_pass").c_str(),  sizeof(wifi_pass));
  strlcpy(mqtt_server,         server.arg("mqtt_server").c_str(),sizeof(mqtt_server));
  strlcpy(mqtt_port,           server.arg("mqtt_port").c_str(),  sizeof(mqtt_port));
  strlcpy(mqtt_user,           server.arg("mqtt_user").c_str(),  sizeof(mqtt_user));
  strlcpy(mqtt_pass,           server.arg("mqtt_pass").c_str(),  sizeof(mqtt_pass));
  strlcpy(btn1_topic,          server.arg("b1_t").c_str(),       sizeof(btn1_topic));
  strlcpy(btn1_payload,        server.arg("b1_p").c_str(),       sizeof(btn1_payload));
  strlcpy(btn1_name,           server.arg("b1_name").c_str(),    sizeof(btn1_name));
  strlcpy(btn1_color,          server.arg("b1_color").c_str(),   sizeof(btn1_color));
  strlcpy(btn1_long_topic,     server.arg("b1_lt").c_str(),      sizeof(btn1_long_topic));
  strlcpy(btn1_long_payload,   server.arg("b1_lp").c_str(),      sizeof(btn1_long_payload));
  strlcpy(btn1_long_mode,      server.arg("b1_lm").c_str(),      sizeof(btn1_long_mode));
  strlcpy(btn2_topic,          server.arg("b2_t").c_str(),       sizeof(btn2_topic));
  strlcpy(btn2_payload,        server.arg("b2_p").c_str(),       sizeof(btn2_payload));
  strlcpy(btn2_name,           server.arg("b2_name").c_str(),    sizeof(btn2_name));
  strlcpy(btn2_color,          server.arg("b2_color").c_str(),   sizeof(btn2_color));
  strlcpy(btn2_long_topic,     server.arg("b2_lt").c_str(),      sizeof(btn2_long_topic));
  strlcpy(btn2_long_payload,   server.arg("b2_lp").c_str(),      sizeof(btn2_long_payload));
  strlcpy(btn2_long_mode,      server.arg("b2_lm").c_str(),      sizeof(btn2_long_mode));
  strlcpy(btn3_topic,          server.arg("b3_t").c_str(),       sizeof(btn3_topic));
  strlcpy(btn3_payload,        server.arg("b3_p").c_str(),       sizeof(btn3_payload));
  strlcpy(btn3_name,           server.arg("b3_name").c_str(),    sizeof(btn3_name));
  strlcpy(btn3_color,          server.arg("b3_color").c_str(),   sizeof(btn3_color));
  strlcpy(btn3_long_topic,     server.arg("b3_lt").c_str(),      sizeof(btn3_long_topic));
  strlcpy(btn3_long_payload,   server.arg("b3_lp").c_str(),      sizeof(btn3_long_payload));
  strlcpy(btn3_long_mode,      server.arg("b3_lm").c_str(),      sizeof(btn3_long_mode));
  long_press_ms = server.arg("long_ms").toInt();
  if (long_press_ms < 300)  long_press_ms = 300;
  if (long_press_ms > 3000) long_press_ms = 3000;
  save_config();
  delay(500);
  ESP.restart();
}

void handle_ap_root() {
  String html = F("<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{margin:0;background:#111827;color:#f3f4f6;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
      "display:flex;align-items:center;justify-content:center;min-height:100vh;}"
    ".card{background:#1f2937;border:1px solid #374151;border-radius:16px;padding:32px 24px;text-align:center;max-width:300px;width:90%;}"
    "h2{margin:0 0 8px;font-size:18px;font-weight:500;}"
    "p{color:#9ca3af;font-size:14px;margin:0 0 24px;}"
    "a{display:inline-block;background:#3b82f6;color:#fff;padding:12px 28px;border-radius:10px;text-decoration:none;font-size:15px;}"
    "</style></head><body>"
    "<div class='card'>"
    "<h2>mode point d&apos;acc&egrave;s</h2>"
    "<p>Connectez-vous au r&eacute;seau <b>button_mqtt</b><br>puis configurez l&apos;appareil.</p>"
    "<a href='/setup'>&#9881; Configurer</a>"
    "</div></body></html>");
  server.send(200, "text/html", html);
}

// ================================================================== routage

void setup_routing() {
  if (is_ap_mode) server.on("/", handle_ap_root);
  else            server.on("/", handle_root);
  server.on("/setup",   handle_setup);
  server.on("/trigger", handle_trigger);
  server.on("/save",    HTTP_POST, handle_save);
  httpUpdater.setup(&server, "/update");
  server.begin();
}

void setup_ap() {
  is_ap_mode = true;
  WiFi.persistent(false);
  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP("button_mqtt");
  Serial.println("[wifi] mode AP -> http://192.168.4.1");
  setup_routing();
}

// ================================================================== setup()

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- boot esp8266 ---");

  pinMode(btn1_pin, INPUT_PULLUP);
  pinMode(btn2_pin, INPUT_PULLUP);
  pinMode(btn3_pin, INPUT_PULLUP);
  pinMode(PIN_LED,  OUTPUT);
  digitalWrite(PIN_LED, HIGH);  // éteinte par défaut (active LOW)

  // Lecture des états électriques initiaux après configuration des pull-ups
  // Cela évite de détecter un faux changement d'état au boot.
  last_raw_btn1_state = digitalRead(btn1_pin);
  last_raw_btn2_state = digitalRead(btn2_pin);
  last_raw_btn3_state = digitalRead(btn3_pin);
  
  debounced_btn1_state = last_raw_btn1_state;
  debounced_btn2_state = last_raw_btn2_state;
  debounced_btn3_state = last_raw_btn3_state;

  load_config();
  if (strlen(wifi_ssid) > 0) {
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_pass);
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 40) {
      delay(500); Serial.print("."); timeout++;
    }
    Serial.println("");
    if (WiFi.status() == WL_CONNECTED) {
      is_ap_mode = false;
      Serial.print("[wifi] ip : "); Serial.println(WiFi.localIP());
      mqtt_client.setServer(mqtt_server, atoi(mqtt_port));
      net_state = NET_RECONNECTING;
      setup_routing();
    } else {
      net_state = NET_WIFI_KO;
      setup_ap();
    }
  } else {
    net_state = NET_WIFI_KO;
    setup_ap();
  }
}

// ================================================================== gestion réseau

void manage_network() {
  unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (net_state != NET_WIFI_KO && net_state != NET_RECONNECTING) {
      Serial.println("[wifi] connexion perdue");
      reconnect_count++;
      net_state = NET_WIFI_KO;
      last_wifi_attempt = 0;
      wifi_backoff_ms   = 5000;
    }
    if (now - last_wifi_attempt >= wifi_backoff_ms) {
      last_wifi_attempt = now;
      net_state = NET_RECONNECTING;
      Serial.print("[wifi] tentative reconnexion (backoff=");
      Serial.print(wifi_backoff_ms / 1000); Serial.println("s)");
      WiFi.disconnect();
      WiFi.begin(wifi_ssid, wifi_pass);
      wifi_backoff_ms = min((unsigned long)30000, wifi_backoff_ms * 2);
    }
    return;
  }

  wifi_backoff_ms = 5000;
  if (!mqtt_client.connected()) {
    if (net_state == NET_OK) {
      Serial.println("[mqtt] connexion perdue");
      reconnect_count++;
    }
    net_state = NET_MQTT_KO;

    if (now - last_mqtt_attempt >= 5000) {
      last_mqtt_attempt = now;
      Serial.println("[mqtt] tentative reconnexion...");
      String clientId = "esp-btn-" + String(ESP.getChipId(), HEX);
      bool ok = (strlen(mqtt_user) > 0)
        ? mqtt_client.connect(clientId.c_str(), mqtt_user, mqtt_pass)
        : mqtt_client.connect(clientId.c_str());
      if (ok) {
        Serial.println("[mqtt] connecté");
        net_state = NET_OK;
      } else {
        Serial.print("[mqtt] erreur code="); Serial.println(mqtt_client.state());
      }
    }
    return;
  }

  net_state = NET_OK;
  mqtt_client.loop();
}

// ================================================================== boutons physiques

void publish_if_connected(const char* topic, const char* payload, const char* label) {
  if (strlen(topic) == 0) return;
  if (!mqtt_client.connected()) { Serial.println("[mqtt] non connecté, commande ignorée"); return; }
  if (mqtt_client.publish(topic, payload)) Serial.println("[mqtt] " + String(label) + " envoyé");
  else Serial.println("[mqtt] echec envoi " + String(label));
}

void check_buttons() {
  int r1 = digitalRead(btn1_pin);
  int r2 = digitalRead(btn2_pin);
  int r3 = digitalRead(btn3_pin);
  unsigned long now = millis();

  // ---- Filtrage & Debounce Bouton 1 ----
  if (r1 != last_raw_btn1_state) {
    last_debounce_time1 = now;
    last_raw_btn1_state = r1;
  }
  if ((now - last_debounce_time1) > debounce_delay) {
    if (r1 != debounced_btn1_state) {
      debounced_btn1_state = r1;
      
      if (debounced_btn1_state == LOW) {
        pressed1 = true;
        long_fired1 = false;
        press_start1 = now;
        if (strcmp(btn1_long_mode, "long") != 0) {
          Serial.println("[action] D1 court");
          publish_if_connected(btn1_topic, btn1_payload, "btn1-court");
        }
      } else {
        if (pressed1 && !long_fired1 && strcmp(btn1_long_mode, "long") == 0) {
          Serial.println("[action] D1 court (mode long-seul, relâchement rapide)");
          publish_if_connected(btn1_topic, btn1_payload, "btn1-court");
        }
        pressed1 = false;
      }
    }
  }
  if (pressed1 && !long_fired1 && (now - press_start1) >= long_press_ms) {
    long_fired1 = true;
    Serial.println("[action] D1 long");
    publish_if_connected(btn1_long_topic, btn1_long_payload, "btn1-long");
  }

  // ---- Filtrage & Debounce Bouton 2 ----
  if (r2 != last_raw_btn2_state) {
    last_debounce_time2 = now;
    last_raw_btn2_state = r2;
  }
  if ((now - last_debounce_time2) > debounce_delay) {
    if (r2 != debounced_btn2_state) {
      debounced_btn2_state = r2;
      
      if (debounced_btn2_state == LOW) {
        pressed2 = true;
        long_fired2 = false;
        press_start2 = now;
        if (strcmp(btn2_long_mode, "long") != 0) {
          Serial.println("[action] D2 court");
          publish_if_connected(btn2_topic, btn2_payload, "btn2-court");
        }
      } else {
        if (pressed2 && !long_fired2 && strcmp(btn2_long_mode, "long") == 0) {
          Serial.println("[action] D2 court (mode long-seul, relâchement rapide)");
          publish_if_connected(btn2_topic, btn2_payload, "btn2-court");
        }
        pressed2 = false;
      }
    }
  }
  if (pressed2 && !long_fired2 && (now - press_start2) >= long_press_ms) {
    long_fired2 = true;
    Serial.println("[action] D2 long");
    publish_if_connected(btn2_long_topic, btn2_long_payload, "btn2-long");
  }

  // ---- Filtrage & Debounce Bouton 3 ----
  if (r3 != last_raw_btn3_state) {
    last_debounce_time3 = now;
    last_raw_btn3_state = r3;
  }
  if ((now - last_debounce_time3) > debounce_delay) {
    if (r3 != debounced_btn3_state) {
      debounced_btn3_state = r3;
      
      if (debounced_btn3_state == LOW) {
        pressed3 = true;
        long_fired3 = false;
        press_start3 = now;
        if (strcmp(btn3_long_mode, "long") != 0) {
          Serial.println("[action] D5 court");
          publish_if_connected(btn3_topic, btn3_payload, "btn3-court");
        }
      } else {
        if (pressed3 && !long_fired3 && strcmp(btn3_long_mode, "long") == 0) {
          Serial.println("[action] D5 court (mode long-seul, relâchement rapide)");
          publish_if_connected(btn3_topic, btn3_payload, "btn3-court");
        }
        pressed3 = false;
      }
    }
  }
  if (pressed3 && !long_fired3 && (now - press_start3) >= long_press_ms) {
    long_fired3 = true;
    Serial.println("[action] D5 long");
    publish_if_connected(btn3_long_topic, btn3_long_payload, "btn3-long");
  }
}

// ================================================================== loop()

void loop() {
  server.handleClient();
  yield();

  if (!is_ap_mode) {
    manage_network();
    update_led();
    check_buttons();
  }
}
