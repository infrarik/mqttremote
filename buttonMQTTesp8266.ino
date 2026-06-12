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

// structures pour machine d'etat des clics physiques
unsigned long press_start1 = 0; unsigned long release_time1 = 0;
int click_count1 = 0; bool pressed1 = false; bool long_fired1 = false;
unsigned long press_start2 = 0; unsigned long release_time2 = 0;
int click_count2 = 0; bool pressed2 = false; bool long_fired2 = false;
unsigned long press_start3 = 0; unsigned long release_time3 = 0;
int click_count3 = 0; bool pressed3 = false; bool long_fired3 = false;

// ------------------------------------------------------------------ objets
ESP8266WebServer         server(80);
ESP8266HTTPUpdateServer  httpupdater;
WiFiClient               esp_client;
PubSubClient             mqtt_client(esp_client);

bool is_ap_mode = false;
File fsUploadFile;

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
    html += "<div class='pill " + String(wifi_ok && mqtt_ok ? "pill-ok" : "pill-err") + "'>";
    html += "<div class='dot " + String(wifi_ok && mqtt_ok ? "dot-ok" : "dot-err") + "'></div>mqtt</div>";
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
  s += "<div class='row'><label>topic long</label><input type='text' name='b" + p + "_lt' value='" + String(ltopic) + "' placeholder='laisser vide pour d&eacute;sactiver'></div>";
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
    ".sec-net{color:#3b82f6;}.sec-mqtt{color:#f59e0b;}.sec-btns{color:#10b981;}.sec-rob{color:#ec4899;}.sec-ota{color:#8b5cf6;}.sec-exp{color:#2563eb;}" +
    ".row{margin-bottom:12px;}" +
    "label{display:block;font-size:12px;color:#6b7280;margin-bottom:4px;}" +
    ".opt{color:#9ca3af;font-style:italic;}" +
    "input,select{width:100%;box-sizing:border-box;padding:9px 10px;border:1px solid #d1d5db;border-radius:8px;font-size:14px;background:#fafafa;color:#111;appearance:auto;}" +
    "input:focus,select:focus{outline:none;border-color:#3b82f6;background:#fff;}" +
    ".grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px;}" +
    ".btn-card{background:#fafafa;border:1px solid #e5e7eb;border-radius:10px;padding:14px;margin-bottom:14px;}" +
    ".btn-card-hdr{font-size:13px;font-weight:600;margin-bottom:12px;display:flex;align-items:center;gap:8px;color:#374151;border-bottom:1px solid #eed;padding-bottom:4px;}" +
    ".clr-dot{width:10px;height:10px;border-radius:50%;border:1px solid rgba(0,0,0,0.1);}" +
    ".long-hdr{font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:0.05em;color:#ec4899;margin:14px 0 10px;padding-top:10px;border-top:1px solid #fbcfe8;}" +
    ".submit-btn{width:calc(100% - 24px);margin:20px 12px 0;box-sizing:border-box;background:#10b981;color:#fff;border:none;padding:14px;border-radius:10px;font-size:15px;font-weight:600;cursor:pointer;}" +
    ".submit-btn:active{opacity:0.8;}" +
    "</style>" +
    "<script>" +
    "function upd(sel,dotId){" +
      "var c='#cccccc';" +
      "if(sel.value=='green')c='#52c98a';" +
      "if(sel.value=='red')c='#e05555';" +
      "if(sel.value=='blue')c='#5b8dee';" +
      "if(sel.value=='orange')c='#e09b40';" +
      "document.getElementById(dotId).style.background=c;" +
    "}" +
    "function tglFields(sel,btnNum,suffix){" +
      "var m=sel.value;" +
      "var mqttDiv=document.getElementById('b'+btnNum+'_'+suffix+'_mqtt_fields');" +
      "var httpDiv=document.getElementById('b'+btnNum+'_'+suffix+'_http_fields');" +
      "if(m=='http'){" +
        "mqttDiv.style.display='none';" +
        "httpDiv.style.display='block';" +
      "}else if(m=='mqtt'){" +
        "mqttDiv.style.display='block';" +
        "httpDiv.style.display='none';" +
      "}else{" +
        "mqttDiv.style.display='block';" +
        "httpDiv.style.display='block';" +
      "}" +
    "}" +
    "function testaction(btnNum,type){" +
      "var x=new XMLHttpRequest();" +
      "x.open('GET','/trigger?id='+btnNum+'&type='+type,true);" +
      "x.send();" +
      "alert('action de test transmise (bouton '+btnNum+' - '+type+')');" +
    "}" +
    "</script>" +
    "</head><body>" +
    "<div class='topbar'><span class='topbar-title'>Param&egrave;tres contr&ocirc;leur</span><a href='/' class='back'>&lArr; retour</a></div>" +
    "<form method='POST' action='/save'>";

  // section reseau
  html += "<div class='section'><div class='sec-hdr sec-net'>&#128246; r&eacute;seau wifi</div>";
  html += "<div class='row'><label>nom du réseau (ssid)</label><select name='ssid'>";
  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    html += "<option value='" + s + "'" + (s == String(wifi_ssid) ? " selected" : "") + ">" + s + "</option>";
  }
  html += "</select></div>";
  html += "<div class='row'><label>cl&eacute; wifi</label><input type='password' name='pass' value='" + String(wifi_pass) + "'></div></div>";

  // section mqtt globaux
  html += "<div class='section'><div class='sec-hdr sec-mqtt'>&#9889; broker mqtt</div>";
  html += "<div class='row'><label style='display:inline;margin-right:8px;'>activer mqtt</label>";
  html += "<input type='checkbox' name='mqtt_en' value='1' style='width:auto;appearance:checkbox;' " + String(mqtt_enabled ? "checked" : "") + "></div>";
  html += "<div class='row'><label>adresse ip du broker</label><input type='text' name='mqtt_srv' value='" + String(mqtt_server) + "'></div>";
  html += "<div class='row'><label>port</label><input type='text' name='mqtt_prt' value='" + String(mqtt_port) + "'></div>";
  html += "<div class='row'><label>utilisateur <span class='opt'>(optionnel)</span></label><input type='text' name='mqtt_u' value='" + String(mqtt_user) + "'></div>";
  html += "<div class='row'><label>mot de passe <span class='opt'>(optionnel)</span></label><input type='password' name='mqtt_p' value='" + String(mqtt_pass) + "'></div></div>";

  // section boutons
  html += "<div class='section'><div class='sec-hdr sec-btns'>&#128434; configuration des boutons</div>";
  html += btn_card(1, "broche D1 / GPIO5", btn1_name, btn1_color, btn1_mode, btn1_topic, btn1_payload, btn1_url, btn1_long_topic, btn1_long_payload, btn1_long_url, btn1_long_mode, btn1_long_act, btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url);
  html += btn_card(2, "broche D2 / GPIO4", btn2_name, btn2_color, btn2_mode, btn2_topic, btn2_payload, btn2_url, btn2_long_topic, btn2_long_payload, btn2_long_url, btn2_long_mode, btn2_long_act, btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url);
  html += btn_card(3, "broche D5 / GPIO14", btn3_name, btn3_color, btn3_mode, btn3_topic, btn3_payload, btn3_url, btn3_long_topic, btn3_long_payload, btn3_long_url, btn3_long_mode, btn3_long_act, btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url);
  html += "</div>";

  // section robustesse & filtres
  html += "<div class='section'><div class='sec-hdr sec-rob'>&#128338; robustesse et filtres</div>";
  html += "<div class='grid2'>";
  html += "<div class='row'><label>appui long (ms)</label><input type='number' name='t_long' value='" + String(long_press_ms) + "'></div>";
  html += "<div class='row'><label>double clic (ms)</label><input type='number' name='t_double' value='" + String(double_click_ms) + "'></div>";
  html += "</div></div>";

  html += "<button type='submit' class='submit-btn'>enregistrer les modifications</button></form>";

  // section sauvegarde et restauration json (export/import)
  html += "<div class='section'><div class='sec-hdr sec-exp'>&#128190; sauvegarde et restauration</div>";
  html += "<div class='row'><label>exporter le fichier de configuration actuel</label>";
  html += "<a href='/export' style='display:inline-block;padding:8px 12px;background:#2563eb;color:#fff;text-decoration:none;border-radius:6px;font-size:13px;font-weight:500;'>t&eacute;l&eacute;charger config.json</a></div>";
  html += "<div class='row'><label>importer un fichier de configuration existant</label>";
  html += "<form method='POST' action='/import' enctype='multipart/form-data' style='display:flex;gap:10px;align-items:center;margin-top:4px;'>";
  html += "<input type='file' name='importfile' style='font-size:13px;background:none;border:none;padding:0;'>";
  html += "<button type='submit' style='width:auto;padding:7px 12px;background:#059669;color:#fff;border:none;border-radius:6px;font-size:13px;font-weight:500;cursor:pointer;'>charger</button>";
  html += "</form></div></div>";

  // section mise a jour firmware ota
  html += "<div class='section'><div class='sec-hdr sec-ota'>&#128229; mise &agrave; jour du firmware</div>";
  html += "<div class='row'><label>acc&eacute;der &agrave; la console ota d'origine</label>";
  html += "<a href='/update' style='display:inline-block;padding:8px 12px;background:#8b5cf6;color:#fff;text-decoration:none;border-radius:6px;font-size:13px;font-weight:500;'>ouvrir la page ota</a></div></div>";

  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handle_save() {
  if (server.hasArg("ssid")) strlcpy(wifi_ssid, server.arg("ssid").c_str(), sizeof(wifi_ssid));
  if (server.hasArg("pass")) strlcpy(wifi_pass, server.arg("pass").c_str(), sizeof(wifi_pass));
  
  mqtt_enabled = server.hasArg("mqtt_en");
  if (server.hasArg("mqtt_srv")) strlcpy(mqtt_server, server.arg("mqtt_srv").c_str(), sizeof(mqtt_server));
  if (server.hasArg("mqtt_prt")) strlcpy(mqtt_port, server.arg("mqtt_prt").c_str(), sizeof(mqtt_port));
  if (server.hasArg("mqtt_u"))   strlcpy(mqtt_user, server.arg("mqtt_u").c_str(), sizeof(mqtt_user));
  if (server.hasArg("mqtt_p"))   strlcpy(mqtt_pass, server.arg("mqtt_p").c_str(), sizeof(mqtt_pass));

  // sauver b1
  if (server.hasArg("b1_mode"))  strlcpy(btn1_mode, server.arg("b1_mode").c_str(), sizeof(btn1_mode));
  if (server.hasArg("b1_t"))     strlcpy(btn1_topic, server.arg("b1_t").c_str(), sizeof(btn1_topic));
  if (server.hasArg("b1_p"))     strlcpy(btn1_payload, server.arg("b1_p").c_str(), sizeof(btn1_payload));
  if (server.hasArg("b1_url"))   strlcpy(btn1_url, server.arg("b1_url").c_str(), sizeof(btn1_url));
  if (server.hasArg("b1_name"))  strlcpy(btn1_name, server.arg("b1_name").c_str(), sizeof(btn1_name));
  if (server.hasArg("b1_color")) strlcpy(btn1_color, server.arg("b1_color").c_str(), sizeof(btn1_color));
  if (server.hasArg("b1_lt"))    strlcpy(btn1_long_topic, server.arg("b1_lt").c_str(), sizeof(btn1_long_topic));
  if (server.hasArg("b1_lp"))    strlcpy(btn1_long_payload, server.arg("b1_lp").c_str(), sizeof(btn1_long_payload));
  if (server.hasArg("b1_lurl"))  strlcpy(btn1_long_url, server.arg("b1_lurl").c_str(), sizeof(btn1_long_url));
  if (server.hasArg("b1_lm"))    strlcpy(btn1_long_mode, server.arg("b1_lm").c_str(), sizeof(btn1_long_mode));
  if (server.hasArg("b1_lact"))  strlcpy(btn1_long_act, server.arg("b1_lact").c_str(), sizeof(btn1_long_act));
  if (server.hasArg("b1_dmode")) strlcpy(btn1_click_mode, server.arg("b1_dmode").c_str(), sizeof(btn1_click_mode));
  if (server.hasArg("b1_dt"))    strlcpy(btn1_click_topic, server.arg("b1_dt").c_str(), sizeof(btn1_click_topic));
  if (server.hasArg("b1_dp"))    strlcpy(btn1_click_payload, server.arg("b1_dp").c_str(), sizeof(btn1_click_payload));
  if (server.hasArg("b1_durl"))  strlcpy(btn1_click_url, server.arg("b1_durl").c_str(), sizeof(btn1_click_url));

  // sauver b2
  if (server.hasArg("b2_mode"))  strlcpy(btn2_mode, server.arg("b2_mode").c_str(), sizeof(btn2_mode));
  if (server.hasArg("b2_t"))     strlcpy(btn2_topic, server.arg("b2_t").c_str(), sizeof(btn2_topic));
  if (server.hasArg("b2_p"))     strlcpy(btn2_payload, server.arg("b2_p").c_str(), sizeof(btn2_payload));
  if (server.hasArg("b2_url"))   strlcpy(btn2_url, server.arg("b2_url").c_str(), sizeof(btn2_url));
  if (server.hasArg("b2_name"))  strlcpy(btn2_name, server.arg("b2_name").c_str(), sizeof(btn2_name));
  if (server.hasArg("b2_color")) strlcpy(btn2_color, server.arg("b2_color").c_str(), sizeof(btn2_color));
  if (server.hasArg("b2_lt"))    strlcpy(btn2_long_topic, server.arg("b2_lt").c_str(), sizeof(btn2_long_topic));
  if (server.hasArg("b2_lp"))    strlcpy(btn2_long_payload, server.arg("b2_lp").c_str(), sizeof(btn2_long_payload));
  if (server.hasArg("b2_lurl"))  strlcpy(btn2_long_url, server.arg("b2_lurl").c_str(), sizeof(btn2_long_url));
  if (server.hasArg("b2_lm"))    strlcpy(btn2_long_mode, server.arg("b2_lm").c_str(), sizeof(btn2_long_mode));
  if (server.hasArg("b2_lact"))  strlcpy(btn2_long_act, server.arg("b2_lact").c_str(), sizeof(btn2_long_act));
  if (server.hasArg("b2_dmode")) strlcpy(btn2_click_mode, server.arg("b2_dmode").c_str(), sizeof(btn2_click_mode));
  if (server.hasArg("b2_dt"))    strlcpy(btn2_click_topic, server.arg("b2_dt").c_str(), sizeof(btn2_click_topic));
  if (server.hasArg("b2_dp"))    strlcpy(btn2_click_payload, server.arg("b2_dp").c_str(), sizeof(btn2_click_payload));
  if (server.hasArg("b2_durl"))  strlcpy(btn2_click_url, server.arg("b2_durl").c_str(), sizeof(btn2_click_url));

  // sauver b3
  if (server.hasArg("b3_mode"))  strlcpy(btn3_mode, server.arg("b3_mode").c_str(), sizeof(btn3_mode));
  if (server.hasArg("b3_t"))     strlcpy(btn3_topic, server.arg("b3_t").c_str(), sizeof(btn3_topic));
  if (server.hasArg("b3_p"))     strlcpy(btn3_payload, server.arg("b3_p").c_str(), sizeof(btn3_payload));
  if (server.hasArg("b3_url"))   strlcpy(btn3_url, server.arg("b3_url").c_str(), sizeof(btn3_url));
  if (server.hasArg("b3_name"))  strlcpy(btn3_name, server.arg("b3_name").c_str(), sizeof(btn3_name));
  if (server.hasArg("b3_color")) strlcpy(btn3_color, server.arg("b3_color").c_str(), sizeof(btn3_color));
  if (server.hasArg("b3_lt"))    strlcpy(btn3_long_topic, server.arg("b3_lt").c_str(), sizeof(btn3_long_topic));
  if (server.hasArg("b3_lp"))    strlcpy(btn3_long_payload, server.arg("b3_lp").c_str(), sizeof(btn3_long_payload));
  if (server.hasArg("b3_lurl"))  strlcpy(btn3_long_url, server.arg("b3_lurl").c_str(), sizeof(btn3_long_url));
  if (server.hasArg("b3_lm"))    strlcpy(btn3_long_mode, server.arg("b3_lm").c_str(), sizeof(btn3_long_mode));
  if (server.hasArg("b3_lact"))  strlcpy(btn3_long_act, server.arg("b3_lact").c_str(), sizeof(btn3_long_act));
  if (server.hasArg("b3_dmode")) strlcpy(btn3_click_mode, server.arg("b3_dmode").c_str(), sizeof(btn3_click_mode));
  if (server.hasArg("b3_dt"))    strlcpy(btn3_click_topic, server.arg("b3_dt").c_str(), sizeof(btn3_click_topic));
  if (server.hasArg("b3_dp"))    strlcpy(btn3_click_payload, server.arg("b3_dp").c_str(), sizeof(btn3_click_payload));
  if (server.hasArg("b3_durl"))  strlcpy(btn3_click_url, server.arg("b3_durl").c_str(), sizeof(btn3_click_url));

  if (server.hasArg("t_long"))   long_press_ms  = server.arg("t_long").toInt();
  if (server.hasArg("t_double")) double_click_ms = server.arg("t_double").toInt();

  save_config();

  String html = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>sauvegarde</title>";
  html += "<style>body{background:#111827;color:#fff;font-family:sans-serif;text-align:center;padding-top:50px;}</style>";
  html += "<script>setTimeout(function(){window.location.href='/';},2500);</script></head>";
  html += "<body><h3>configuration enregistr&eacute;e avec succ&egrave;s !</h3><p>redirection automatique...</p></body></html>";
  server.send(200, "text/html", html);

  delay(500);
  ESP.restart();
}

