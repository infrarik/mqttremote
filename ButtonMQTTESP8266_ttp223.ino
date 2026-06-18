//
// Use TTP223 sensors as buttons
//
// Code updated with modified inputs, delays, and backup/restore features
//
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
unsigned int  reconnect_count    = 0;

// clignotement led non bloquant
unsigned long last_led_toggle    = 0;
bool          led_state          = false;

// ------------------------------------------------------------------ debounce & etats filtres
bool last_raw_btn1_state = LOW;   // ttp223 repos=LOW
bool last_raw_btn2_state = LOW;
bool last_raw_btn3_state = LOW;
bool debounced_btn1_state = LOW;
bool debounced_btn2_state = LOW;
bool debounced_btn3_state = LOW;

unsigned long last_debounce_time1 = 0;
unsigned long last_debounce_time2 = 0;
unsigned long last_debounce_time3 = 0;
const unsigned long debounce_delay = 50;

// structures pour machine d'etats
unsigned long press_start1 = 0; unsigned long release_time1 = 0; int click_count1 = 0; bool pressed1 = false; bool long_fired1 = false;
unsigned long press_start2 = 0; unsigned long release_time2 = 0; int click_count2 = 0; bool pressed2 = false; bool long_fired2 = false;
unsigned long press_start3 = 0; unsigned long release_time3 = 0; int click_count3 = 0; bool pressed3 = false; bool long_fired3 = false;

// ------------------------------------------------------------------ objets
ESP8266WebServer         server(80);
ESP8266HTTPUpdateServer  httpupdater;
WiFiClient               esp_client;
PubSubClient             mqtt_client(esp_client);

bool is_ap_mode = false;

// ================================================================== helpers couleur et style

String color_styles(const char* c) {
  String s = String(c);
  if (s == "green")  return "background:#111827;border:1px solid #10b981;color:#10b981;box-shadow:0 0 10px rgba(16,185,129,0.15);";
  if (s == "red")    return "background:#111827;border:1px solid #ef4444;color:#ef4444;box-shadow:0 0 10px rgba(239,68,68,0.15);";
  if (s == "blue")   return "background:#111827;border:1px solid #3b82f6;color:#3b82f6;box-shadow:0 0 10px rgba(59,130,246,0.15);";
  if (s == "orange") return "background:#111827;border:1px solid #f59e0b;color:#f59e0b;box-shadow:0 0 10px rgba(245,158,11,0.15);";
  return "background:#111827;border:1px solid #f3f4f6;color:#f3f4f6;box-shadow:0 0 10px rgba(243,244,246,0.1);";
}

String color_hex(const char* c) {
  String s = String(c);
  if (s == "green")  return "#10b981";
  if (s == "red")    return "#ef4444";
  if (s == "blue")   return "#3b82f6";
  if (s == "orange") return "#f59e0b";
  return "#f3f4f6";
}

String btn_icon(int n) {
  if (n == 1) return "&#9650;";
  if (n == 3) return "&#9660;";
  return "&#9632;";
}

