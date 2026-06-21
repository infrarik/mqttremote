#
# This code is for TTP223 butons only.
#
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <PubSubClient.h>

// ------------------------------------------------------------------ broches
// GPIO boutons configurables depuis l'interface web (defauts D1/D2/D5)
int btn1_pin = 5;
int btn2_pin = 4;
int btn3_pin = 14;
#define pin_led 2    // d4 = gpio2, led onboard nodemcu (active low)

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

    btn1_pin = json["btn1_gpio"] | 5;
    btn2_pin = json["btn2_gpio"] | 4;
    btn3_pin = json["btn3_gpio"] | 14;
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

  json["btn1_gpio"] = btn1_pin;
  json["btn2_gpio"] = btn2_pin;
  json["btn3_gpio"] = btn3_pin;
  
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

// helper select GPIO Wemos D1 mini D0-D8
String gpio_select(const char* name, int cur) {
  const int   gpios[]  = {16, 5, 4, 0, 2, 14, 12, 13, 15};
  const char* labels[] = {"D0-GPIO16","D1-GPIO5","D2-GPIO4","D3-GPIO0","D4-GPIO2","D5-GPIO14","D6-GPIO12","D7-GPIO13","D8-GPIO15"};
  String s = "<select name='" + String(name) + "'>";
  for (int i = 0; i < 9; i++) {
    s += "<option value='" + String(gpios[i]) + "'" + (gpios[i] == cur ? " selected" : "") + ">" + String(labels[i]) + "</option>";
  }
  s += "</select>";
  return s;
}

String btn_card(int n, int gpio_val, const char* name, const char* color, 
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
  s += color_option("green",  "vert fluo",  color);
  s += color_option("white",  "blanc épuré", color);
  s += color_option("red",    "rouge alerte",color);
  s += color_option("blue",   "bleu tech",  color);
  s += color_option("orange", "orange vif", color);
  s += "</select></div>";

  s += "<div class='row'><label>broche gpio</label>" + gpio_select(("b" + p + "_gpio").c_str(), gpio_val) + "</div>";

  s += "<div class='long-hdr' style='color:#10b981; border-top:none; padding-top:0;'>&#9642; action clic court</div>";
  s += "<div class='row'><label>type de protocole</label><select name='b" + p + "_mode' onchange='tglFields(this," + p + ",\"c\")'>";
  s += act_option("mqtt", "mqtt uniquement", mode);
  s += act_option("http", "http post uniquement", mode);
  s += act_option("both", "mqtt + http post simultan&eacute;", mode);
  s += "</select></div>";
  s += "<div id='b" + p + "_c_mqtt_fields' style='display:" + String(strcmp(mode,"http")==0?"none":"block") + ";'>";
  s += "<div class='grid2'><div class='row'><label>topic court</label><input type='text' name='b" + p + "_t' value='" + String(topic) + "'></div>";
  s += "<div class='row'><label>payload court</label><input type='text' name='b" + p + "_p' value='" + String(payload) + "'></div></div>";
  s += "</div>";
  s += "<div id='b" + p + "_c_http_fields' style='display:" + String(strcmp(mode,"mqtt")==0?"none":"block") + ";'>";
  s += "<div class='row'><label>url cible courte</label><input type='text' name='b" + p + "_url' value='" + String(url) + "' placeholder='http://...'></div>";
  s += "</div>";


  s += "<div class='long-hdr' style='color:#3b82f6; border-top-color:#374151;'>&#9898;&#9898; action double clic</div>";
  s += "<div class='row'><label>type de protocole</label><select name='b" + p + "_dmode' onchange='tglFields(this," + p + ",\"d\")'>";
  s += act_option("mqtt", "mqtt uniquement", dmode);
  s += act_option("http", "http post uniquement", dmode);
  s += act_option("both", "mqtt + http post simultan&eacute;", dmode);
  s += "</select></div>";
  s += "<div id='b" + p + "_d_mqtt_fields' style='display:" + String(strcmp(dmode,"http")==0?"none":"block") + ";'>";
  s += "<div class='grid2'><div class='row'><label>topic double clic</label><input type='text' name='b" + p + "_dt' value='" + String(dtopic) + "' placeholder='vide = d&eacute;sactiv&eacute;'></div>";
  s += "<div class='row'><label>payload double</label><input type='text' name='b" + p + "_dp' value='" + String(dpayload) + "'></div></div>";
  s += "</div>";
  s += "<div id='b" + p + "_d_http_fields' style='display:" + String(strcmp(dmode,"mqtt")==0?"none":"block") + ";'>";
  s += "<div class='row'><label>url cible double clic</label><input type='text' name='b" + p + "_durl' value='" + String(durl) + "' placeholder='http://...'></div>";
  s += "</div>";


  s += "<div class='long-hdr' style='color:#ec4899; border-top-color:#374151;'>&#128336; action appui long</div>";
  s += "<div class='row'><label>mode appui long</label><select name='b" + p + "_lm'>";
  s += "<option value='both'" + String(strcmp(lmode,"both")==0?" selected":"") + ">cumuler les deux actions (court puis long)</option>";
  s += "<option value='long'" + String(strcmp(lmode,"long")==0?" selected":"") + ">exclure le clic court (long seul)</option>";
  s += "</select></div>";
  s += "<div class='row'><label>type de protocole</label><select name='b" + p + "_lact' onchange='tglFields(this," + p + ",\"l\")'>";
  s += act_option("mqtt", "mqtt uniquement", lact);
  s += act_option("http", "http post uniquement", lact);
  s += act_option("both", "mqtt + http post simultan&eacute;", lact);
  s += "</select></div>";
  s += "<div id='b" + p + "_l_mqtt_fields' style='display:" + String(strcmp(lact,"http")==0?"none":"block") + ";'>";
  s += "<div class='grid2'><div class='row'><label>topic long</label><input type='text' name='b" + p + "_lt' value='" + String(ltopic) + "' placeholder='vide = d&eacute;sactiv&eacute;'></div>";
  s += "<div class='row'><label>payload long</label><input type='text' name='b" + p + "_lp' value='" + String(lpayload) + "'></div></div>";
  s += "</div>";
  s += "<div id='b" + p + "_l_http_fields' style='display:" + String(strcmp(lact,"mqtt")==0?"none":"block") + ";'>";
  s += "<div class='row'><label>url cible longue</label><input type='text' name='b" + p + "_lurl' value='" + String(lurl) + "' placeholder='http://...'></div>";
  s += "</div>";

  s += "</div>";
  return s;
}