void handle_trigger() {
  if (!server.hasArg("id") || !server.hasArg("type")) {
    server.send(400, "text/plain", "parametres manquants");
    return;
  }
  int id = server.arg("id").toInt();
  String type = server.arg("type");
  bool ok = false;

  if (id == 1) {
    if (type == "court")  ok = execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "web-b1-court");
    if (type == "double") ok = execute_action(btn1_click_mode, btn1_click_topic, btn1_click_payload, btn1_click_url, "web-b1-double");
    if (type == "long")   ok = execute_action(btn1_long_act, btn1_long_topic, btn1_long_payload, btn1_long_url, "web-b1-long");
  } else if (id == 2) {
    if (type == "court")  ok = execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "web-b2-court");
    if (type == "double") ok = execute_action(btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url, "web-b2-double");
    if (type == "long")   ok = execute_action(btn2_long_act, btn2_long_topic, btn2_long_payload, btn2_long_url, "web-b2-long");
  } else if (id == 3) {
    if (type == "court")  ok = execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "web-b3-court");
    if (type == "double") ok = execute_action(btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url, "web-b3-double");
    if (type == "long")   ok = execute_action(btn3_long_act, btn3_long_topic, btn3_long_payload, btn3_long_url, "web-b3-long");
  }

  server.send(200, "text/plain", ok ? "ok" : "echec");
}