// ================================================================== envoyer l'action

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
        Serial.println("[mqtt] non connecte");
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
          Serial.println("[http] impossible de se connecter");
          success = false;
        }
      } else {
        Serial.println("[http] wifi non connecte");
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
    case net_ok:           interval = 0; break;
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

  String html = String("") +
    "<!doctype html><html><head>" +
    "<meta charset='utf-8'>" +
    "<meta name='viewport' content='width=device-width,initial-scale=1'>" +
    "<title>telecommande</title>" +
    "<style>" +
    "body{margin:0;padding:0;background:#111827;font-family:-apple-system,blinkmacsystemfont,'segoe ui',sans-serif;display:flex;flex-direction:column;align-items:center;min-height:100vh;color:#f3f4f6;}" +
    ".card{width:92%;max-width:340px;margin:0 auto;}" +
    ".hdr{padding:20px 0 10px;width:92%;max-width:340px;display:flex;flex-direction:column;align-items:center;gap:6px;}" +
    ".status-row{display:flex;gap:8px;}" +
    ".pill{display:flex;align-items:center;gap:5px;font-size:12px;padding:4px 10px;border-radius:99px;font-weight:500;}" +
    ".pill-ok{background:#065f46;color:#34d399;border:1px solid #047857;}" +
    ".pill-err{background:#991b1b;color:#f87171;border:1px solid #b91c1c;}" +
    ".pill-dis{background:#374151;color:#9ca3af;border:1px solid #4b5563;}" +
    ".dot{width:7px;height:7px;border-radius:50%;}" +
    ".dot-ok{background:#34d399;}" +
    ".dot-err{background:#f87171;}" +
    ".dot-dis{background:#9ca3af;}" +
    ".ip{font-size:11px;color:#9ca3af;margin-top:2px;font-family:monospace;}" +
    ".recon{font-size:10px;color:#f87171;margin-top:1px;}" +
    ".tgl-container{display:flex;align-items:center;justify-content:space-between;background:#1f2937;padding:12px 16px;border-radius:12px;margin:8px 0;width:100%;box-sizing:border-box;border:1px solid #374151;}" +
    ".tgl-lbl{font-size:12px;color:#9ca3af;font-weight:600;text-transform:uppercase;letter-spacing:0.05em;}" +
    ".sw{position:relative;display:inline-block;width:44px;height:24px;}" +
    ".sw input{opacity:0;width:0;height:0;}" +
    ".sl{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#4b5563;transition:.2s;border-radius:24px;}" +
    ".sl:before{position:absolute;content:'';height:18px;width:18px;left:3px;bottom:3px;background-color:white;transition:.2s;border-radius:50%;}" +
    "input:checked + .sl{background-color:#ec4899;}" +
    "input:checked + .sl:before{transform:translateX(20px);}" +
    ".btns{display:flex;flex-direction:column;gap:14px;padding:8px 0 20px;width:100%;}" +
    ".btn{width:100%;padding:24px 0;border-radius:14px;cursor:pointer;font-size:24px;font-weight:600;letter-spacing:0.03em;display:flex;flex-direction:column;align-items:center;gap:6px;box-sizing:border-box;transition:all 0.15s ease;}" +
    ".btn-icon{font-size:14px;opacity:0.6;}" +
    ".btn:active{opacity:0.75;transform:scale(0.97);}" +
    ".footer{padding:16px 0 24px;border-top:1px solid #1f2937;width:100%;text-align:center;}" +
    ".footer a{color:#6b7280;font-size:13px;text-decoration:none;font-weight:500;transition:color 0.2s;}" +
    ".footer a:hover{color:#3b82f6;}" +
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
  html += "<div class='footer'><a href='/setup'>&#9881; param&egrave;tres configuration</a></div>";
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

String btn_card(int n, const char* name, const char* color, 
                 const char* mode, const char* topic, const char* payload, const char* url, 
                 const char* ltopic, const char* lpayload, const char* lurl, const char* lmode, const char* lact,
                 const char* dmode, const char* dtopic, const char* dpayload, const char* durl) {
  
  String p   = String(n);
  String dot = "dot" + p;
  String s;
  s += "<div class='btn-card'>";
  s += "<div class='btn-card-hdr'><div class='clr-dot' id='" + dot + "' style='background:" + color_hex(color) + "'></div>";
  s += "Bouton " + p + "</div>";
  s += "<div class='row'><label>nom affich&eacute;</label><input type='text' name='b" + p + "_name' value='" + String(name) + "' maxlength='12'></div>";
  s += "<div class='row'><label>couleur bouton d'accueil</label><select name='b" + p + "_color' onchange='upd(this,\"" + dot + "\")'>";
  s += color_option("green", "vert fluo", color);
  s += color_option("white", "blanc épuré", color);
  s += color_option("red", "rouge alerte", color);
  s += color_option("blue", "bleu tech", color);
  s += color_option("orange", "orange vif", color);
  s += "</select></div>";
  
  s += "<div class='long-hdr' style='color:#10b981; border-top:none; padding-top:0;'>&#9642; action clic court</div>";
  s += "<div class='row'><label>type de protocole</label><select name='b" + p + "_mode' onchange='tglFields(this," + p + ",\"c\")'>";
  s += act_option("mqtt", "mqtt uniquement", mode);
  s += act_option("http", "http post uniquement", mode);
  s += act_option("both", "mqtt + http post simultan&eacute;", mode);
  s += "</select></div>";
  
  s += "<div id='b" + p + "_c_mqtt_fields' style='display:" + String(strcmp(mode, "http") == 0 ? "none" : "block") + ";'>";
  s += "<div class='grid2'><div class='row'><label>topic court</label><input type='text' name='b" + p + "_t' value='" + String(topic) + "'></div>";
  s += "<div class='row'><label>payload court</label><input type='text' name='b" + p + "_p' value='" + String(payload) + "'></div></div>";
  s += "</div>";
  
  s += "<div id='b" + p + "_c_http_fields' style='display:" + String(strcmp(mode, "mqtt") == 0 ? "none" : "block") + ";'>";
  s += "<div class='row'><label>url cible courte</label><input type='text' name='b" + p + "_url' value='" + String(url) + "' placeholder='http://...'></div>";
  s += "</div>";
  s += "<button type='button' class='tst-btn' style='background:#10b981;' onclick='testaction(" + p + ", \"court\")'>tester action courte</button>";
  
  s += "<div class='long-hdr' style='color:#3b82f6; border-top-color:#374151;'>&#9898;&#9898; action double clic</div>";
  s += "<div class='row'><label>type de protocole</label><select name='b" + p + "_dmode' onchange='tglFields(this," + p + ",\"d\")'>";
  s += act_option("mqtt", "mqtt uniquement", dmode);
  s += act_option("http", "http post uniquement", dmode);
  s += act_option("both", "mqtt + http post simultan&eacute;", dmode);
  s += "</select></div>";
  
  s += "<div id='b" + p + "_d_mqtt_fields' style='display:" + String(strcmp(dmode, "http") == 0 ? "none" : "block") + ";'>";
  s += "<div class='grid2'><div class='row'><label>topic double clic</label><input type='text' name='b" + p + "_dt' value='" + String(dtopic) + "' placeholder='vide = d&eacute;sactiv&eacute;'></div>";
  s += "<div class='row'><label>payload double</label><input type='text' name='b" + p + "_dp' value='" + String(dpayload) + "'></div></div>";
  s += "</div>";
  
  s += "<div id='b" + p + "_d_http_fields' style='display:" + String(strcmp(dmode, "mqtt") == 0 ? "none" : "block") + ";'>";
  s += "<div class='row'><label>url cible double clic</label><input type='text' name='b" + p + "_durl' value='" + String(durl) + "' placeholder='http://...'></div>";
  s += "</div>";
  s += "<button type='button' class='tst-btn' style='background:#3b82f6;' onclick='testaction(" + p + ", \"double\")'>tester double clic</button>";
  
  s += "<div class='long-hdr' style='color:#ec4899; border-top-color:#374151;'>&#128336; action appui long</div>";
  s += "<div class='row'><label>comportement d'attente temporel</label><select name='b" + p + "_lm'>";
  s += "<option value='both'" + String(strcmp(lmode, "both") == 0 ? " selected" : "") + ">ex&eacute;cuter court d'abord, puis long s'il se prolonge</option>";
  s += "<option value='long'" + String(strcmp(lmode, "long") == 0 ? " selected" : "") + ">bloquer le court, ex&eacute;cuter le long au relâchement</option>";
  s += "</select></div>";
  
  s += "<div class='row'><label>type de protocole</label><select name='b" + p + "_lact' onchange='tglFields(this," + p + ",\"l\")'>";
  s += act_option("mqtt", "mqtt uniquement", lact);
  s += act_option("http", "http post uniquement", lact);
  s += act_option("both", "mqtt + http post simultan&eacute;", lact);
  s += "</select></div>";
  
  s += "<div id='b" + p + "_l_mqtt_fields' style='display:" + String(strcmp(lact, "http") == 0 ? "none" : "block") + ";'>";
  s += "<div class='grid2'><div class='row'><label>topic appui long</label><input type='text' name='b" + p + "_lt' value='" + String(ltopic) + "' placeholder='vide = d&eacute;sactiv&eacute;'></div>";
  s += "<div class='row'><label>payload long</label><input type='text' name='b" + p + "_lp' value='" + String(lpayload) + "'></div></div>";
  s += "</div>";
  
  s += "<div id='b" + p + "_l_http_fields' style='display:" + String(strcmp(lact, "mqtt") == 0 ? "none" : "block") + ";'>";
  s += "<div class='row'><label>url cible appui long</label><input type='text' name='b" + p + "_lurl' value='" + String(lurl) + "' placeholder='http://...'></div>";
  s += "</div>";
  s += "<button type='button' class='tst-btn' style='background:#ec4899;' onclick='testaction(" + p + ", \"long\")'>tester appui long</button>";
  
  s += "</div>";
  return s;
}

void handle_setup() {
  String html = String("") +
    "<!doctype html><html><head>" +
    "<meta charset='utf-8'>" +
    "<meta name='viewport' content='width=device-width,initial-scale=1'>" +
    "<title>configuration</title>" +
    "<style>" +
    "body{margin:0;padding:20px 0;background:#0f172a;font-family:-apple-system,blinkmacsystemfont,'segoe ui',sans-serif;color:#f8fafc;display:flex;flex-direction:column;align-items:center;min-height:100vh;box-sizing:border-box;}" +
    ".container{width:92%;max-width:680px;display:flex;flex-direction:column;gap:16px;}" +
    "form{display:flex;flex-direction:column;gap:16px;width:100%;}" +
    ".section{background:#1e293b;padding:20px;border-radius:14px;border:1px solid #334155;box-shadow:0 4px 6px -1px rgba(0,0,0,0.1);box-sizing:border-box;}" +
    "h2{margin:0 0 16px;font-size:16px;text-transform:uppercase;letter-spacing:0.05em;color:#38bdf8;border-bottom:1px solid #334155;padding-bottom:8px;display:flex;align-items:center;gap:8px;}" +
    ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:14px;}" +
    "@media(max-width:500px){.grid2{grid-template-columns:1fr;}.bkp-row{flex-direction:column!important;align-items:stretch!important;}}" +
    ".row{display:flex;flex-direction:column;gap:5px;margin-bottom:12px;}" +
    "label{font-size:12px;color:#94a3b8;font-weight:500;}" +
    "input[type='text'],input[type='password'],select,input[type='file']{background:#0f172a;border:1px solid #334155;padding:10px 12px;border-radius:8px;color:#f8fafc;font-size:14px;outline:none;transition:border 0.2s;box-sizing:border-box;width:100%;}" +
    "input:focus,select:focus{border-color:#38bdf8;}" +
    ".btn-card{background:#1e293b;border:1px solid #334155;border-radius:14px;padding:20px;box-sizing:border-box;display:flex;flex-direction:column;margin-bottom:16px;}" +
    ".btn-card-hdr{font-size:14px;font-weight:600;margin-bottom:14px;display:flex;align-items:center;gap:8px;color:#f1f5f9;text-transform:uppercase;letter-spacing:0.02em;}" +
    ".clr-dot{width:10px;height:10px;border-radius:50%;box-shadow:0 0 8px currentColor;}" +
    ".long-hdr{font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:0.05em;margin:14px 0 10px;padding-top:10px;border-top:1px dashed #334155;}" +
    ".tst-btn{width:100%;padding:8px;border:none;border-radius:6px;color:#111827;font-size:11px;font-weight:700;text-transform:uppercase;letter-spacing:0.03em;cursor:pointer;margin:4px 0 12px;transition:opacity 0.15s;}" +
    ".tst-btn:hover{opacity:0.9;}" +
    ".actions{display:flex;gap:14px;justify-content:flex-end;margin-top:8px;}" +
    ".btn-sub{background:#38bdf8;color:#0f172a;font-weight:600;font-size:15px;padding:12px 28px;border:none;border-radius:8px;cursor:pointer;transition:all 0.15s;box-shadow:0 4px 12px rgba(56,189,248,0.2);}" +
    ".btn-sub:hover{background:#7dd3fc;transform:translateY(-1px);}" +
    ".btn-back{background:transparent;color:#94a3b8;font-weight:500;font-size:14px;padding:12px 20px;border:1px solid #334155;border-radius:8px;cursor:pointer;transition:all 0.15s;text-decoration:none;display:inline-flex;align-items:center;}" +
    ".btn-back:hover{background:#1e293b;color:#f8fafc;border-color:#475569;}" +
    ".bkp-row{display:flex;gap:14px;align-items:center;justify-content:between;margin-top:8px;}" +
    ".btn-bkp{background:#475569;color:#f8fafc;font-weight:600;font-size:13px;padding:10px 16px;border:none;border-radius:8px;cursor:pointer;text-decoration:none;text-align:center;transition:background 0.15s;}" +
    ".btn-bkp:hover{background:#64748b;}" +
    "body::-webkit-scrollbar{width:8px;}body::-webkit-scrollbar-track{background:#0f172a;}body::-webkit-scrollbar-thumb{background:#334155;border-radius:4px;}" +
    "</style>" +
    "<script>" +
    "function tglMqtt(cb){document.getElementById('mqtt_fields').style.display=cb.checked?'block':'none';}" +
    "function tglFields(sel,p,prefix){" +
      "var m=sel.value;" +
      "document.getElementById('b'+p+'_'+prefix+'_mqtt_fields').style.display=(m==='http')?'none':'block';" +
      "document.getElementById('b'+p+'_'+prefix+'_http_fields').style.display=(m==='mqtt')?'none':'block';" +
    "}" +
    "function upd(sel,dotId){" +
      "var c='#f3f4f6';" +
      "if(sel.value==='green')c='#10b981';" +
      "if(sel.value==='red')c='#ef4444';" +
      "if(sel.value==='blue')c='#3b82f6';" +
      "if(sel.value==='orange')c='#f59e0b';" +
      "document.getElementById(dotId).style.background=c;" +
    "}" +
    "function testaction(id,type){" +
      "var x=new XMLHttpRequest();" +
      "x.open('GET','/trigger?id='+id+'&type='+type,true);" +
      "x.send();" +
      "alert('Commande de test '+type+' envoyée pour le bouton '+id);" +
    "}" +
    "</script></head><body>" +
    "<div class='container'>";

  // section outil de sauvegarde/restauration
  html += "<div class='section'><h2>&#128190; Sauvegarde & Restauration</h2>";
  html += "<div class='bkp-row'>";
  html += "<a href='/backup' class='btn-bkp'>Télécharger la configuration (backup)</a>";
  html += "<form method='POST' action='/restore' enctype='multipart/form-data' style='display:inline-flex;gap:10px;align-items:center;width:auto;flex-grow:1;'>";
  html += "<input type='file' name='restore_file' accept='.json' required style='padding:6px 10px;'>";
  html += "<button type='submit' class='btn-bkp' style='background:#059669;'>Restaurer</button>";
  html += "</form></div></div>";
    
  html += "<form method='POST' action='/save'>";
    
  html += "<div class='section'><h2>&#128246; Param&egrave;tres Connectivit&eacute;</h2>";
  html += "<div class='grid2'><div class='row'><label>nom du r&eacute;seau wifi (ssid)</label><input type='text' name='ws' value='" + String(wifi_ssid) + "' maxlength='31'></div>";
  html += "<div class='row'><label>mot de passe wifi</label><input type='password' name='wp' value='" + String(wifi_pass) + "' maxlength='63'></div></div>";
  html += "<div class='row' style='flex-direction:row;align-items:center;gap:10px;margin-top:6px;margin-bottom:0;'>" +
          String("<input type='checkbox' name='me' value='1' id='me' onchange='tglMqtt(this)'") + (mqtt_enabled ? " checked" : "") + ">" +
          "<label for='me' style='font-size:14px;color:#f1f5f9;cursor:pointer;'>Activer le protocole MQTT</label></div>";
  html += "<div id='mqtt_fields' style='display:" + String(mqtt_enabled ? "block" : "none") + ";margin-top:14px;'>";
  html += "<div class='grid2'><div class='row'><label>adresse du serveur mqtt</label><input type='text' name='ms' value='" + String(mqtt_server) + "' maxlength='39'></div>";
  html += "<div class='row'><label>port mqtt</label><input type='text' name='mp' value='" + String(mqtt_port) + "' maxlength='5'></div></div>";
  html += "<div class='grid2'><div class='row'><label>identifiant utilisateur mqtt</label><input type='text' name='mu' value='" + String(mqtt_user) + "' maxlength='39'></div>";
  html += "<div class='row'><label>mot de passe mqtt</label><input type='password' name='mwp' value='" + String(mqtt_pass) + "' maxlength='39'></div></div>";
  html += "</div></div>";

  html += "<div class='section'><h2>&#9201; Seuils Temporels</h2>";
  html += "<div class='grid2'><div class='row'><label>dur&eacute;e appui long (ms)</label><input type='text' name='t_long' value='" + String(long_press_ms) + "' maxlength='5'></div>";
  html += "<div class='row'><label>intervalle double clic (ms)</label><input type='text' name='t_double' value='" + String(double_click_ms) + "' maxlength='5'></div></div>";
  html += "</div>";

  // affichage linéaire (une seule colonne) comme avant
  html += btn_card(1, btn1_name, btn1_color, btn1_mode, btn1_topic, btn1_payload, btn1_url, btn1_long_topic, btn1_long_payload, btn1_long_url, btn1_long_mode, btn1_long_act, btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url);
  html += btn_card(2, btn2_name, btn2_color, btn2_mode, btn2_topic, btn2_payload, btn2_url, btn2_long_topic, btn2_long_payload, btn2_long_url, btn2_long_mode, btn2_long_act, btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url);
  html += btn_card(3, btn3_name, btn3_color, btn3_mode, btn3_topic, btn3_payload, btn3_url, btn3_long_topic, btn3_long_payload, btn3_long_url, btn3_long_mode, btn3_long_act, btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url);

  html += "<div class='actions'><a href='/' class='btn-back'>&#10094; Annuler</a><button type='submit' class='btn-sub'>Enregistrer la Configuration</button></div>";
  html += "</form></div></body></html>";
  
  server.send(200, "text/html", html);
}

