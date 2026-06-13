#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <PubSubClient.h>

// ------------------------------------------------------------------ broches
#define pin_d1  5
#define pin_d2  4
#define pin_d5  14
#define pin_led 2    // d4 = gpio2, led onboard nodemcu (active low)

const int btn1_pin = pin_d1;
const int btn2_pin = pin_d2;
const int btn3_pin = pin_d5;

// ------------------------------------------------------------------ config
char wifi_ssid[32]  = "";
char wifi_pass[64]  = "";
bool mqtt_enabled   = false;
char mqtt_server[40]= "";
char mqtt_port[6]   = "1883";
char mqtt_user[40]  = "";
char mqtt_pass[40]  = "";

// bouton 1
char btn1_mode[10]         = "mqtt";   // "mqtt", "http", "both"
char btn1_topic[60]        = "esp/btn1/topic";
char btn1_payload[100]     = "message_btn1";
char btn1_url[150]         = "";
char btn1_name[20]         = "up";
char btn1_color[10]        = "green";
char btn1_long_mode[6]     = "both";   // "both" = court+long, "long" = long seul
char btn1_long_act[10]     = "mqtt";   // "mqtt", "http", "both"
char btn1_long_topic[60]   = "";
char btn1_long_payload[100]= "";
char btn1_long_url[150]    = "";
char btn1_click_mode[10]   = "mqtt";   // double clic
char btn1_click_topic[60]  = "";
char btn1_click_payload[100]= "";
char btn1_click_url[150]   = "";

// bouton 2
char btn2_mode[10]         = "mqtt";
char btn2_topic[60]        = "esp/btn2/topic";
char btn2_payload[100]     = "message_btn2";
char btn2_url[150]         = "";
char btn2_name[20]         = "my";
char btn2_color[10]        = "white";
char btn2_long_mode[6]     = "both";
char btn2_long_act[10]     = "mqtt";
char btn2_long_topic[60]   = "";
char btn2_long_payload[100]= "";
char btn2_long_url[150]    = "";
char btn2_click_mode[10]   = "mqtt";
char btn2_click_topic[60]  = "";
char btn2_click_payload[100]= "";
char btn2_click_url[150]   = "";

// bouton 3
char btn3_mode[10]         = "mqtt";
char btn3_topic[60]        = "esp/btn3/topic";
char btn3_payload[100]     = "message_btn3";
char btn3_url[150]         = "";
char btn3_name[20]         = "down";
char btn3_color[10]        = "red";
char btn3_long_mode[6]     = "both";
char btn3_long_act[10]     = "mqtt";
char btn3_long_topic[60]   = "";
char btn3_long_payload[100]= "";
char btn3_long_url[150]    = "";
char btn3_click_mode[10]   = "mqtt";
char btn3_click_topic[60]  = "";
char btn3_click_payload[100]= "";
char btn3_click_url[150]   = "";

// seuils temporels en ms
unsigned int long_press_ms  = 800;
unsigned int double_click_ms = 400;

// ------------------------------------------------------------------ etat reseau
enum netstate { net_ok, net_mqtt_ko, net_wifi_ko, net_reconnecting };
netstate net_state = net_reconnecting;

unsigned long last_wifi_attempt  = 0;
unsigned long last_mqtt_attempt  = 0;
unsigned long wifi_backoff_ms    = 5000;
unsigned int  reconnect_count    = 0;

// clignotement led non bloquant
unsigned long last_led_toggle    = 0;
bool          led_state          = false;

// ------------------------------------------------------------------ debounce & etats filtres
bool last_raw_btn1_state = LOW;   // TTP223 repos=LOW
bool last_raw_btn2_state = LOW;   // TTP223 repos=LOW
bool last_raw_btn3_state = LOW;   // TTP223 repos=LOW
bool debounced_btn1_state = LOW;  // TTP223 repos=LOW
bool debounced_btn2_state = LOW;  // TTP223 repos=LOW
bool debounced_btn3_state = LOW;  // TTP223 repos=LOW

unsigned long last_debounce_time1 = 0;
unsigned long last_debounce_time2 = 0;
unsigned long last_debounce_time3 = 0;
const unsigned long debounce_delay = 50;

// structures pour machine d'etat des clics physiques
unsigned long press_start1 = 0; unsigned long release_time1 = 0; int click_count1 = 0; bool pressed1 = false; bool long_fired1 = false;
unsigned long press_start2 = 0; unsigned long release_time2 = 0; int click_count2 = 0; bool pressed2 = false; bool long_fired2 = false;
unsigned long press_start3 = 0; unsigned long release_time3 = 0; int click_count3 = 0; bool pressed3 = false; bool long_fired3 = false;

// ------------------------------------------------------------------ objets
ESP8266WebServer         server(80);
ESP8266HTTPUpdateServer  httpupdater;
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

// ================================================================== envoyer l'action (http / mqtt)

bool execute_action(const char* mode, const char* topic, const char* payload, const char* url, const char* label) {
  bool success = true;
  String smode = String(mode);

  if (mqtt_enabled && (smode == "mqtt" || smode == "both")) {
    if (strlen(topic) > 0) {
      if (mqtt_client.connected()) {
        if (mqtt_client.publish(topic, payload)) {
          Serial.println("[mqtt] " + String(label) + " envoye");
        } else {
          Serial.println("[mqtt] echec envoi " + String(label));
          success = false;
        }
      } else {
        Serial.println("[mqtt] non connecte, commande marquee echec");
        success = false;
      }
    }
  }

  if (smode == "http" || smode == "both") {
    if (strlen(url) > 0) {
      if (WiFi.status() == WL_CONNECTED) {
        WiFiClient client;
        HTTPClient http;
        Serial.println("[http] post vers " + String(url));
        if (http.begin(client, url)) {
          int httpcode = http.POST("");
          if (httpcode > 0) {
            Serial.printf("[http] reponse : %d\n", httpcode);
            if (httpcode < 200 || httpcode >= 300) success = false;
          } else {
            Serial.printf("[http] echec post err : %s\n", http.errorToString(httpcode).c_str());
            success = false;
          }
          http.end();
        } else {
          Serial.println("[http] impossible de se connecter au serveur");
          success = false;
        }
      } else {
        Serial.println("[http] wifi non connecte, abandon");
        success = false;
      }
    }
  }

  return success;
}

// ================================================================== config json