// ================================================================== handlers export / import json

void handle_export() {
  if (!LittleFS.exists("/config.json")) {
    server.send(404, "text/plain", "aucun fichier trouve");
    return;
  }
  File f = LittleFS.open("/config.json", "r");
  server.sendHeader("Content-Disposition", "attachment; filename=config.json");
  server.streamFile(f, "application/json");
  f.close();
}

void handle_import_page() {
  String html = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>import</title>";
  html += "<style>body{background:#111827;color:#fff;font-family:sans-serif;text-align:center;padding-top:50px;}</style>";
  html += "<script>setTimeout(function(){window.location.href='/setup';},3000);</script></head>";
  html += "<body><h3>configuration import&eacute;e avec succ&egrave;s !</h3><p>red&eacute;marrage de l'appareil...</p></body></html>";
  server.send(200, "text/html", html);
  delay(500);
  ESP.restart();
}

void handle_import_upload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    fsUploadFile = LittleFS.open("/config.json", "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) {
      fsUploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
      Serial.println("[import] fichier config reçu et enregistre");
    }
  }
}

// ================================================================== reseau machine etat

void handle_wifi_mqtt() {
  if (is_ap_mode) return;
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    net_state = net_wifi_ko;
    if (now - last_wifi_attempt >= wifi_backoff_ms) {
      last_wifi_attempt = now;
      Serial.println("[wifi] tentative reconnexion...");
      WiFi.begin(wifi_ssid, wifi_pass);
    }
    return;
  }

  if (net_state == net_wifi_ko || net_state == net_reconnecting) {
    Serial.println("[wifi] connecte. ip: " + WiFi.localIP().toString());
    reconnect_count++;
    net_state = net_ok;
  }

  if (mqtt_enabled) {
    if (!mqtt_client.connected()) {
      net_state = net_mqtt_ko;
      if (now - last_mqtt_attempt >= 5000) {
        last_mqtt_attempt = now;
        Serial.println("[mqtt] tentative connexion broker...");
        String clientId = "esp-btn-" + String(ESP.getChipId());
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
      if (net_state == net_mqtt_ko) net_state = net_ok;
      mqtt_client.loop();
    }
  } else {
    if (net_state == net_mqtt_ko) net_state = net_ok;
  }
}