void handleBackup() {
  if (!LittleFS.begin()) { server.send(500, "text/plain", "fs error"); return; }
  File f = LittleFS.open("/config.json", "r");
  if (!f) { server.send(500, "text/plain", "erreur lecture config"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=config.json");
  server.streamFile(f, "application/json");
  f.close();
}

void handleRestore() {
  if (!server.hasArg("plain")) { server.send(400, "text/plain", "body manquant"); return; }
  String body = server.arg("plain");
  StaticJsonDocument<4096> json;
  if (deserializeJson(json, body) != DeserializationError::Ok) {
    server.send(400, "text/plain", "json invalide");
    return;
  }
  if (!LittleFS.begin()) { server.send(500, "text/plain", "fs error"); return; }
  File f = LittleFS.open("/config.json", "w");
  if (!f) { server.send(500, "text/plain", "erreur ecriture"); return; }
  f.print(body);
  f.close();
  server.send(200, "text/plain", "ok");
  delay(500);
  ESP.restart();
}


void handle_setup() {
  // --- 1. En-tête HTTP avec longueur inconnue (streaming) ---
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", ""); // envoie uniquement l'en-tête

  // --- 2. Début de la page (balises head, style, body, formulaire) ---
  server.sendContent(
    "<!doctype html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>configuration</title>"
    "<style>"
    "body{margin:0;padding:0 0 40px;background:#111827;font-family:-apple-system,blinkmacsystemfont,'segoe ui',sans-serif;color:#f3f4f6;}"
    ".topbar{background:#1f2937;border-bottom:1px solid #374151;padding:14px 16px;display:flex;align-items:center;justify-content:space-between;position:sticky;top:0;z-index:10;}"
    ".topbar-title{font-size:15px;font-weight:600;letter-spacing:0.02em;}"
    ".back{font-size:13px;color:#3b82f6;text-decoration:none;font-weight:500;}"
    ".section{background:#1f2937;border-radius:14px;margin:14px 12px 0;padding:16px;border:1px solid #374151;}"
    ".sec-hdr{font-size:11px;font-weight:700;letter-spacing:0.08em;text-transform:uppercase;margin-bottom:14px;padding-bottom:8px;border-bottom:1px solid #374151;}"
    ".sec-net{color:#3b82f6;}.sec-mqtt{color:#f59e0b;}.sec-btns{color:#10b981;}.sec-rob{color:#a855f7;}.sec-ota{color:#ec4899;}"
    ".row{margin-bottom:12px;display:flex;flex-direction:column;gap:5px;}"
    "label{font-size:12px;color:#9ca3af;font-weight:500;}"
    "input[type='text'],input[type='password'],select{padding:10px 12px;border:1px solid #4b5563;border-radius:8px;font-size:14px;background:#111827;color:#f3f4f6;box-sizing:border-box;width:100%;transition:border-color 0.15s;}"
    "input[type='text']:focus,input[type='password']:focus,select:focus{border-color:#3b82f6;outline:none;}"
    "input[type='checkbox']{width:18px;height:18px;margin:0;cursor:pointer;}"
    ".chk-row{flex-direction:row;align-items:center;gap:10px;margin:6px 0 14px;}"
    ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-bottom:4px;}"
    ".btn-card{background:#111827;border:1px solid #374151;border-radius:10px;padding:14px;margin-bottom:16px;}"
    ".btn-card-hdr{font-size:14px;font-weight:600;margin-bottom:12px;display:flex;align-items:center;gap:8px;color:#f3f4f6;border-bottom:1px solid #374151;padding-bottom:8px;}"
    ".clr-dot{width:10px;height:10px;border-radius:50%;border:1px solid rgba(255,255,255,0.1);}"
    ".long-hdr{font-size:11px;font-weight:700;text-transform:uppercase;margin:16px 0 12px;padding-top:10px;border-top:1px solid #374151;letter-spacing:0.05em;}"
    ".tst-btn{width:100%;padding:8px 0;border:none;border-radius:6px;color:#fff;font-size:12px;font-weight:600;cursor:pointer;margin:4px 0 12px 0;text-transform:uppercase;letter-spacing:0.02em;opacity:0.9;transition:opacity 0.15s;}"
    ".tst-btn:hover{opacity:1;}"
    ".submit-btn{background:#3b82f6;color:#fff;border:none;padding:14px;border-radius:10px;font-size:15px;font-weight:600;cursor:pointer;width:100%;margin-top:10px;box-shadow:0 4px 12px rgba(59,130,246,0.25);transition:background 0.2s;}"
    ".submit-btn:active{background:#2563eb;}"
    ".opt{font-size:10px;color:#6b7280;font-weight:normal;font-style:italic;}"
    ".wifi-item{font-size:13px;padding:8px 10px;border-bottom:1px solid #374151;cursor:pointer;color:#3b82f6;font-weight:500;transition:background 0.15s;border-radius:6px;}"
    ".wifi-item:hover{background:#111827;}"
    ".wifi-item:last-child{border:none;}"
    "</style></head><body>"
    "<form method='POST' action='/save'>"
    "<div class='topbar'><span class='topbar-title'>Configuration Param&egrave;tres</span><a href='/' class='back'>&larr; Annuler</a></div>"
  );

  // --- 3. Section WiFi (avec scan des réseaux) ---
  int n = WiFi.scanNetworks();
  server.sendContent(
    "<div class='section'><div class='sec-hdr sec-net'>&#128246; liaison sans-fil wifi</div>"
    "<div class='row'><label>nom du r&eacute;seau local (ssid)</label><input type='text' id='ssid_in' name='ssid' value='"
    + String(wifi_ssid) + "'></div>"
    "<div class='row'><label>cl&eacute; de s&eacute;curit&eacute; wpa</label><input type='password' name='password' value='"
    + String(wifi_pass) + "'></div>"
  );

  if (n > 0) {
    server.sendContent("<div class='row'><label style='margin-bottom:4px;'>r&eacute;seaux sans-fil scann&eacute;s (cliquer pour s&eacute;lectionner)</label>");
    for (int i = 0; i < n; ++i) {
      server.sendContent("<div class='wifi-item' onclick='document.getElementById(\"ssid_in\").value=this.innerText'>" + WiFi.SSID(i) + "</div>");
    }
    server.sendContent("</div>");
  }
  server.sendContent("</div>");

  // --- 4. Section MQTT ---
  server.sendContent(
    "<div class='section'><div class='sec-hdr sec-mqtt'>&#128172; courtier broker mqtt</div>"
    "<div class='row chk-row'><input type='checkbox' name='mqtt_en' value='1'"
    + String(mqtt_enabled ? " checked" : "")
    + " id='mq_en' onchange='tglMqtt()'><label for='mq_en'>activer l'infrastructure mqtt</label></div>"
    "<div id='mqtt_fields' style='display:" + String(mqtt_enabled ? "block" : "none") + ";'>"
    "<div class='grid2'><div class='row'><label>adresse ip du serveur</label><input type='text' name='mq_srv' value='"
    + String(mqtt_server) + "'></div>"
    "<div class='row'><label>port tcp</label><input type='text' name='mq_prt' value='"
    + String(mqtt_port) + "'></div></div>"
    "<div class='grid2'><div class='row'><label>identifiant utilisateur <span class='opt'>(optionnel)</span></label><input type='text' name='mq_usr' value='"
    + String(mqtt_user) + "'></div>"
    "<div class='row'><label>mot de passe de connexion <span class='opt'>(optionnel)</span></label><input type='password' name='mq_pwd' value='"
    + String(mqtt_pass) + "'></div></div>"
    "</div></div>"
  );

  // --- 5. Section des 3 boutons (appel de btn_card pour chaque) ---
  server.sendContent(
    "<div class='section'><div class='sec-hdr sec-btns'>&#9899; param&eacute;trage des boutons tactiles</div>"
  );
  // Bouton 1
  server.sendContent(btn_card(1, btn1_pin, btn1_name, btn1_color,
                              btn1_mode, btn1_topic, btn1_payload, btn1_url,
                              btn1_long_topic, btn1_long_payload, btn1_long_url, btn1_long_mode, btn1_long_act,
                              btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url));
  // Bouton 2
  server.sendContent(btn_card(2, btn2_pin, btn2_name, btn2_color,
                              btn2_mode, btn2_topic, btn2_payload, btn2_url,
                              btn2_long_topic, btn2_long_payload, btn2_long_url, btn2_long_mode, btn2_long_act,
                              btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url));
  // Bouton 3
  server.sendContent(btn_card(3, btn3_pin, btn3_name, btn3_color,
                              btn3_mode, btn3_topic, btn3_payload, btn3_url,
                              btn3_long_topic, btn3_long_payload, btn3_long_url, btn3_long_mode, btn3_long_act,
                              btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url));
  server.sendContent("</div>");

  // --- 6. Filtres temporels ---
  server.sendContent(
    "<div class='section'><div class='sec-hdr sec-rob'>&#128336; filtres temporels anti-rebond</div>"
    "<div class='grid2'><div class='row'><label>seuil appui long (ms)</label><input type='text' name='t_long' value='"
    + String(long_press_ms) + "'></div>"
    "<div class='row'><label>fen&ecirc;tre double clic (ms)</label><input type='text' name='t_double' value='"
    + String(double_click_ms) + "'></div></div>"
    "</div>"
  );

  // --- 7. OTA ---
  server.sendContent(
    "<div class='section'>"
    "<div class='sec-hdr sec-ota'>&#128225; t&eacute;l&eacute;chargement firmware ota</div>"
    "<div class='row'>"
    "<a href='/update' style='font-size:13px; color:#a855f7; text-decoration:none; font-weight:600;'>&xrArr; acc&eacute;der &agrave; la page flash ota</a>"
    "</div>"
    "</div>"
  );

  // --- 8. Backup / Restore ---
  server.sendContent(
    "<div class='section'>"
    "<div class='sec-hdr' style='color:#06b6d4;'>&#128190; sauvegarde &amp; restauration</div>"
    "<div class='row'><label>exporter la configuration actuelle</label>"
    "<a href='/backup' style='display:inline-block;margin-top:4px;padding:8px 14px;background:#164e63;color:#67e8f9;border:1px solid #0e7490;border-radius:7px;font-size:13px;font-weight:600;text-decoration:none;'>&#8659; t&eacute;l&eacute;charger config.json</a></div>"
    "<div class='row'><label>importer une configuration</label>"
    "<div id='restore-area'>"
    "<div style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;'>"
    "<label for='restore-file' style='display:inline-flex;align-items:center;gap:6px;padding:8px 13px;background:#164e63;border:1px solid #0e7490;border-radius:7px;cursor:pointer;font-size:13px;color:#67e8f9;font-weight:600;'>&#128193; choisir config.json</label>"
    "<input type='file' id='restore-file' accept='.json,application/json' style='display:none;'>"
    "<span id='restore-fname' style='font-size:12px;color:#9ca3af;'>aucun fichier</span>"
    "</div>"
    "<button type='button' onclick='doRestore()' style='margin-top:8px;width:100%;padding:9px;background:#0e7490;color:#fff;border:none;border-radius:7px;font-size:13px;font-weight:600;cursor:pointer;'>&#8657; restaurer et red&eacute;marrer</button>"
    "</div></div></div>"
  );

  // --- 9. Bouton de soumission et fermeture du formulaire ---
  server.sendContent(
    "<div style='padding:0 12px 30px;'><button type='submit' class='submit-btn'>Appliquer et m&eacute;moriser la configuration</button></div>"
    "</form>"
  );

  // --- 10. Scripts JavaScript ---
  server.sendContent(
    "<script>"
    "function tglMqtt(){document.getElementById('mqtt_fields').style.display=document.getElementById('mq_en').checked?'block':'none';}"
    "function upd(sel,id){"
    "  var c='#f3f4f6';"
    "  if(sel.value=='green')c='#10b981';if(sel.value=='red')c='#ef4444';if(sel.value=='blue')c='#3b82f6';if(sel.value=='orange')c='#f59e0b';"
    "  document.getElementById(id).style.background=c;"
    "}"
    "function tglFields(sel,p,type){"
    "  var m=sel.value;"
    "  document.getElementById('b'+p+'_'+type+'_mqtt_fields').style.display=(m=='http')?'none':'block';"
    "  document.getElementById('b'+p+'_'+type+'_http_fields').style.display=(m=='mqtt')?'none':'block';"
    "}"
    "document.getElementById('restore-file').addEventListener('change',function(){"
    "  document.getElementById('restore-fname').textContent=this.files[0]?this.files[0].name:'aucun fichier';"
    "});"
    "function doRestore(){"
    "  var fi=document.getElementById('restore-file');"
    "  if(!fi||!fi.files||!fi.files[0]){alert('selectionnez un fichier config.json');return;}"
    "  var reader=new FileReader();"
    "  reader.onload=function(e){"
    "    var x=new XMLHttpRequest();x.open('POST','/restore',true);"
    "    x.setRequestHeader('Content-Type','application/json');"
    "    x.onload=function(){"
    "      if(x.status==200){"
    "        document.getElementById('restore-area').innerHTML='<div style=\"color:#34d399;font-size:14px;font-weight:600;padding:8px 0;\">restauration reussie, redemarre...</div>';"
    "        setTimeout(function(){window.location.href='/';},4000);"
    "      }else{alert('erreur restauration : '+x.responseText);}"
    "    };x.send(e.target.result);"
    "  };reader.readAsText(fi.files[0]);"
    "}"
    "</script>"
    "</body></html>"
  );

  // --- 11. Fermeture de la réponse ---
  server.sendContent("");  // termine le streaming
}
void handle_save() {
  strlcpy(wifi_ssid, server.arg("ssid").c_str(), sizeof(wifi_ssid));
  strlcpy(wifi_pass, server.arg("password").c_str(), sizeof(wifi_pass));
  
  mqtt_enabled = (server.arg("mqtt_en") == "1");
  strlcpy(mqtt_server, server.arg("mq_srv").c_str(), sizeof(mqtt_server));
  strlcpy(mqtt_port,   server.arg("mq_prt").c_str(), sizeof(mqtt_port));
  strlcpy(mqtt_user,   server.arg("mq_usr").c_str(), sizeof(mqtt_user));
  strlcpy(mqtt_pass,   server.arg("mq_pwd").c_str(), sizeof(mqtt_pass));

  // bouton 1
  strlcpy(btn1_name,          server.arg("b1_name").c_str(),  sizeof(btn1_name));
  strlcpy(btn1_color,         server.arg("b1_color").c_str(), sizeof(btn1_color));
  strlcpy(btn1_mode,          server.arg("b1_mode").c_str(),  sizeof(btn1_mode));
  strlcpy(btn1_topic,         server.arg("b1_t").c_str(),     sizeof(btn1_topic));
  strlcpy(btn1_payload,       server.arg("b1_p").c_str(),     sizeof(btn1_payload));
  strlcpy(btn1_url,           server.arg("b1_url").c_str(),   sizeof(btn1_url));
  strlcpy(btn1_long_mode,     server.arg("b1_lm").c_str(),    sizeof(btn1_long_mode));
  strlcpy(btn1_long_act,      server.arg("b1_lact").c_str(),  sizeof(btn1_long_act));
  strlcpy(btn1_long_topic,    server.arg("b1_lt").c_str(),    sizeof(btn1_long_topic));
  strlcpy(btn1_long_payload,  server.arg("b1_lp").c_str(),    sizeof(btn1_long_payload));
  strlcpy(btn1_long_url,      server.arg("b1_lurl").c_str(),  sizeof(btn1_long_url));
  strlcpy(btn1_click_mode,    server.arg("b1_dmode").c_str(), sizeof(btn1_click_mode));
  strlcpy(btn1_click_topic,   server.arg("b1_dt").c_str(),    sizeof(btn1_click_topic));
  strlcpy(btn1_click_payload, server.arg("b1_dp").c_str(),    sizeof(btn1_click_payload));
  strlcpy(btn1_click_url,     server.arg("b1_durl").c_str(),  sizeof(btn1_click_url));

  // bouton 2
  strlcpy(btn2_name,          server.arg("b2_name").c_str(),  sizeof(btn2_name));
  strlcpy(btn2_color,         server.arg("b2_color").c_str(), sizeof(btn2_color));
  strlcpy(btn2_mode,          server.arg("b2_mode").c_str(),  sizeof(btn2_mode));
  strlcpy(btn2_topic,         server.arg("b2_t").c_str(),     sizeof(btn2_topic));
  strlcpy(btn2_payload,       server.arg("b2_p").c_str(),     sizeof(btn2_payload));
  strlcpy(btn2_url,           server.arg("b2_url").c_str(),   sizeof(btn2_url));
  strlcpy(btn2_long_mode,     server.arg("b2_lm").c_str(),    sizeof(btn2_long_mode));
  strlcpy(btn2_long_act,      server.arg("b2_lact").c_str(),  sizeof(btn2_long_act));
  strlcpy(btn2_long_topic,    server.arg("b2_lt").c_str(),    sizeof(btn2_long_topic));
  strlcpy(btn2_long_payload,  server.arg("b2_lp").c_str(),    sizeof(btn2_long_payload));
  strlcpy(btn2_long_url,      server.arg("b2_lurl").c_str(),  sizeof(btn2_long_url));
  strlcpy(btn2_click_mode,    server.arg("b2_dmode").c_str(), sizeof(btn2_click_mode));
  strlcpy(btn2_click_topic,   server.arg("b2_dt").c_str(),    sizeof(btn2_click_topic));
  strlcpy(btn2_click_payload, server.arg("b2_dp").c_str(),    sizeof(btn2_click_payload));
  strlcpy(btn2_click_url,     server.arg("b2_durl").c_str(),  sizeof(btn2_click_url));

  // bouton 3
  strlcpy(btn3_name,          server.arg("b3_name").c_str(),  sizeof(btn3_name));
  strlcpy(btn3_color,         server.arg("b3_color").c_str(), sizeof(btn3_color));
  strlcpy(btn3_mode,          server.arg("b3_mode").c_str(),  sizeof(btn3_mode));
  strlcpy(btn3_topic,         server.arg("b3_t").c_str(),     sizeof(btn3_topic));
  strlcpy(btn3_payload,       server.arg("b3_p").c_str(),     sizeof(btn3_payload));
  strlcpy(btn3_url,           server.arg("b3_url").c_str(),   sizeof(btn3_url));
  strlcpy(btn3_long_mode,     server.arg("b3_lm").c_str(),    sizeof(btn3_long_mode));
  strlcpy(btn3_long_act,      server.arg("b3_lact").c_str(),  sizeof(btn3_long_act));
  strlcpy(btn3_long_topic,    server.arg("b3_lt").c_str(),    sizeof(btn3_long_topic));
  strlcpy(btn3_long_payload,  server.arg("b3_lp").c_str(),    sizeof(btn3_long_payload));
  strlcpy(btn3_long_url,      server.arg("b3_lurl").c_str(),  sizeof(btn3_long_url));
  strlcpy(btn3_click_mode,    server.arg("b3_dmode").c_str(), sizeof(btn3_click_mode));
  strlcpy(btn3_click_topic,   server.arg("b3_dt").c_str(),    sizeof(btn3_click_topic));
  strlcpy(btn3_click_payload, server.arg("b3_dp").c_str(),    sizeof(btn3_click_payload));
  strlcpy(btn3_click_url,     server.arg("b3_durl").c_str(),  sizeof(btn3_click_url));

  if (server.hasArg("t_long"))   long_press_ms   = server.arg("t_long").toInt();
  if (server.hasArg("t_double")) double_click_ms = server.arg("t_double").toInt();

  if (server.hasArg("b1_gpio")) btn1_pin = server.arg("b1_gpio").toInt();
  if (server.hasArg("b2_gpio")) btn2_pin = server.arg("b2_gpio").toInt();
  if (server.hasArg("b3_gpio")) btn3_pin = server.arg("b3_gpio").toInt();

  save_config();

  String html = "<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{background:#111827;font-family:sans-serif;display:flex;flex-direction:column;align-items:center;justify-content:center;height:80vh;color:#f3f4f6;} div{background:#1f2937;padding:24px;border-radius:14px;border:1px solid #374151;text-align:center;box-shadow:0 4px 10px rgba(0,0,0,0.3);}</style></head><body><div><h3 style='color:#34d399;'>M&eacute;morisation appliqu&eacute;e avec succ&egrave;s !</h3><p>Relancement de l'ESP8266...</p></div><script>setTimeout(function(){window.location.href='/';},3000);</script></body></html>";
  server.send(200, "text/html", html);
  delay(1000);
  ESP.restart();
}

void handle_trigger() {
  int id = server.arg("id").toInt();
  String type = server.arg("type");
  bool ok = false;

  if (id == 1) {
    if (type == "court")  ok = execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court-web");
    if (type == "double") ok = execute_action(btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url, "btn1-double-web");
    if (type == "long")   ok = execute_action(btn1_long_act, btn1_long_topic, btn1_long_payload, btn1_long_url, "btn1-long-web");
  } else if (id == 2) {
    if (type == "court")  ok = execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court-web");
    if (type == "double") ok = execute_action(btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url, "btn2-double-web");
    if (type == "long")   ok = execute_action(btn2_long_act, btn2_long_topic, btn2_long_payload, btn2_long_url, "btn2-long-web");
  } else if (id == 3) {
    if (type == "court")  ok = execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court-web");
    if (type == "double") ok = execute_action(btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url, "btn3-double-web");
    if (type == "long")   ok = execute_action(btn3_long_act, btn3_long_topic, btn3_long_payload, btn3_long_url, "btn3-long-web");
  }

  server.send(200, "text/plain", ok ? "1" : "0");
}

// ================================================================== loop reseau

void handle_network() {
  unsigned long now = millis();
  bool wifi_connected = (WiFi.status() == WL_CONNECTED);

  if (!wifi_connected) {
    net_state = net_wifi_ko;
    // reconnexion en arriere-plan geree par l'esp via setautoreconnect(true)
    return;
  }

  if (mqtt_enabled) {
    if (!mqtt_client.connected()) {
      net_state = net_mqtt_ko;
      if (now - last_mqtt_attempt >= 5000) {
        last_mqtt_attempt = now;
        Serial.println("[mqtt] reconnexion...");
        String clientId = "telecommande-" + String(ESP.getChipId());
        bool mq_ok = false;
        if (strlen(mqtt_user) > 0) {
          mq_ok = mqtt_client.connect(clientId.c_str(), mqtt_user, mqtt_pass);
        } else {
          mq_ok = mqtt_client.connect(clientId.c_str());
        }
        if (mq_ok) {
          Serial.println("[mqtt] connecte");
          net_state = net_ok;
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

// ================================================================== setup & loop principal

void setup() {
  Serial.begin(115200);
  Serial.println("\n[sys] demarrage");

  pinMode(pin_led, OUTPUT);
  digitalWrite(pin_led, HIGH);

  load_config();

  // GPIO configures apres load_config pour utiliser les valeurs lues
  pinMode(btn1_pin, INPUT);
  pinMode(btn2_pin, INPUT);
  pinMode(btn3_pin, INPUT);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  if (strlen(wifi_ssid) > 0) {
    Serial.printf("[wifi] connexion a %s\n", wifi_ssid);
    WiFi.begin(wifi_ssid, wifi_pass);
    net_state = net_reconnecting;
  } else {
    Serial.println("[wifi] pas de ssid configure. ap de secours");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("telecommande-config", "12345678");
    is_ap_mode = true;
    net_state = net_wifi_ko;
    Serial.print("[wifi] ip ap de secours : ");
    Serial.println(WiFi.softAPIP());
  }

  if (mqtt_enabled && strlen(mqtt_server) > 0) {
    mqtt_client.setServer(mqtt_server, atoi(mqtt_port));
  }

  server.on("/", HTTP_GET, handle_root);
  server.on("/setup", HTTP_GET, handle_setup);
  server.on("/save", HTTP_POST, handle_save);
  server.on("/trigger", HTTP_GET, handle_trigger);
  server.on("/backup", HTTP_GET, handleBackup);
  server.on("/restore", HTTP_POST, handleRestore);

  httpupdater.setup(&server);
  server.begin();
  Serial.println("[http] serveur initialise");
}

void loop() {
  server.handleClient();
  
  if (!is_ap_mode) {
    handle_network();
  }
  
  update_led();

  unsigned long now = millis();

  bool r1 = digitalRead(btn1_pin);
  bool r2 = digitalRead(btn2_pin);
  bool r3 = digitalRead(btn3_pin);

  // ---- machine d'etats bouton 1 ----
  if (r1 != last_raw_btn1_state) { last_debounce_time1 = now; last_raw_btn1_state = r1; }
  if ((now - last_debounce_time1) > debounce_delay) {
    if (r1 != debounced_btn1_state) {
      debounced_btn1_state = r1;
      if (debounced_btn1_state == HIGH) {
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
        Serial.println("[action] d1 double");
        execute_action(btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url, "btn1-double");
      } else {
        Serial.println("[action] d1 double ignore, court x2");
        execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court");
        execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court");
      }
    } else {
      if (strcmp(btn1_long_mode, "long") == 0) {
        Serial.println("[action] d1 court seul");
        execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court-seul");
      } else {
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
      if (debounced_btn2_state == HIGH) {
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
    Serial.println("[action] my long");
    execute_action(btn2_long_act, btn2_long_topic, btn2_long_payload, btn2_long_url, "btn2-long");
  }
  if (!pressed2 && click_count2 > 0 && (now - release_time2 >= double_click_ms)) {
    if (click_count2 >= 2) {
      if (strlen(btn2_click_topic) > 0 || strlen(btn2_click_url) > 0) {
        Serial.println("[action] my double");
        execute_action(btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url, "btn2-double");
      } else {
        Serial.println("[action] my double ignore, court x2");
        execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court");
        execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court");
      }
    } else {
      if (strcmp(btn2_long_mode, "long") == 0) {
        Serial.println("[action] my court seul");
        execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court-seul");
      } else {
        Serial.println("[action] my court");
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
      if (debounced_btn3_state == HIGH) {
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
        Serial.println("[action] d5 double");
        execute_action(btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url, "btn3-double");
      } else {
        Serial.println("[action] d5 double ignore, court x2");
        execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court");
        execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court");
      }
    } else {
      if (strcmp(btn3_long_mode, "long") == 0) {
        Serial.println("[action] d5 court seul");
        execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court-seul");
      } else {
        Serial.println("[action] d5 court");
        execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court");
      }
    }
    click_count3 = 0;
  }
}