void load_config() {
  if (!LittleFS.begin()) return;
  if (!LittleFS.exists("/config.json")) return;
  File f = LittleFS.open("/config.json", "r");
  if (!f) return;
  StaticJsonDocument<4096> json;
  if (deserializeJson(json, f) == DeserializationError::Ok) {
    strlcpy(wifi_ssid,           json["wifi_ssid"]           | "",              sizeof(wifi_ssid));
    strlcpy(wifi_pass,           json["wifi_pass"]           | "",              sizeof(wifi_pass));
    mqtt_enabled                 = json["mqtt_enabled"]      | false;
    strlcpy(mqtt_server,         json["mqtt_server"]         | "",              sizeof(mqtt_server));
    strlcpy(mqtt_port,           json["mqtt_port"]           | "1883",          sizeof(mqtt_port));
    strlcpy(mqtt_user,           json["mqtt_user"]           | "",              sizeof(mqtt_user));
    strlcpy(mqtt_pass,           json["mqtt_pass"]           | "",              sizeof(mqtt_pass));
    
    // bouton 1
    strlcpy(btn1_mode,           json["btn1_mode"]           | "mqtt",          sizeof(btn1_mode));
    strlcpy(btn1_topic,          json["btn1_topic"]          | "esp/btn1/topic",sizeof(btn1_topic));
    strlcpy(btn1_payload,        json["btn1_payload"]        | "message_btn1",  sizeof(btn1_payload));
    strlcpy(btn1_url,            json["btn1_url"]            | "",              sizeof(btn1_url));
    strlcpy(btn1_name,           json["btn1_name"]           | "up",            sizeof(btn1_name));
    strlcpy(btn1_color,          json["btn1_color"]          | "green",         sizeof(btn1_color));
    strlcpy(btn1_long_topic,     json["btn1_long_topic"]     | "",              sizeof(btn1_long_topic));
    strlcpy(btn1_long_payload,   json["btn1_long_payload"]   | "",              sizeof(btn1_long_payload));
    strlcpy(btn1_long_url,       json["btn1_long_url"]       | "",              sizeof(btn1_long_url));
    strlcpy(btn1_long_mode,      json["btn1_long_mode"]      | "both",          sizeof(btn1_long_mode));
    strlcpy(btn1_long_act,       json["btn1_long_act"]       | "mqtt",          sizeof(btn1_long_act));
    strlcpy(btn1_click_mode,     json["btn1_click_mode"]     | "mqtt",          sizeof(btn1_click_mode));
    strlcpy(btn1_click_topic,    json["btn1_click_topic"]    | "",              sizeof(btn1_click_topic));
    strlcpy(btn1_click_payload,  json["btn1_click_payload"]  | "",              sizeof(btn1_click_payload));
    strlcpy(btn1_click_url,      json["btn1_click_url"]      | "",              sizeof(btn1_click_url));

    // bouton 2
    strlcpy(btn2_mode,           json["btn2_mode"]           | "mqtt",          sizeof(btn2_mode));
    strlcpy(btn2_topic,          json["btn2_topic"]          | "esp/btn2/topic",sizeof(btn2_topic));
    strlcpy(btn2_payload,        json["btn2_payload"]        | "message_btn2",  sizeof(btn2_payload));
    strlcpy(btn2_url,            json["btn2_url"]            | "",              sizeof(btn2_url));
    strlcpy(btn2_name,           json["btn2_name"]           | "my",            sizeof(btn2_name));
    strlcpy(btn2_color,          json["btn2_color"]          | "white",         sizeof(btn2_color));
    strlcpy(btn2_long_topic,     json["btn2_long_topic"]     | "",              sizeof(btn2_long_topic));
    strlcpy(btn2_long_payload,   json["btn2_long_payload"]   | "",              sizeof(btn2_long_payload));
    strlcpy(btn2_long_url,       json["btn2_long_url"]       | "",              sizeof(btn2_long_url));
    strlcpy(btn2_long_mode,      json["btn2_long_mode"]      | "both",          sizeof(btn2_long_mode));
    strlcpy(btn2_long_act,       json["btn2_long_act"]       | "mqtt",          sizeof(btn2_long_act));
    strlcpy(btn2_click_mode,     json["btn2_click_mode"]     | "mqtt",          sizeof(btn2_click_mode));
    strlcpy(btn2_click_topic,    json["btn2_click_topic"]    | "",              sizeof(btn2_click_topic));
    strlcpy(btn2_click_payload,  json["btn2_click_payload"]  | "",              sizeof(btn2_click_payload));
    strlcpy(btn2_click_url,      json["btn2_click_url"]      | "",              sizeof(btn2_click_url));

    // bouton 3
    strlcpy(btn3_mode,           json["btn3_mode"]           | "mqtt",          sizeof(btn3_mode));
    strlcpy(btn3_topic,          json["btn3_topic"]          | "esp/btn3/topic",sizeof(btn3_topic));
    strlcpy(btn3_payload,        json["btn3_payload"]        | "message_btn3",  sizeof(btn3_payload));
    strlcpy(btn3_url,            json["btn3_url"]            | "",              sizeof(btn3_url));
    strlcpy(btn3_name,           json["btn3_name"]           | "down",          sizeof(btn3_name));
    strlcpy(btn3_color,          json["btn3_color"]          | "red",           sizeof(btn3_color));
    strlcpy(btn3_long_topic,     json["btn3_long_topic"]     | "",              sizeof(btn3_long_topic));
    strlcpy(btn3_long_payload,   json["btn3_long_payload"]   | "",              sizeof(btn3_long_payload));
    strlcpy(btn3_long_url,       json["btn3_long_url"]       | "",              sizeof(btn3_long_url));
    strlcpy(btn3_long_mode,      json["btn3_long_mode"]      | "both",          sizeof(btn3_long_mode));
    strlcpy(btn3_long_act,       json["btn3_long_act"]       | "mqtt",          sizeof(btn3_long_act));
    strlcpy(btn3_click_mode,     json["btn3_click_mode"]     | "mqtt",          sizeof(btn3_click_mode));
    strlcpy(btn3_click_topic,    json["btn3_click_topic"]    | "",              sizeof(btn3_click_topic));
    strlcpy(btn3_click_payload,  json["btn3_click_payload"]  | "",              sizeof(btn3_click_payload));
    strlcpy(btn3_click_url,      json["btn3_click_url"]      | "",              sizeof(btn3_click_url));
    
    long_press_ms   = json["long_press_ms"]   | 800;
    double_click_ms = json["double_click_ms"] | 400;
  }
  f.close();
}

void save_config() {
  StaticJsonDocument<4096> json;
  json["wifi_ssid"]         = wifi_ssid;
  json["wifi_pass"]         = wifi_pass;
  json["mqtt_enabled"]      = mqtt_enabled;
  json["mqtt_server"]       = mqtt_server;
  json["mqtt_port"]         = mqtt_port;
  json["mqtt_user"]         = mqtt_user;
  json["mqtt_pass"]         = mqtt_pass;
  
  // bouton 1
  json["btn1_mode"]         = btn1_mode;
  json["btn1_topic"]        = btn1_topic;
  json["btn1_payload"]      = btn1_payload;
  json["btn1_url"]          = btn1_url;
  json["btn1_name"]         = btn1_name;
  json["btn1_color"]        = btn1_color;
  json["btn1_long_topic"]   = btn1_long_topic;
  json["btn1_long_payload"] = btn1_long_payload;
  json["btn1_long_url"]     = btn1_long_url;
  json["btn1_long_mode"]    = btn1_long_mode;
  json["btn1_long_act"]     = btn1_long_act;
  json["btn1_click_mode"]   = btn1_click_mode;
  json["btn1_click_topic"]  = btn1_click_topic;
  json["btn1_click_payload"]= btn1_click_payload;
  json["btn1_click_url"]    = btn1_click_url;

  // bouton 2
  json["btn2_mode"]         = btn2_mode;
  json["btn2_topic"]        = btn2_topic;
  json["btn2_payload"]      = btn2_payload;
  json["btn2_url"]          = btn2_url;
  json["btn2_name"]         = btn2_name;
  json["btn2_color"]        = btn2_color;
  json["btn2_long_topic"]   = btn2_long_topic;
  json["btn2_long_payload"] = btn2_long_payload;
  json["btn2_long_url"]     = btn2_long_url;
  json["btn2_long_mode"]    = btn2_long_mode;
  json["btn2_long_act"]     = btn2_long_act;
  json["btn2_click_mode"]   = btn2_click_mode;
  json["btn2_click_topic"]  = btn2_click_topic;
  json["btn2_click_payload"]= btn2_click_payload;
  json["btn2_click_url"]    = btn2_click_url;

  // bouton 3
  json["btn3_mode"]         = btn3_mode;
  json["btn3_topic"]        = btn3_topic;
  json["btn3_payload"]      = btn3_payload;
  json["btn3_url"]          = btn3_url;
  json["btn3_name"]         = btn3_name;
  json["btn3_color"]        = btn3_color;
  json["btn3_long_topic"]   = btn3_long_topic;
  json["btn3_long_payload"] = btn3_long_payload;
  json["btn3_long_url"]     = btn3_long_url;
  json["btn3_long_mode"]    = btn3_long_mode;
  json["btn3_long_act"]     = btn3_long_act;
  json["btn3_click_mode"]   = btn3_click_mode;
  json["btn3_click_topic"]  = btn3_click_topic;
  json["btn3_click_payload"]= btn3_click_payload;
  json["btn3_click_url"]    = btn3_click_url;

  json["long_press_ms"]     = long_press_ms;
  json["double_click_ms"]   = double_click_ms;
  
  File f = LittleFS.open("/config.json", "w");
  if (f) { serializeJson(json, f); f.close(); }
}