// ================================================================== setup initial

void setup() {
  Serial.begin(115200);
  Serial.println("\n[boot] demarrage");

  pinMode(btn1_pin, INPUT_PULLUP);
  pinMode(btn2_pin, INPUT_PULLUP);
  pinMode(btn3_pin, INPUT_PULLUP);
  pinMode(pin_led, OUTPUT);
  digitalWrite(pin_led, HIGH);

  load_config();

  if (strlen(wifi_ssid) == 0) {
    Serial.println("[boot] aucun ssid configure. passage en mode ap");
    WiFi.mode(WIFI_AP);
    String apName = "telecommande-" + String(ESP.getChipId());
    WiFi.softAP(apName.c_str(), "12345678");
    Serial.println("[boot] ap cree : " + apName + " (ip: 192.168.4.1)");
    is_ap_mode = true;
    net_state = net_ok;
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_pass);
    Serial.println("[boot] connexion au reseau : " + String(wifi_ssid));
    last_wifi_attempt = millis();
    net_state = net_reconnecting;
  }

  if (mqtt_enabled && strlen(mqtt_server) > 0) {
    mqtt_client.setServer(mqtt_server, atoi(mqtt_port));
  }

  server.on("/", HTTP_GET, handle_root);
  server.on("/setup", HTTP_GET, handle_setup);
  server.on("/save", HTTP_POST, handle_save);
  server.on("/trigger", HTTP_GET, handle_trigger);
  
  // routes pour import / export config
  server.on("/export", HTTP_GET, handle_export);
  server.on("/import", HTTP_POST, handle_import_page, handle_import_upload);

  httpupdater.setup(&server);
  server.begin();
  Serial.println("[boot] serveur HTTP pret");
}