void handle_save() {
  strlcpy(wifi_ssid,           server.arg("ws").c_str(),      sizeof(wifi_ssid));
  strlcpy(wifi_pass,           server.arg("wp").c_str(),      sizeof(wifi_pass));
  mqtt_enabled                 = server.hasArg("me");
  strlcpy(mqtt_server,         server.arg("ms").c_str(),      sizeof(mqtt_server));
  strlcpy(mqtt_port,           server.arg("mp").c_str(),      sizeof(mqtt_port));
  strlcpy(mqtt_user,           server.arg("mu").c_str(),      sizeof(mqtt_user));
  strlcpy(mqtt_pass,           server.arg("mwp").c_str(),     sizeof(mqtt_pass));
  
  long_press_ms   = server.arg("t_long").toInt();
  double_click_ms = server.arg("t_double").toInt();

  // bouton 1
  strlcpy(btn1_name,           server.arg("b1_name").c_str(), sizeof(btn1_name));
  strlcpy(btn1_color,          server.arg("b1_color").c_str(),sizeof(btn1_color));
  strlcpy(btn1_mode,           server.arg("b1_mode").c_str(), sizeof(btn1_mode));
  strlcpy(btn1_topic,          server.arg("b1_t").c_str(),    sizeof(btn1_topic));
  strlcpy(btn1_payload,        server.arg("b1_p").c_str(),    sizeof(btn1_payload));
  strlcpy(btn1_url,            server.arg("b1_url").c_str(),  sizeof(btn1_url));
  strlcpy(btn1_long_topic,     server.arg("b1_lt").c_str(),   sizeof(btn1_long_topic));
  strlcpy(btn1_long_payload,   server.arg("b1_lp").c_str(),   sizeof(btn1_long_payload));
  strlcpy(btn1_long_url,       server.arg("b1_lurl").c_str(), sizeof(btn1_long_url));
  strlcpy(btn1_long_mode,      server.arg("b1_lm").c_str(),   sizeof(btn1_long_mode));
  strlcpy(btn1_long_act,       server.arg("b1_lact").c_str(), sizeof(btn1_long_act));
  strlcpy(btn1_click_mode,     server.arg("b1_dmode").c_str(),sizeof(btn1_click_mode));
  strlcpy(btn1_click_topic,    server.arg("b1_dt").c_str(),   sizeof(btn1_click_topic));
  strlcpy(btn1_click_payload,  server.arg("b1_dp").c_str(),   sizeof(btn1_click_payload));
  strlcpy(btn1_click_url,      server.arg("b1_durl").c_str(), sizeof(btn1_click_url));

  // bouton 2
  strlcpy(btn2_name,           server.arg("b2_name").c_str(), sizeof(btn2_name));
  strlcpy(btn2_color,          server.arg("b2_color").c_str(),sizeof(btn2_color));
  strlcpy(btn2_mode,           server.arg("b2_mode").c_str(), sizeof(btn2_mode));
  strlcpy(btn2_topic,          server.arg("b2_t").c_str(),    sizeof(btn2_topic));
  strlcpy(btn2_payload,        server.arg("b2_p").c_str(),    sizeof(btn2_payload));
  strlcpy(btn2_url,            server.arg("b2_url").c_str(),  sizeof(btn2_url));
  strlcpy(btn2_long_topic,     server.arg("b2_lt").c_str(),   sizeof(btn2_long_topic));
  strlcpy(btn2_long_payload,   server.arg("b2_lp").c_str(),   sizeof(btn2_long_payload));
  strlcpy(btn2_long_url,       server.arg("b2_lurl").c_str(), sizeof(btn2_long_url));
  strlcpy(btn2_long_mode,      server.arg("b2_lm").c_str(),   sizeof(btn2_long_mode));
  strlcpy(btn2_long_act,       server.arg("b2_lact").c_str(), sizeof(btn2_long_act));
  strlcpy(btn2_click_mode,     server.arg("b2_dmode").c_str(),sizeof(btn2_click_mode));
  strlcpy(btn2_click_topic,    server.arg("b2_dt").c_str(),   sizeof(btn2_click_topic));
  strlcpy(btn2_click_payload,  server.arg("b2_dp").c_str(),   sizeof(btn2_click_payload));
  strlcpy(btn2_click_url,      server.arg("b2_durl").c_str(), sizeof(btn2_click_url));

  // bouton 3
  strlcpy(btn3_name,           server.arg("b3_name").c_str(), sizeof(btn3_name));
  strlcpy(btn3_color,          server.arg("b3_color").c_str(),sizeof(btn3_color));
  strlcpy(btn3_mode,           server.arg("b3_mode").c_str(), sizeof(btn3_mode));
  strlcpy(btn3_topic,          server.arg("b3_t").c_str(),    sizeof(btn3_topic));
  strlcpy(btn3_payload,        server.arg("b3_p").c_str(),    sizeof(btn3_payload));
  strlcpy(btn3_url,            server.arg("b3_url").c_str(),  sizeof(btn3_url));
  strlcpy(btn3_long_topic,     server.arg("b3_lt").c_str(),   sizeof(btn3_long_topic));
  strlcpy(btn3_long_payload,   server.arg("b3_lp").c_str(),   sizeof(btn3_long_payload));
  strlcpy(btn3_long_url,       server.arg("b3_lurl").c_str(), sizeof(btn3_long_url));
  strlcpy(btn3_long_mode,      server.arg("b3_lm").c_str(),   sizeof(btn3_long_mode));
  strlcpy(btn3_long_act,       server.arg("b3_lact").c_str(), sizeof(btn3_long_act));
  strlcpy(btn3_click_mode,     server.arg("b3_dmode").c_str(),sizeof(btn3_click_mode));
  strlcpy(btn3_click_topic,    server.arg("b3_dt").c_str(),   sizeof(btn3_click_topic));
  strlcpy(btn3_click_payload,  server.arg("b3_dp").c_str(),   sizeof(btn3_click_payload));
  strlcpy(btn3_click_url,      server.arg("b3_durl").c_str(), sizeof(btn3_click_url));

  save_config();

  String html = "<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<style>body{background:#0f172a;color:#f8fafc;font-family:sans-serif;text-align:center;padding:50px 20px;}"
                ".msg{background:#1e293b;padding:30px;border-radius:12px;display:inline-block;border:1px solid #334155;}"
                "a{color:#38bdf8;text-decoration:none;font-weight:bold;}</style></head>"
                "<body><div class='msg'><h2>Configuration enregistr&eacute;e !</h2>"
                "<p>L'appareil va red&eacute;marrer pour appliquer les modifications.</p>"
                "<p><a href='/'>Retour &agrave; l'accueil</a></p></div>"
                "<script>setTimeout(function(){window.location.href='/';},3000);</script></body></html>";
  server.send(200, "text/html", html);
  delay(500);
  ESP.restart();
}