// ================================================================== led statut

void update_led() {
  unsigned long now = millis();
  unsigned long interval = 0;

  switch (net_state) {
    case net_ok:           interval = 0;   break;
    case net_mqtt_ko:      interval = mqtt_enabled ? 800 : 0; break;
    case net_wifi_ko:      interval = 150; break;
    case net_reconnecting: interval = 300; break;
  }

  if (interval == 0) {
    digitalWrite(pin_led, HIGH);
    led_state = false;
    return;
  }

  if (now - last_led_toggle >= interval) {
    last_led_toggle = now;
    led_state = !led_state;
    digitalWrite(pin_led, led_state ? LOW : HIGH);
  }
}

// ================================================================== pages web

void handle_root() {
  bool wifi_ok = (WiFi.status() == WL_CONNECTED);
  bool mqtt_ok = mqtt_enabled ? mqtt_client.connected() : false;

  String html = String("<!doctype html><html><head>") +
    "<meta charset='utf-8'>" +
    "<meta name='viewport' content='width=device-width,initial-scale=1'>" +
    "<title>telecommande</title>" +
    "<style>" +
    "body{margin:0;padding:0;background:#111827;font-family:-apple-system,blinkmacsystemfont,'segoe ui',sans-serif;display:flex;flex-direction:column;align-items:center;min-height:100vh;}" +
    ".card{width:92%;max-width:340px;margin:0 auto;}" +
    ".hdr{padding:14px 0 10px;width:92%;max-width:340px;display:flex;flex-direction:column;align-items:center;gap:6px;}" +
    ".status-row{display:flex;gap:8px;}" +
    ".pill{display:flex;align-items:center;gap:5px;font-size:12px;padding:4px 10px;border-radius:99px;font-weight:500;}" +
    ".pill-ok{background:#0d2b1a;color:#52c98a;border:1px solid #1e5c33;}" +
    ".pill-err{background:#2b0d0d;color:#e05555;border:1px solid #5c1e1e;}" +
    ".pill-dis{background:#1f2937;color:#9ca3af;border:1px solid #374151;}" +
    ".dot{width:7px;height:7px;border-radius:50%;}" +
    ".dot-ok{background:#52c98a;}" +
    ".dot-err{background:#e05555;}" +
    ".dot-dis{background:#9ca3af;}" +
    ".ip{font-size:11px;color:#4b5563;margin-top:2px;}" +
    ".recon{font-size:10px;color:#374151;margin-top:1px;}" +
    ".tgl-container{display:flex;align-items:center;justify-content:space-between;background:#1f2937;padding:12px 16px;border-radius:12px;margin:8px 0;width:100%;box-sizing:border-box;border:1px solid #374151;}" +
    ".tgl-lbl{font-size:13px;color:#f3f4f6;font-weight:500;text-transform:uppercase;letter-spacing:0.03em;}" +
    ".sw{position:relative;display:inline-block;width:44px;height:24px;}" +
    ".sw input{opacity:0;width:0;height:0;}" +
    ".sl{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#4b5563;transition:.2s;border-radius:24px;}" +
    ".sl:before{position:absolute;content:'';height:18px;width:18px;left:3px;bottom:3px;background-color:white;transition:.2s;border-radius:50%;}" +
    "input:checked + .sl{background-color:#ec4899;}" +
    "input:checked + .sl:before{transform:translateX(20px);}" +
    ".btns{display:flex;flex-direction:column;gap:12px;padding:8px 0 16px;width:100%;}" +
    ".btn{width:100%;padding:22px 0;border-radius:14px;border:none;cursor:pointer;font-size:24px;font-weight:500;letter-spacing:0.02em;display:flex;flex-direction:column;align-items:center;gap:4px;box-sizing:border-box;}" +
    ".btn-icon{font-size:13px;opacity:0.5;}" +
    ".btn:active{opacity:0.75;transform:scale(0.98);}" +
    ".footer{padding:12px 0 24px;border-top:1px solid #1f2937;width:100%;text-align:center;}" +
    ".footer a{color:#374151;font-size:13px;text-decoration:none;}" +
    "</style></head><body>";

  html += "<div class='hdr'><div class='status-row'>";
  html += "<div class='pill " + String(wifi_ok ? "pill-ok" : "pill-err") + "'>";
  html += "<div class='dot " + String(wifi_ok ? "dot-ok" : "dot-err") + "'></div>wifi</div>";
  
  if (mqtt_enabled) {
    html += "<div class='pill " + String(mqtt_ok ? "pill-ok" : "pill-err") + "'>";
    html += "<div class='dot " + String(mqtt_ok ? "dot-ok" : "dot-err") + "'></div>mqtt</div>";
  } else {
    html += "<div class='pill pill-dis'>";
    html += "<div class='dot dot-dis'></div>mqtt off</div>";
  }
  
  html += "</div>";
  if (wifi_ok) html += "<div class='ip'>" + WiFi.localIP().toString() + "</div>";
  if (reconnect_count > 0)
    html += "<div class='recon'>reconnexions : " + String(reconnect_count) + "</div>";
  html += "</div>";

  html += "<div class='card'>";
  
  // toggle appui long persistant sur la page
  html += "<div class='tgl-container'><span class='tgl-lbl'>&#128336; action appui long</span>";
  html += "<label class='sw'><input type='checkbox' id='tgl_long'><span class='sl'></span></label></div>";

  html += "<div class='btns'>";
  html += "<button class='btn' style='" + color_styles(btn1_color) + "' onclick='pub(1)'>";
  html += "<span class='btn-icon'>" + btn_icon(1) + "</span><span>" + String(btn1_name) + "</span></button>";
  html += "<button class='btn' style='" + color_styles(btn2_color) + "' onclick='pub(2)'>";
  html += "<span class='btn-icon'>" + btn_icon(2) + "</span><span>" + String(btn2_name) + "</span></button>";
  html += "<button class='btn' style='" + color_styles(btn3_color) + "' onclick='pub(3)'>";
  html += "<span class='btn-icon'>" + btn_icon(3) + "</span><span>" + String(btn3_name) + "</span></button>";
  html += "</div>";
  html += "<div class='footer'><a href='/setup'>&#9881; configuration</a></div>";
  html += "</div>";
  html += "<script>function pub(id){"
          "var isLong=document.getElementById('tgl_long').checked;"
          "var type=isLong?'long':'court';"
          "var x=new XMLHttpRequest();x.open('GET','/trigger?id='+id+'&type='+type,true);x.send();"
          "}</script>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

String color_option(const char* val, const char* lbl, const char* cur) {
  return "<option value='" + String(val) + "'" + (strcmp(val, cur) == 0 ? " selected" : "") + ">" + String(lbl) + "</option>";
}

String act_option(const char* val, const char* lbl, const char* cur) {
  return "<option value='" + String(val) + "'" + (strcmp(val, cur) == 0 ? " selected" : "") + ">" + String(lbl) + "</option>";
}

String btn_card(int n, const char* pin_lbl, const char* name, const char* color, 
                 const char* mode, const char* topic, const char* payload, const char* url, 
                 const char* ltopic, const char* lpayload, const char* lurl, const char* lmode, const char* lact,
                 const char* dmode, const char* dtopic, const char* dpayload, const char* durl) {
  String p   = String(n);
  String dot = "dot" + p;
  String s;
  s += "<div class='btn-card'>";
  s += "<div class='btn-card-hdr'><div class='clr-dot' id='" + dot + "' style='background:" + color_hex(color) + "'></div>";
  s += "bouton " + p + " &mdash; " + String(pin_lbl) + "</div>";

  s += "<div class='grid2'>";
  s += "<div class='row'><label>nom affich&eacute;</label><input type='text' name='b" + p + "_name' value='" + String(name) + "' maxlength='12'></div>";
  s += "<div class='row'><label>couleur</label><select name='b" + p + "_color' onchange='upd(this,\"" + dot + "\")'>";
  s += color_option("green",  "vert",   color);
  s += color_option("white",  "blanc",  color);
  s += color_option("red",    "rouge",  color);
  s += color_option("blue",   "bleu",   color);
  s += color_option("orange", "orange", color);
  s += "</select></div></div>";

  // action courte
  s += "<div class='row'><label>type d'action (court)</label><select name='b" + p + "_mode' onchange='tglFields(this," + p + ",\"c\")'>";
  s += act_option("mqtt", "mqtt seul", mode);
  s += act_option("http", "http post seul", mode);
  s += act_option("both", "mqtt + http post", mode);
  s += "</select></div>";
  
  s += "<div id='b" + p + "_c_mqtt_fields' style='display:" + String(strcmp(mode,"http")==0?"none":"block") + ";'>";
  s += "<div class='row'><label>topic court</label><input type='text' name='b" + p + "_t' value='" + String(topic) + "'></div>";
  s += "<div class='row'><label>payload court</label><input type='text' name='b" + p + "_p' value='" + String(payload) + "'></div>";
  s += "</div>";
  
  s += "<div id='b" + p + "_c_http_fields' style='display:" + String(strcmp(mode,"mqtt")==0?"none":"block") + ";'>";
  s += "<div class='row'><label>url post courte</label><input type='text' name='b" + p + "_url' value='" + String(url) + "' placeholder='http://...'></div>";
  s += "</div>";
  
  s += "<button type='button' style='margin: 4px 0 12px 0; padding: 6px; font-size:12px; background:#3b82f6; color:#fff; border:none; border-radius:4px; cursor:pointer;' onclick='testaction(" + p + ", \"court\")'>&brvbar;&rArr; tester action courte</button>";

  // double clic
  s += "<div class='long-hdr' style='color:#3b82f6; border-top-color:#c7d7fd;'>&#9898;&#9898; double clic</div>";
  s += "<div class='row'><label>type d'action (double clic)</label><select name='b" + p + "_dmode' onchange='tglFields(this," + p + ",\"d\")'>";
  s += act_option("mqtt", "mqtt seul", dmode);
  s += act_option("http", "http post seul", dmode);
  s += act_option("both", "mqtt + http post", dmode);
  s += "</select></div>";

  s += "<div id='b" + p + "_d_mqtt_fields' style='display:" + String(strcmp(dmode,"http")==0?"none":"block") + ";'>";
  s += "<div class='row'><label>topic double clic</label><input type='text' name='b" + p + "_dt' value='" + String(dtopic) + "' placeholder='laisser vide pour d&eacute;sactiver'></div>";
  s += "<div class='row'><label>payload double clic</label><input type='text' name='b" + p + "_dp' value='" + String(dpayload) + "'></div>";
  s += "</div>";

  s += "<div id='b" + p + "_d_http_fields' style='display:" + String(strcmp(dmode,"mqtt")==0?"none":"block") + ";'>";
  s += "<div class='row'><label>url post double clic</label><input type='text' name='b" + p + "_durl' value='" + String(durl) + "' placeholder='http://...'></div>";
  s += "</div>";

  s += "<button type='button' style='margin: 4px 0 12px 0; padding: 6px; font-size:12px; background:#10b981; color:#fff; border:none; border-radius:4px; cursor:pointer;' onclick='testaction(" + p + ", \"double\")'>&brvbar;&rArr; tester double clic</button>";

  // appui long
  s += "<div class='long-hdr'>&#128336; appui long</div>";
  s += "<div class='row'><label>mode d'attente</label><select name='b" + p + "_lm'>";
  s += "<option value='both'" + String(strcmp(lmode,"both")==0?" selected":"") + ">court + long (deux actions)</option>";
  s += "<option value='long'" + String(strcmp(lmode,"long")==0?" selected":"") + ">long seul (le court attend le rel&acirc;chement)</option>";
  s += "</select></div>";
  
  s += "<div class='row'><label>type d'action (long)</label><select name='b" + p + "_lact' onchange='tglFields(this," + p + ",\"l\")'>";
  s += act_option("mqtt", "mqtt seul", lact);
  s += act_option("http", "http post seul", lact);
  s += act_option("both", "mqtt + http post", lact);
  s += "</select></div>";
  
  s += "<div id='b" + p + "_l_mqtt_fields' style='display:" + String(strcmp(lact,"http")==0?"none":"block") + ";'>";
  s += "<div class='row'><label>topic long <span class='opt'>(optionnel)</span></label><input type='text' name='b" + p + "_lt' value='" + String(ltopic) + "' placeholder='laisser vide pour d&eacute;sactiver'></div>";
  s += "<div class='row'><label>payload long</label><input type='text' name='b" + p + "_lp' value='" + String(lpayload) + "'></div>";
  s += "</div>";
  
  s += "<div id='b" + p + "_l_http_fields' style='display:" + String(strcmp(lact,"mqtt")==0?"none":"block") + ";'>";
  s += "<div class='row'><label>url post longue</label><input type='text' name='b" + p + "_lurl' value='" + String(lurl) + "' placeholder='http://...'></div>";
  s += "</div>";
  
  s += "<button type='button' style='margin: 4px 0 4px 0; padding: 6px; font-size:12px; background:#ec4899; color:#fff; border:none; border-radius:4px; cursor:pointer;' onclick='testaction(" + p + ", \"long\")'>&brvbar;&rArr; tester action longue</button>";
  
  s += "</div>";
  return s;
}

void handle_setup() {
  int n = WiFi.scanNetworks();
  String html = String("<!doctype html><html><head>") +
    "<meta charset='utf-8'>" +
    "<meta name='viewport' content='width=device-width,initial-scale=1'>" +
    "<title>configuration</title>" +
    "<style>" +
    "body{margin:0;padding:0 0 40px;background:#f5f5f3;font-family:-apple-system,blinkmacsystemfont,'segoe ui',sans-serif;color:#111;}" +
    ".topbar{background:#fff;border-bottom:1px solid #e5e7eb;padding:12px 16px;display:flex;align-items:center;justify-content:space-between;position:sticky;top:0;z-index:10;}" +
    ".topbar-title{font-size:15px;font-weight:500;}" +
    ".back{font-size:13px;color:#3b82f6;text-decoration:none;}" +
    ".section{background:#fff;border-radius:12px;margin:14px 12px 0;padding:14px 16px;border:1px solid #e5e7eb;}" +
    ".sec-hdr{font-size:11px;font-weight:600;letter-spacing:0.07em;text-transform:uppercase;margin-bottom:12px;padding-bottom:8px;border-bottom:1px solid #f0f0f0;}" +
    ".sec-net{color:#3b82f6;}.sec-mqtt{color:#f59e0b;}.sec-btns{color:#10b981;}.sec-rob{color:#ec4899;}.sec-ota{color:#8b5cf6;}" +
    ".row{margin-bottom:12px;}" +
    "label{display:block;font-size:12px;color:#6b7280;margin-bottom:4px;}" +
    ".opt{color:#9ca3af;font-style:italic;}" +
    "input,select{width:100%;box-sizing:border-box;padding:9px 10px;border:1px solid #d1d5db;border-radius:8px;font-size:14px;background:#fafafa;color:#111;appearance:auto;}" +
    "input:focus,select:focus{outline:none;border-color:#3b82f6;background:#fff;}" +
    ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px;}" +
    ".btn-card{background:#f9f9f7;border:1px solid #e5e7eb;border-radius:10px;padding:12px 14px;margin-bottom:10px;}" +
    ".btn-card-hdr{display:flex;align-items:center;gap:8px;margin-bottom:10px;font-size:13px;font-weight:500;}" +
    ".clr-dot{width:10px;height:10px;border-radius:50%;flex-shrink:0;}" +
    ".long-hdr{font-size:11px;font-weight:600;color:#ec4899;letter-spacing:0.05em;text-transform:uppercase;margin:10px 0 8px;padding-top:10px;border-top:1px dashed #e5e7eb;}" +
    ".save{display:block;width:calc(100% - 24px);margin:16px 12px 0;padding:14px;background:#10b981;color:#fff;border:none;border-radius:10px;font-size:15px;font-weight:500;cursor:pointer;text-align:center;}" +
    ".save:active{background:#059669;}" +
    "</style></head><body>";

  html += "<div class='topbar'><a class='back' href='/'>&#8592; retour</a><span class='topbar-title'>configuration</span><span></span></div>";
  
  html += "<div class='section'><div class='sec-hdr sec-net'>&#9312; r&eacute;seau wifi</div>";
  html += "<div class='row'><label>r&eacute;seau d&eacute;tect&eacute;</label><select name='wifi_ssid' id='ssid_sel'>";
  if (n == 0) {
    html += "<option value=''>aucun r&eacute;seau trouv&eacute;</option>";
  } else {
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      String sel  = (ssid == String(wifi_ssid)) ? " selected" : "";
      html += "<option value='" + ssid + "'" + sel + ">" + ssid + " (" + String(WiFi.RSSI(i)) + " dbm)</option>";
    }
  }
  html += "</select></div>";
  html += "<div class='row'><label>mot de passe wpa2</label><input type='password' name='wifi_pass' id='wp' value='" + String(wifi_pass) + "'></div>";
  html += "</div>";

  html += "<div class='section'><div class='sec-hdr sec-mqtt'>&#9313; broker mqtt</div>";
  html += "<div class='row'><label>activer mqtt</label><select name='mqtt_enabled' id='me' onchange='tglMqtt(this)'>";
  html += "<option value='false'" + String(!mqtt_enabled?" selected":"") + ">d&eacute;sactiv&eacute;</option>";
  html += "<option value='true'" + String(mqtt_enabled?" selected":"") + ">activ&eacute;</option>";
  html += "</select></div>";
  
  html += "<div id='mqtt_config_block' style='display:" + String(mqtt_enabled?"block":"none") + ";'>";
  html += "<div class='grid2'>";
  html += "<div class='row'><label>serveur</label><input type='text' name='mqtt_server' id='ms' value='" + String(mqtt_server) + "'></div>";
  html += "<div class='row'><label>port</label><input type='text' name='mqtt_port' id='mp' value='" + String(mqtt_port) + "'></div>";
  html += "</div><div class='grid2'>";
  html += "<div class='row'><label>login</label><input type='text' name='mqtt_user' id='mu' value='" + String(mqtt_user) + "'></div>";
  html += "<div class='row'><label>mot de passe</label><input type='password' name='mqtt_pass' id='mpx' value='" + String(mqtt_pass) + "'></div>";
  html += "</div></div></div>";

  html += "<div class='section'><div class='sec-hdr sec-btns'>&#9314; boutons</div>";
  html += btn_card(1, "d1", btn1_name, btn1_color, btn1_mode, btn1_topic, btn1_payload, btn1_url, btn1_long_topic, btn1_long_payload, btn1_long_url, btn1_long_mode, btn1_long_act, btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url);
  html += btn_card(2, "d2", btn2_name, btn2_color, btn2_mode, btn2_topic, btn2_payload, btn2_url, btn2_long_topic, btn2_long_payload, btn2_long_url, btn2_long_mode, btn2_long_act, btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url);
  html += btn_card(3, "d5", btn3_name, btn3_color, btn3_mode, btn3_topic, btn3_payload, btn3_url, btn3_long_topic, btn3_long_payload, btn3_long_url, btn3_long_mode, btn3_long_act, btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url);
  html += "</div>";

  html += "<div class='section'><div class='sec-hdr sec-rob'>&#9315; temporisations temporelles</div>";
  html += "<div class='grid2'>";
  html += "<div class='row'><label>appui long (ms)</label><input type='number' name='long_ms' id='lms' value='" + String(long_press_ms) + "' min='300' max='3000'></div>";
  html += "<div class='row'><label>double clic (ms)</label><input type='number' name='double_ms' id='dclms' value='" + String(double_click_ms) + "' min='150' max='1000'></div>";
  html += "</div></div>";

  if (!is_ap_mode) {
    html += "<div class='section'><div class='sec-hdr sec-ota'>&#9316; mise &agrave; jour firmware</div>";
    html += "<p style='font-size:13px;color:#6b7280;margin:0 0 12px;'>chargez un fichier <b>.bin</b> g&eacute;n&eacute;r&eacute;.</p>";
    html += "<div id='ota-area'>";
    html += "<div style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;'>";
    html += "<label for='ota-file' style='display:inline-flex;align-items:center;gap:6px;padding:9px 14px;background:#f0f4ff;border:1px solid #c7d7fd;border-radius:8px;cursor:pointer;font-size:13px;color:#3b5bdb;'>&#128193; choisir un .bin</label>";
    html += "<input type='file' id='ota-file' accept='.bin' style='display:none;'>";
    html += "<span id='ota-fname' style='font-size:12px;color:#9ca3af;'>aucun fichier s&eacute;lectionn&eacute;</span>";
    html += "</div>";
    html += "<div id='ota-progress-wrap' style='display:none;margin-top:12px;'>";
    html += "<div style='background:#e5e7eb;border-radius:99px;height:8px;overflow:hidden;'>";
    html += "<div id='ota-bar' style='background:#3b82f6;height:8px;width:0%;transition:width 0.3s;'></div></div>";
    html += "<div id='ota-status' style='font-size:12px;color:#6b7280;margin-top:6px;text-align:center;'>0%</div></div>";
    html += "<button id='ota-btn' onclick='doota()' style='margin-top:12px;width:100%;padding:11px;background:#3b82f6;color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:500;cursor:pointer;'>&#8593; flasher le firmware</button>";
    html += "</div></div>";
  }

  html += "<button class='save' onclick='dosave()'>&#128190; sauvegarder et red&eacute;marrer</button>";
  
  html += "<script>"
    "var colormap={green:'#52c98a',white:'#cccccc',red:'#e05555',blue:'#5b8dee',orange:'#e09b40'};"
    "function upd(sel,dotid){var d=document.getElementById(dotid);if(d)d.style.background=colormap[sel.value]||'#ccc';}"
    "function tglMqtt(sel){"
    "document.getElementById('mqtt_config_block').style.display=(sel.value==='true')?'block':'none';"
    "}"
    "function tglFields(sel,btnid,suffix){"
    "var val=sel.value;"
    "var mq=document.getElementById('b'+btnid+'_'+suffix+'_mqtt_fields');"
    "var ht=document.getElementById('b'+btnid+'_'+suffix+'_http_fields');"
    "if(val==='mqtt') {mq.style.display='block'; ht.style.display='none';}"
    "else if(val==='http') {mq.style.display='none'; ht.style.display='block';}"
    "else {mq.style.display='block'; ht.style.display='block';}"
    "}"
    "var fi=document.getElementById('ota-file');"
    "if(fi)fi.addEventListener('change',function(){"
    "var fn=document.getElementById('ota-fname');"
    "if(fn)fn.textContent=this.files[0]?this.files[0].name:'aucun fichier s\u00e9lectionn\u00e9';"
    "});"
    "function doota() {"
    "var fi=document.getElementById('ota-file');"
    "if(!fi||!fi.files||!fi.files[0]){alert('s\u00e9lectionnez un fichier .bin');return;}"
    "var f=fi.files[0];"
    "document.getElementById('ota-btn').disabled=true;"
    "document.getElementById('ota-btn').textContent='envoi en cours...';"
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
    "document.getElementById('ota-area').innerHTML='<div style=\"color:#10b981;font-size:15px;font-weight:500;text-align:center;padding:12px 0\">&#10003; mise &agrave; jour r&eacute;ussie ! red&eacute;marrage...</div>';"
    "setTimeout(function(){window.location.href='/';},5000);"
    "}else{alert('echec du flash');document.getElementById('ota-btn').disabled=false;document.getElementById('ota-btn').textContent='flasher le firmware';}"
    "};"
    "x.send(fd);"
    "}"
    "function dosave(){"
    "var obj={};"
    "obj.wifi_ssid=document.getElementById('ssid_sel').value;"
    "obj.wifi_pass=document.getElementById('wp').value;"
    "obj.mqtt_enabled=(document.getElementById('me').value==='true');"
    "obj.mqtt_server=document.getElementById('ms').value;"
    "obj.mqtt_port=document.getElementById('mp').value;"
    "obj.mqtt_user=document.getElementById('mu').value;"
    "obj.mqtt_pass=document.getElementById('mpx').value;"
    "var inputs=document.querySelectorAll('input, select');"
    "inputs.forEach(function(i){if(i.name&&!obj.hasOwnProperty(i.name))obj[i.name]=i.value;});"
    "obj.long_press_ms=parseInt(document.getElementById('lms').value)||800;"
    "obj.double_click_ms=parseInt(document.getElementById('dclms').value)||400;"
    "var x=new XMLHttpRequest();x.open('POST','/config',true);x.setRequestHeader('Content-Type','application/json');"
    "x.onload=function(){"
    "document.body.innerHTML='<div style=\"text-align:center;padding-top:100px;font-family:sans-serif;color:#111;\"><h2>configuration enregistr&eacute;e</h2><p>red&eacute;marrage du bo&icirc;tier en cours...</p></div>';"
    "setTimeout(function(){window.location.href=\"/\";},4000);"
    "};"
    "x.send(JSON.stringify(obj));"
    "}"
    "function testaction(btnid, type){"
    "var x=new XMLHttpRequest();x.open('GET','/testaction?id='+btnid+'&type='+type,true);x.send();"
    "alert('commande de test envoy&eacute;e');"
    "}"
    "</script></body></html>";
  server.send(200, "text/html", html);
}