// ================================================================== boucle principale

void loop() {
  server.handleClient();
  handle_wifi_mqtt();
  update_led();

  unsigned long now = millis();

  // ---------------------------------------------------- traitement bouton 1
  bool r1 = digitalRead(btn1_pin);
  if (r1 != last_raw_btn1_state) { last_debounce_time1 = now; last_raw_btn1_state = r1; }
  if ((now - last_debounce_time1) >= debounce_delay) {
    if (r1 != debounced_btn1_state) {
      debounced_btn1_state = r1;
      if (debounced_btn1_state == LOW) {
        pressed1 = true; long_fired1 = false; press_start1 = now;
      } else {
        if (pressed1 && !long_fired1) { click_count1++; release_time1 = now; }
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
        Serial.println("[action] d1 double (non configure, fallback court)");
        execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court-fallback");
      }
    } else {
      if (strcmp(btn1_long_mode, "both") == 0 || (strcmp(btn1_long_mode, "long") == 0 && !long_fired1)) {
        Serial.println("[action] d1 court");
        execute_action(btn1_mode, btn1_topic, btn1_payload, btn1_url, "btn1-court");
      }
    }
    click_count1 = 0;
  }

  // ---------------------------------------------------- traitement bouton 2
  bool r2 = digitalRead(btn2_pin);
  if (r2 != last_raw_btn2_state) { last_debounce_time2 = now; last_raw_btn2_state = r2; }
  if ((now - last_debounce_time2) >= debounce_delay) {
    if (r2 != debounced_btn2_state) {
      debounced_btn2_state = r2;
      if (debounced_btn2_state == LOW) {
        pressed2 = true; long_fired2 = false; press_start2 = now;
      } else {
        if (pressed2 && !long_fired2) { click_count2++; release_time2 = now; }
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
        Serial.println("[action] d2 double");
        execute_action(btn2_click_mode, btn2_click_topic, btn2_click_payload, btn2_click_url, "btn2-double");
      } else {
        Serial.println("[action] d2 double (non configure, fallback court)");
        execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court-fallback");
      }
    } else {
      if (strcmp(btn2_long_mode, "both") == 0 || (strcmp(btn2_long_mode, "long") == 0 && !long_fired2)) {
        Serial.println("[action] d2 court");
        execute_action(btn2_mode, btn2_topic, btn2_payload, btn2_url, "btn2-court");
      }
    }
    click_count2 = 0;
  }

  // ---------------------------------------------------- traitement bouton 3
  bool r3 = digitalRead(btn3_pin);
  if (r3 != last_raw_btn3_state) { last_debounce_time3 = now; last_raw_btn3_state = r3; }
  if ((now - last_debounce_time3) >= debounce_delay) {
    if (r3 != debounced_btn3_state) {
      debounced_btn3_state = r3;
      if (debounced_btn3_state == LOW) {
        pressed3 = true; long_fired3 = false; press_start3 = now;
      } else {
        if (pressed3 && !long_fired3) { click_count3++; release_time3 = now; }
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
        Serial.println("[action] d3 double");
        execute_action(btn3_click_mode, btn3_click_topic, btn3_click_payload, btn3_click_url, "btn3-double");
      } else {
        Serial.println("[action] d3 double (non configure, fallback court)");
        execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-double-fallback");
      }
    } else {
      if (strcmp(btn3_long_mode, "both") == 0 || (strcmp(btn3_long_mode, "long") == 0 && !long_fired3)) {
        Serial.println("[action] d3 court");
        execute_action(btn3_mode, btn3_topic, btn3_payload, btn3_url, "btn3-court");
      }
    }
    click_count3 = 0;
  }
}