void handle_trigger() {
  int id = server.arg("id").toInt();
  String type = server.arg("type");
  bool ok = false;

  if (id == 1) {
    if (type == "court")       ok = execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "web-btn1-court");
    else if (type == "double") ok = execute_action(btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url, "web-btn1-double");
    else if (type == "long")   ok = execute_action(btn1_long_act, btn1_long_topic, btn1_long_payload, btn1_long_url, "web-btn1-long");
  } else if (id == 2) {
    if (type == "court")       ok = execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "web-btn2-court");
    else if (type == "double") ok = execute_action(btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url, "web-btn2-double");
    else if (type == "long")   ok = execute_action(btn2_long_act, btn2_long_topic, btn2_long_payload, btn2_long_url, "web-btn2-long");
  } else if (id == 3) {
    if (type == "court")       ok = execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "web-btn3-court");
    else if (type == "double") ok = execute_action(btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url, "web-btn3-double");
    else if (type == "long")   ok = execute_action(btn3_long_act, btn3_long_topic, btn3_long_payload, btn3_long_url, "web-btn3-long");
  }

  server.send(200, "text/plain", ok ? "OK" : "ERR");
}

// export et extraction forcée en téléchargement de fichier
void handle_backup() {
  if (LittleFS.exists("/config.json")) {
    File f = LittleFS.open("/config.json", "r");
    // force le navigateur à ouvrir la fenêtre d'enregistrement de fichier
    server.sendHeader("Content-Disposition", "attachment; filename=config.json");
    server.streamFile(f, "application/json");
    f.close();
  } else {
    server.send(404, "text/plain", "Fichier config.json introuvable.");
  }
}