void handle_config_save() {
  if (server.hasArg("plain") == false) {
    server.send(400, "text/plain", "body missing");
    return;
  }
  String body = server.arg("plain");
  StaticJsonDocument<4096> json;
  if (deserializeJson(json, body) == DeserializationError::Ok) {
    strlcpy(wifi_ssid,           json["wifi_ssid"]           | "",              sizeof(wifi_ssid));
    strlcpy(wifi_pass,           json["wifi_pass"]           | "",              sizeof(wifi_pass));
    mqtt_enabled                 = json["mqtt_enabled"]      | false;
    strlcpy(mqtt_server,         json["mqtt_server"]         | "",              sizeof(mqtt_server));
    strlcpy(mqtt_port,           json["mqtt_port"]           | "1883",          sizeof(mqtt_port));
    strlcpy(mqtt_user,           json["mqtt_user"]           | "",              sizeof(mqtt_user));
    strlcpy(mqtt_pass,           json["mqtt_pass"]           | "",              sizeof(mqtt_pass));
    
    // boutons 1
    strlcpy(btn1_mode,           json["b1_mode"]             | "mqtt",          sizeof(btn1_mode));
    strlcpy(btn1_topic,          json["b1_t"]                | "",              sizeof(btn1_topic));
    strlcpy(btn1_payload,        json["b1_p"]                | "",              sizeof(btn1_payload));
    strlcpy(btn1_url,            json["b1_url"]              | "",              sizeof(btn1_url));
    strlcpy(btn1_name,           json["b1_name"]             | "up",            sizeof(btn1_name));
    strlcpy(btn1_color,          json["b1_color"]            | "green",         sizeof(btn1_color));
    strlcpy(btn1_long_topic,     json["b1_lt"]               | "",              sizeof(btn1_long_topic));
    strlcpy(btn1_long_payload,   json["b1_lp"]               | "",              sizeof(btn1_long_payload));
    strlcpy(btn1_long_url,       json["b1_lurl"]             | "",              sizeof(btn1_long_url));
    strlcpy(btn1_long_mode,      json["b1_lm"]               | "both",          sizeof(btn1_long_mode));
    strlcpy(btn1_long_act,       json["b1_lact"]             | "mqtt",          sizeof(btn1_long_act));
    strlcpy(btn1_click_mode,     json["b1_dmode"]            | "mqtt",          sizeof(btn1_click_mode));
    strlcpy(btn1_click_topic,    json["b1_dt"]               | "",              sizeof(btn1_click_topic));
    strlcpy(btn1_click_payload,  json["b1_dp"]               | "",              sizeof(btn1_click_payload));
    strlcpy(btn1_click_url,      json["b1_durl"]             | "",              sizeof(btn1_click_url));

    // boutons 2
    strlcpy(btn2_mode,           json["b2_mode"]             | "mqtt",          sizeof(btn2_mode));
    strlcpy(btn2_topic,          json["b2_t"]                | "",              sizeof(btn2_topic));
    strlcpy(btn2_payload,        json["b2_p"]                | "",              sizeof(btn2_payload));
    strlcpy(btn2_url,            json["b2_url"]              | "",              sizeof(btn2_url));
    strlcpy(btn2_name,           json["b2_name"]             | "my",            sizeof(btn2_name));
    strlcpy(btn2_color,          json["b2_color"]            | "white",         sizeof(btn2_color));
    strlcpy(btn2_long_topic,     json["b2_lt"]               | "",              sizeof(btn2_long_topic));
    strlcpy(btn2_long_payload,   json["b2_lp"]               | "",              sizeof(btn2_long_payload));
    strlcpy(btn2_long_url,       json["b2_lurl"]             | "",              sizeof(btn2_long_url));
    strlcpy(btn2_long_mode,      json["b2_lm"]               | "both",          sizeof(btn2_long_mode));
    strlcpy(btn2_long_act,       json["b2_lact"]             | "mqtt",          sizeof(btn2_long_act));
    strlcpy(btn2_click_mode,     json["b2_dmode"]            | "mqtt",          sizeof(btn2_click_mode));
    strlcpy(btn2_click_topic,    json["b2_dt"]               | "",              sizeof(btn2_click_topic));
    strlcpy(btn2_click_payload,  json["b2_dp"]               | "",              sizeof(btn2_click_payload));
    strlcpy(btn2_click_url,      json["b2_durl"]             | "",              sizeof(btn2_click_url));

    // boutons 3
    strlcpy(btn3_mode,           json["b3_mode"]             | "mqtt",          sizeof(btn3_mode));
    strlcpy(btn3_topic,          json["b3_t"]                | "",              sizeof(btn3_topic));
    strlcpy(btn3_payload,        json["b3_p"]                | "",              sizeof(btn3_payload));
    strlcpy(btn3_url,            json["b3_url"]              | "",              sizeof(btn3_url));
    strlcpy(btn3_name,           json["b3_name"]             | "down",          sizeof(btn3_name));
    strlcpy(btn3_color,          json["b3_color"]            | "red",           sizeof(btn3_color));
    strlcpy(btn3_long_topic,     json["b3_lt"]               | "",              sizeof(btn3_long_topic));
    strlcpy(btn3_long_payload,   json["b3_lp"]               | "",              sizeof(btn3_long_payload));
    strlcpy(btn3_long_url,       json["b3_lurl"]             | "",              sizeof(btn3_long_url));
    strlcpy(btn3_long_mode,      json["b3_lm"]               | "both",          sizeof(btn3_long_mode));
    strlcpy(btn3_long_act,       json["b3_lact"]             | "mqtt",          sizeof(btn3_long_act));
    strlcpy(btn3_click_mode,     json["b3_dmode"]            | "mqtt",          sizeof(btn3_click_mode));
    strlcpy(btn3_click_topic,    json["b3_dt"]               | "",              sizeof(btn3_click_topic));
    strlcpy(btn3_click_payload,  json["b3_dp"]               | "",              sizeof(btn3_click_payload));
    strlcpy(btn3_click_url,      json["b3_durl"]             | "",              sizeof(btn3_click_url));
    
    long_press_ms   = json["long_press_ms"]   | 800;
    double_click_ms = json["double_click_ms"] | 400;
    save_config();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
    delay(500);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "bad json");
  }
}

void handle_trigger() {
  if (!server.hasArg("id")) { server.send(400, "text/plain", "missing id"); return; }
  int id = server.arg("id").toInt();
  String req_type = server.hasArg("type") ? server.arg("type") : "court";
  bool ok = false;
  
  if (req_type == "long") {
    if (id == 1) ok = execute_action(btn1_long_act, btn1_long_topic, btn1_long_payload, btn1_long_url, "web-btn1-long");
    if (id == 2) ok = execute_action(btn2_long_act, btn2_long_topic, btn2_long_payload, btn2_long_url, "web-btn2-long");
    if (id == 3) ok = execute_action(btn3_long_act, btn3_long_topic, btn3_long_payload, btn3_long_url, "web-btn3-long");
  } else {
    if (id == 1) ok = execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "web-btn1-court");
    if (id == 2) ok = execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "web-btn2-court");
    if (id == 3) ok = execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "web-btn3-court");
  }
  server.send(200, "text/plain", ok ? "ok" : "error");
}

void handle_test_action() {
  if (!server.hasArg("id") || !server.hasArg("type")) {
    server.send(400, "text/plain", "bad arguments");
    return;
  }
  int id = server.arg("id").toInt();
  String type = server.arg("type");
  bool ok = false;

  if (type == "court") {
    if (id == 1) ok = execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "test-b1-court");
    if (id == 2) ok = execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "test-b2-court");
    if (id == 3) ok = execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "test-b3-court");
  } else if (type == "long") {
    if (id == 1) ok = execute_action(btn1_long_act, btn1_long_topic, btn1_long_payload, btn1_long_url, "test-b1-long");
    if (id == 2) ok = execute_action(btn2_long_act, btn2_long_topic, btn2_long_payload, btn2_long_url, "test-b2-long");
    if (id == 3) ok = execute_action(btn3_long_act, btn3_long_topic, btn3_long_payload, btn3_long_url, "test-b3-long");
  } else if (type == "double") {
    if (id == 1) ok = execute_action(btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url, "test-b1-double");
    if (id == 2) ok = execute_action(btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url, "test-b2-double");
    if (id == 3) ok = execute_action(btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url, "test-b3-double");
  }
  server.send(200, "text/plain", ok ? "ok" : "error");
}