// importation et ecrasement du fichier config.json
void handle_restore() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[restore] début réception : %s\n", upload.filename.c_str());
    // ouverture temporaire du fichier cible en écriture brute
    File f = LittleFS.open("/config.json", "w");
    if (!f) Serial.println("[restore] erreur impossible d'ouvrir config.json");
    f.close();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    File f = LittleFS.open("/config.json", "a");
    if (f) {
      f.write(upload.buf, upload.currentSize);
      f.close();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    Serial.println("[restore] transfert terminé.");
    String html = "<!doctype html><html><head><meta charset='utf-8'>"
                  "<style>body{background:#0f172a;color:#f8fafc;font-family:sans-serif;text-align:center;padding:50px 20px;}</style></head>"
                  "<body><h2>Restauration réussie !</h2><p>Redémarrage matériel en cours...</p>"
                  "<script>setTimeout(function(){window.location.href='/';},4000);</script></body></html>";
    server.send(200, "text/html", html);
    delay(500);
    ESP.restart();
  }
}

// ================================================================== loop wifi / mqtt

void handle_network() {
  if (is_ap_mode) return;

  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    net_state = net_wifi_ko;
    if (now - last_wifi_attempt >= 10000 || last_wifi_attempt == 0) {
      last_wifi_attempt = now;
      Serial.println("[wifi] reconnexion en cours...");
      WiFi.begin(wifi_ssid, wifi_pass);
      reconnect_count++;
    }
    return;
  }

  if (mqtt_enabled) {
    if (!mqtt_client.connected()) {
      net_state = net_mqtt_ko;
      if (now - last_mqtt_attempt >= 8000 || last_mqtt_attempt == 0) {
        last_mqtt_attempt = now;
        Serial.println("[mqtt] reconnexion au broker...");
        String clientId = "ESP8266Client-" + String(random(0xffff), HEX);
        bool connected = false;
        if (strlen(mqtt_user) > 0) {
          connected = mqtt_client.connect(clientId.c_str(), mqtt_user, mqtt_pass);
        } else {
          connected = mqtt_client.connect(clientId.c_str());
        }
        if (connected) {
          Serial.println("[mqtt] connecte");
          net_state = net_ok;
        } else {
          Serial.print("[mqtt] echec, rc=");
          Serial.println(mqtt_client.state());
        }
      }
    } else {
      net_state = net_ok;
      mqtt_client.loop();
    }
  } else {
    net_state = net_ok;
  }
}