// ================================================================== reseau & connexion

void start_ap_mode() {
  is_ap_mode = true;
  net_state = net_ok;
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ap_name[30];
  sprintf(ap_name, "telecommande-%02x%02x", mac[4], mac[5]);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_name);
  Serial.println("[wifi] echec connexion. mode ap lance : " + String(ap_name));
}

void connect_network() {
  if (strlen(wifi_ssid) == 0) {
    start_ap_mode();
    return;
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pass);
  Serial.println("[wifi] connexion a " + String(wifi_ssid));
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(200);
    digitalWrite(pin_led, !digitalRead(pin_led));
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[wifi] connecte ! ip : " + WiFi.localIP().toString());
    net_state = mqtt_enabled ? net_mqtt_ko : net_ok;
  } else {
    start_ap_mode();
  }
}

// ================================================================== setup & loop

void setup() {
  Serial.begin(115200);
  Serial.println("");

  pinMode(btn1_pin, INPUT);  // TTP223 : actif HIGH, pas de pull-up
  pinMode(btn2_pin, INPUT);  // TTP223 : actif HIGH, pas de pull-up
  pinMode(btn3_pin, INPUT);  // TTP223 : actif HIGH, pas de pull-up
  pinMode(pin_led, OUTPUT);
  digitalWrite(pin_led, HIGH);

  load_config();
  connect_network();

  if (mqtt_enabled) {
    int port = atoi(mqtt_port);
    mqtt_client.setServer(mqtt_server, port);
  }

  server.on("/", handle_root);
  server.on("/setup", handle_setup);
  server.on("/config", HTTP_POST, handle_config_save);
  server.on("/trigger", handle_trigger);
  server.on("/testaction", handle_test_action);

  httpupdater.setup(&server);
  server.begin();
  Serial.println("[http] serveur web demarre");
}