// ================================================================== setup() et loop()

void setup() {
  Serial.begin(115200);
  Serial.println("\n[boot] démarrage...");

  pinMode(btn1_pin, INPUT);
  pinMode(btn2_pin, INPUT);
  pinMode(btn3_pin, INPUT);
  pinMode(pin_led, OUTPUT);
  digitalWrite(pin_led, HIGH);

  load_config();

  WiFi.mode(WIFI_STA);
  if (strlen(wifi_ssid) > 0) {
    WiFi.begin(wifi_ssid, wifi_pass);
    Serial.println("[wifi] connexion demandée vers " + String(wifi_ssid));
  } else {
    Serial.println("[wifi] pas de ssid configuré, bascule en mode AP temporaire");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP-Telecommande-Config", "12345678");
    is_ap_mode = true;
    Serial.print("[wifi] mode AP actif. IP : ");
    Serial.println(WiFi.softAPIP());
  }

  if (mqtt_enabled && strlen(mqtt_server) > 0) {
    mqtt_client.setServer(mqtt_server, atoi(mqtt_port));
  }

  server.on("/", handle_root);
  server.on("/setup", handle_setup);
  server.on("/save", HTTP_POST, handle_save);
  server.on("/trigger", handle_trigger);
  server.on("/backup", handle_backup);
  server.on("/restore", HTTP_POST, handle_root, handle_restore); // traitement multipart pour la restauration
  httpupdater.setup(&server, "/update");
  server.begin();
  Serial.println("[web] serveur http démarré");
}

void loop() {
  server.handleClient();
  handle_network();
  update_led();

  unsigned long now = millis();

  // --- BOUTON 1 (D1)
  bool raw1 = digitalRead(btn1_pin);
  if (raw1 != last_raw_btn1_state) { last_debounce_time1 = now; }
  last_raw_btn1_state = raw1;
  if ((now - last_debounce_time1) >= debounce_delay) {
    if (raw1 != debounced_btn1_state) {
      debounced_btn1_state = raw1;
      if (debounced_btn1_state == HIGH) {
        pressed1 = true; long_fired1 = false; press_start1 = now;
        if (strcmp(btn1_long_mode, "both") == 0 && click_count1 == 0) {
          Serial.println("[action] d1 court immédiat");
          execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court");
        }
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
    long_fired1 = true; 
    if (strcmp(btn1_long_mode, "long") == 0) { click_count1 = 0; }
    Serial.println("[action] d1 long");
    execute_action(btn1_long_act, btn1_long_topic, btn1_long_payload, btn1_long_url, "btn1-long");
  }
  if (!pressed1 && click_count1 > 0 && (now - release_time1 >= double_click_ms)) {
    if (click_count1 >= 2) {
      if (strlen(btn1_click_topic) > 0 || strlen(btn1_click_url) > 0) {
        Serial.println("[action] d1 double");
        execute_action(btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url, "btn1-double");
      } else if (strcmp(btn1_long_mode, "long") == 0) {
        Serial.println("[action] d1 double ignoré, exécution court x2");
        execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court");
        execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court");
      }
    } else if (click_count1 == 1 && strcmp(btn1_long_mode, "long") == 0) {
      Serial.println("[action] d1 court différé");
      execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court");
    }
    click_count1 = 0;
  }

  // --- BOUTON 2 (D2)
  bool raw2 = digitalRead(btn2_pin);
  if (raw2 != last_raw_btn2_state) { last_debounce_time2 = now; }
  last_raw_btn2_state = raw2;
  if ((now - last_debounce_time2) >= debounce_delay) {
    if (raw2 != debounced_btn2_state) {
      debounced_btn2_state = raw2;
      if (debounced_btn2_state == HIGH) {
        pressed2 = true; long_fired2 = false; press_start2 = now;
        if (strcmp(btn2_long_mode, "both") == 0 && click_count2 == 0) {
          Serial.println("[action] d2 court immédiat");
          execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court");
        }
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
    long_fired2 = true;
    if (strcmp(btn2_long_mode, "long") == 0) { click_count2 = 0; }
    Serial.println("[action] d2 long");
    execute_action(btn2_long_act, btn2_long_topic, btn2_long_payload, btn2_long_url, "btn2-long");
  }
  if (!pressed2 && click_count2 > 0 && (now - release_time2 >= double_click_ms)) {
    if (click_count2 >= 2) {
      if (strlen(btn2_click_topic) > 0 || strlen(btn2_click_url) > 0) {
        Serial.println("[action] d2 double");
        execute_action(btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url, "btn2-double");
      } else if (strcmp(btn2_long_mode, "long") == 0) {
        Serial.println("[action] d2 double ignoré, exécution court x2");
        execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court");
        execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court");
      }
    } else if (click_count2 == 1 && strcmp(btn2_long_mode, "long") == 0) {
      Serial.println("[action] d2 court différé");
      execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court");
    }
    click_count2 = 0;
  }

  // --- BOUTON 3 (D5)
  bool raw3 = digitalRead(btn3_pin);
  if (raw3 != last_raw_btn3_state) { last_debounce_time3 = now; }
  last_raw_btn3_state = raw3;
  if ((now - last_debounce_time3) >= debounce_delay) {
    if (raw3 != debounced_btn3_state) {
      debounced_btn3_state = raw3;
      if (debounced_btn3_state == HIGH) {
        pressed3 = true; long_fired3 = false; press_start3 = now;
        if (strcmp(btn3_long_mode, "both") == 0 && click_count3 == 0) {
          Serial.println("[action] d5 court immédiat");
          execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court");
        }
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
    long_fired3 = true;
    if (strcmp(btn3_long_mode, "long") == 0) { click_count3 = 0; }
    Serial.println("[action] d5 long");
    execute_action(btn3_long_act, btn3_long_topic, btn3_long_payload, btn3_long_url, "btn3-long");
  }
  if (!pressed3 && click_count3 > 0 && (now - release_time3 >= double_click_ms)) {
    if (click_count3 >= 2) {
      if (strlen(btn3_click_topic) > 0 || strlen(btn3_click_url) > 0) {
        Serial.println("[action] d5 double");
        execute_action(btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url, "btn3-double");
      } else if (strcmp(btn3_long_mode, "long") == 0) {
        Serial.println("[action] d5 double ignoré, exécution court x2");
        execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court");
        execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court");
      }
    } else if (click_count3 == 1 && strcmp(btn3_long_mode, "long") == 0) {
      Serial.println("[action] d5 court différé");
      execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court");
    }
    click_count3 = 0;
  }
}