void loop() {
  server.handleClient();
  unsigned long now = millis();
  update_led();

  if (mqtt_enabled && !is_ap_mode) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!mqtt_client.connected()) {
        net_state = net_mqtt_ko;
        if (now - last_mqtt_attempt >= 10000) {
          last_mqtt_attempt = now;
          Serial.println("[mqtt] tentative de connexion...");
          String clientid = "esp-btn-" + String(random(0, 999));
          bool connected = false;
          if (strlen(mqtt_user) > 0) {
            connected = mqtt_client.connect(clientid.c_str(), mqtt_user, mqtt_pass);
          } else {
            connected = mqtt_client.connect(clientid.c_str());
          }
          if (connected) {
            Serial.println("[mqtt] connecte au broker");
            net_state = net_ok;
            reconnect_count++;
          } else {
            Serial.println("[mqtt] echec connexion");
          }
        }
      } else {
        mqtt_client.loop();
        net_state = net_ok;
      }
    } else {
      net_state = net_wifi_ko;
      if (now - last_wifi_attempt >= wifi_backoff_ms) {
        last_wifi_attempt = now;
        Serial.println("[wifi] perte de signal, reconnexion...");
        WiFi.begin(wifi_ssid, wifi_pass);
      }
    }
  } else if (!is_ap_mode) {
    if (WiFi.status() == WL_CONNECTED) {
      net_state = net_ok;
    } else {
      net_state = net_wifi_ko;
      if (now - last_wifi_attempt >= wifi_backoff_ms) {
        last_wifi_attempt = now;
        WiFi.begin(wifi_ssid, wifi_pass);
      }
    }
  }

  // ---- lecture physique des boutons ----
  bool r1 = digitalRead(btn1_pin);
  bool r2 = digitalRead(btn2_pin);
  bool r3 = digitalRead(btn3_pin);

  // ---- machine d'etats bouton 1 ----
  if (r1 != last_raw_btn1_state) { last_debounce_time1 = now; last_raw_btn1_state = r1; }
  if ((now - last_debounce_time1) > debounce_delay) {
    if (r1 != debounced_btn1_state) {
      debounced_btn1_state = r1;
      if (debounced_btn1_state == HIGH) { // TTP223 : toucher=HIGH
        pressed1 = true; long_fired1 = false; press_start1 = now;
      } else {
        if (pressed1 && !long_fired1) {
          click_count1++;
          release_time1 = now;
        }
        pressed1 = false;
      }
    }
  }
  if (pressed1 && !long_fired1 && (now - press_start1 >= long_press_ms)) {
    long_fired1 = true; click_count1 = 0;
    Serial.println("[action] d1 long");
    execute_action(btn1_long_act, btn1_long_topic, btn1_long_payload, btn1_long_url, "btn1-long");
  }
  if (!pressed1 && click_count1 > 0 && (now - release_time1 >= double_click_ms)) {
    if (click_count1 >= 2) {
      if (strlen(btn1_click_topic) > 0 || strlen(btn1_click_url) > 0) {
        Serial.println("[action] d1 double clic");
        execute_action(btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url, "btn1-double");
      }
    } else {
      if (strcmp(btn1_long_mode, "long") != 0 || (release_time1 - press_start1 < long_press_ms)) {
        Serial.println("[action] d1 court");
        execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court");
      }
    }
    click_count1 = 0;
  }

  // ---- machine d'etats bouton 2 ----
  if (r2 != last_raw_btn2_state) { last_debounce_time2 = now; last_raw_btn2_state = r2; }
  if ((now - last_debounce_time2) > debounce_delay) {
    if (r2 != debounced_btn2_state) {
      debounced_btn2_state = r2;
      if (debounced_btn2_state == HIGH) { // TTP223 : toucher=HIGH
        pressed2 = true; long_fired2 = false; press_start2 = now;
      } else {
        if (pressed2 && !long_fired2) {
          click_count2++;
          release_time2 = now;
        }
        pressed2 = false;
      }
    }
  }
  if (pressed2 && !long_fired2 && (now - press_start2 >= long_press_ms)) {
    long_fired2 = true; click_count2 = 0;
    Serial.println("[action] d2 long");
    execute_action(btn2_long_act, btn2_long_topic, btn2_long_payload, btn2_long_url, "btn2-long");
  }
  if (!pressed2 && click_count2 > 0 && (now - release_time2 >= double_click_ms)) {
    if (click_count2 >= 2) {
      if (strlen(btn2_click_topic) > 0 || strlen(btn2_click_url) > 0) {
        Serial.println("[action] d2 double clic");
        execute_action(btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url, "btn2-double");
      }
    } else {
      if (strcmp(btn2_long_mode, "long") != 0 || (release_time2 - press_start2 < long_press_ms)) {
        Serial.println("[action] d2 court");
        execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court");
      }
    }
    click_count2 = 0;
  }

  // ---- machine d'etats bouton 3 ----
  if (r3 != last_raw_btn3_state) { last_debounce_time3 = now; last_raw_btn3_state = r3; }
  if ((now - last_debounce_time3) > debounce_delay) {
    if (r3 != debounced_btn3_state) {
      debounced_btn3_state = r3;
      if (debounced_btn3_state == HIGH) { // TTP223 : toucher=HIGH
        pressed3 = true; long_fired3 = false; press_start3 = now;
      } else {
        if (pressed3 && !long_fired3) {
          click_count3++;
          release_time3 = now;
        }
        pressed3 = false;
      }
    }
  }
  if (pressed3 && !long_fired3 && (now - press_start3 >= long_press_ms)) {
    long_fired3 = true; click_count3 = 0;
    Serial.println("[action] d5 long");
    execute_action(btn3_long_act, btn3_long_topic, btn3_long_payload, btn3_long_url, "btn3-long");
  }
  if (!pressed3 && click_count3 > 0 && (now - release_time3 >= double_click_ms)) {
    if (click_count3 >= 2) {
      if (strlen(btn3_click_topic) > 0 || strlen(btn3_click_url) > 0) {
        Serial.println("[action] d5 double clic");
        execute_action(btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url, "btn3-double");
      }
    } else {
      if (strcmp(btn3_long_mode, "long") != 0 || (release_time3 - press_start3 < long_press_ms)) {
        Serial.println("[action] d5 court");
        execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court");
      }
    }
    click_count3 = 0;
  }
}
