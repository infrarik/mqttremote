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
char esp_hostname[32]= "button_mqtt";

// Bouton 1
char btn1_topic[60]        = "esp/btn1/topic";
char btn1_payload[100]     = "message_btn1";
char btn1_name[20]         = "UP";
char btn1_color[10]        = "green";
char btn1_long_topic[60]   = "";
char btn1_long_payload[100]= "";
char btn1_long_mode[6]     = "both";

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

// ------------------------------------------------------------------ debounce et appuis physiques
bool last_btn1_state = HIGH;
bool last_btn2_state = HIGH;
bool last_btn3_state = HIGH;
unsigned long last_debounce_time1 = 0;
unsigned long last_debounce_time2 = 0;
unsigned long last_debounce_time3 = 0;
const unsigned long debounce_delay = 50;

bool pressed1 = false;
bool pressed2 = false;
bool pressed3 = false;
unsigned long press_start1 = 0;
unsigned long press_start2 = 0;
unsigned long press_start3 = 0;
bool long_fired1 = false;
bool long_fired2 = false;
bool long_fired3 = false;

// ------------------------------------------------------------------ objets web & mqtt
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

// ================================================================== gestion mqtt et publication

void publish_if_connected(const char* topic, const char* payload, const char* debug_label) {
  if (strlen(topic) == 0) {
    Serial.printf("[%s] aucun topic configure, annulation.\n", debug_label);
    return;
  }
  if (mqtt_client.connected()) {
    Serial.printf("[%s] publication sur %s : %s\n", debug_label, topic, payload);
    mqtt_client.publish(topic, payload);
  } else {
    Serial.printf("[%s] echec : mqtt non connecte. message perdu.\n", debug_label);
  }
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
    strlcpy(esp_hostname,        json["esp_hostname"]        | "button_mqtt",   sizeof(esp_hostname));
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
  json["esp_hostname"]      = esp_hostname;
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
    case NET_OK:           interval = 0; break;
    case NET_MQTT_KO:      interval = 800;  break;
    case NET_WIFI_KO:      interval = 150;  break;
    case NET_RECONNECTING: interval = 300;  break;
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
    "<title>telecommande</title>"
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
    ".btn-toggle{width:100%;padding:14px 0;border-radius:14px;border:1px solid #374151;cursor:pointer;font-size:14px;font-weight:600;letter-spacing:0.05em;text-transform:uppercase;transition:all 0.2s;margin-bottom:4px;box-sizing:border-box;text-align:center;}"
    ".toggle-off{background:#1f2937;color:#9ca3af;}"
    ".toggle-on{background:#3b0d0d;color:#e05555;border-color:#6b1717;box-shadow:0 0 10px rgba(224,85,85,0.2);}"
    ".footer{padding:12px 0 24px;border-top:1px solid #1f2937;width:100%;text-align:center;}"
    ".footer a{color:#374151;font-size:13px;text-decoration:none;}"
    "</style></head><body>");

  html += "<div class='hdr'>";
  html += "<div class='status-row'>";
  html += wifi_ok ? F("<div class='pill pill-ok'><span class='dot dot-ok'></span>WIFI</div>") : F("<div class='pill pill-err'><span class='dot dot-err'></span>WIFI</div>");
  html += mqtt_ok ? F("<div class='pill pill-ok'><span class='dot dot-ok'></span>MQTT</div>") : F("<div class='pill pill-err'><span class='dot dot-err'></span>MQTT</div>");
  html += "</div>";
  html += "<div class='ip'>" + WiFi.localIP().toString() + " (" + String(esp_hostname) + ")</div>";
  if (!mqtt_ok && wifi_ok) {
    html += "<div class='recon'>tentatives mqtt : " + String(reconnect_count) + "</div>";
  }
  html += "</div>";

  html += F("<div class='card'><div class='btns'>");

  // Bouton Toggle APPUI LONG virtuel
  html += F("<button id='longToggle' class='btn-toggle toggle-off' onclick='toggleLongMode()'>Appui court</button>");

  // Bouton 1
  html += "<button class='btn' style='" + color_styles(btn1_color) + "' onclick='sendClick(1)'>"
          "<span>" + String(btn1_name) + "</span>"
          "<span class='btn-icon'>" + btn_icon(1) + "</span>"
          "</button>";

  // Bouton 2
  html += "<button class='btn' style='" + color_styles(btn2_color) + "' onclick='sendClick(2)'>"
          "<span>" + String(btn2_name) + "</span>"
          "<span class='btn-icon'>" + btn_icon(2) + "</span>"
          "</button>";

  // Bouton 3
  html += "<button class='btn' style='" + color_styles(btn3_color) + "' onclick='sendClick(3)'>"
          "<span>" + String(btn3_name) + "</span>"
          "<span class='btn-icon'>" + btn_icon(3) + "</span>"
          "</button>";

  html += F("</div><div class='footer'><a href='/setup'>configuration</a></div></div>");

  html += F("<script>"
    "let longMode = false;"
    "function toggleLongMode() {"
      "longMode = !longMode;"
      "const btn = document.getElementById('longToggle');"
      "if(longMode) {"
        "btn.className = 'btn-toggle toggle-on';"
        "btn.innerText = 'Appui long';"
      "} else {"
        "btn.className = 'btn-toggle toggle-off';"
        "btn.innerText = 'Appui court';"
      "}"
    "}"
    "function sendClick(id) {"
      "const type = longMode ? 'long' : 'short';"
      "fetch('/click?b=' + id + '&t=' + type);"
    "}"
    "</script></body></html>");

  server.send(200, "text/html", html);
}

void handle_click() {
  int btn_id = server.arg("b").toInt();
  String type = server.arg("t");
  bool is_long = (type == "long");

  if (btn_id == 1) {
    if (is_long) publish_if_connected(btn1_long_topic, btn1_long_payload, "web-btn1-long");
    else         publish_if_connected(btn1_topic, btn1_payload, "web-btn1-court");
  } 
  else if (btn_id == 2) {
    if (is_long) publish_if_connected(btn2_long_topic, btn2_long_payload, "web-btn2-long");
    else         publish_if_connected(btn2_topic, btn2_payload, "web-btn2-court");
  } 
  else if (btn_id == 3) {
    if (is_long) publish_if_connected(btn3_long_topic, btn3_long_payload, "web-btn3-long");
    else         publish_if_connected(btn3_topic, btn3_payload, "web-btn3-court");
  }
  server.send(200, "text/plain", "OK");
}

void handle_setup() {
  String html = F("<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>configuration</title>"
    "<style>"
    "body{background:#111827;color:#e5e7eb;font-family:-apple-system,sans-serif;padding:16px;margin:0;}"
    ".box{max-width:550px;margin:0 auto;background:#1f2937;padding:20px;border-radius:12px;box-shadow:0 4px 6px rgba(0,0,0,0.3);}"
    "h2{margin-top:0;color:#f3f4f6;border-bottom:1px solid #374151;padding-bottom:8px;font-size:20px;}"
    "h3{color:#9ca3af;font-size:14px;text-transform:uppercase;margin:24px 0 12px;letter-spacing:0.05em;}"
    ".grid{display:grid;grid-template-columns:1fr;gap:12px;}"
    "@media(min-width:480px){.grid-2{grid-template-columns:1fr 1fr;}}"
    ".f-group{display:flex;flex-direction:column;gap:4px;}"
    "label{font-size:13px;color:#9ca3af;}"
    "input,select{background:#374151;border:1px solid #4b5563;color:#fff;padding:10px;border-radius:6px;font-size:14px;box-sizing:border-box;width:100%;}"
    "input:focus,select:focus{outline:none;border-color:#6366f1;}"
    ".pass-container{position:relative;display:flex;align-items:center;}"
    ".pass-container input{padding-right:40px;}"
    ".eye-btn{position:absolute;right:4px;background:none;border:none;color:#9ca3af;cursor:pointer;padding:8px;font-size:16px;display:flex;align-items:center;justify-content:center;width:36px;height:36px;}"
    ".eye-btn:hover{color:#f3f4f6;}"
    ".actions{margin-top:28px;display:flex;gap:12px;}"
    ".btn-save{background:#10b981;color:#fff;border:none;padding:12px 24px;border-radius:6px;cursor:pointer;font-weight:600;font-size:14px;flex:1;}"
    ".btn-save:hover{background:#059669;}"
    ".btn-back{background:#4b5563;color:#fff;text-decoration:none;padding:12px 20px;border-radius:6px;text-align:center;font-size:14px;font-weight:600;}"
    ".btn-back:hover{background:#374151;}"
    "</style></head><body>"
    "<div class='box'>"
    "<h2>configuration systeme</h2>"
    "<form method='POST' action='/save'>");

  // Section Réseau & ESP Hostname
  html += F("<h3>reseau &amp; peripherique</h3>"
    "<div class='grid'>"
    "<div class='f-group'>"
    "<label>nom de l'esp8266 (reseau)</label>");
  html += "<input type='text' name='esp_hostname' maxlen='31' value='" + String(esp_hostname) + "' placeholder='button_mqtt'>";
  html += F("</div>"
    "</div>"
    "<div class='grid grid-2' style='margin-top:12px;'>"
    "<div class='f-group'>"
    "<label>ssid wifi</label>");
  html += "<input type='text' name='wifi_ssid' maxlen='31' value='" + String(wifi_ssid) + "'>";
  html += F("</div>"
    "<div class='f-group'>"
    "<label>mot de passe wifi</label>"
    "<div class='pass-container'>");
  html += "<input type='password' id='p_wifipass' name='wifi_pass' maxlen='63' value='" + String(wifi_pass) + "'>";
  html += F("<button type='button' class='eye-btn' onclick=\"toggleEye('p_wifipass')\">&#x1F441;</button>"
    "</div>"
    "</div>"
    "</div>");

  // Section MQTT
  html += F("<h3>serveur mqtt</h3>"
    "<div class='grid grid-2'>"
    "<div class='f-group'>"
    "<label>adresse du broker</label>");
  html += "<input type='text' name='mqtt_server' maxlen='39' value='" + String(mqtt_server) + "'>";
  html += F("</div>"
    "<div class='f-group'>"
    "<label>port</label>");
  html += "<input type='number' name='mqtt_port' value='" + String(mqtt_port) + "'>";
  html += F("</div>"
    "<div class='f-group'>"
    "<label>utilisateur</label>");
  html += "<input type='text' name='mqtt_user' maxlen='39' value='" + String(mqtt_user) + "'>";
  html += F("</div>"
    "<div class='f-group'>"
    "<label>mot de passe mqtt</label>"
    "<div class='pass-container'>");
  html += "<input type='password' id='p_mqttpass' name='mqtt_pass' maxlen='39' value='" + String(mqtt_pass) + "'>";
  html += F("<button type='button' class='eye-btn' onclick=\"toggleEye('p_mqttpass')\">&#x1F441;</button>"
    "</div>"
    "</div>"
    "</div>");

  // Options globales (Seuil long)
  html += F("<h3>options globales</h3>"
    "<div class='grid'>"
    "<div class='f-group'>"
    "<label>seuil appui long (ms)</label>");
  html += "<input type='number' name='long_press_ms' value='" + String(long_press_ms) + "'>";
  html += F("</div>"
    "</div>");

  // Macro pour générer les champs d'un bouton
  auto gen_btn_fields = [&](int id, const char* name, const char* topic, const char* payload, const char* color, const char* l_topic, const char* l_payload, const char* l_mode) {
    html += "<h3>bouton " + String(id) + "</h3>"
            "<div class='grid grid-2'>"
            "<div class='f-group'><label>label du bouton</label>"
            "<input type='text' name='btn" + String(id) + "_name' maxlen='19' value='" + String(name) + "'></div>"
            "<div class='f-group'><label>couleur interface</label>"
            "<select name='btn" + String(id) + "_color'>"
            "<option value='green'" + String(strcmp(color,"green")==0?" selected":"") + ">vert</option>"
            "<option value='red'" + String(strcmp(color,"red")==0?" selected":"") + ">rouge</option>"
            "<option value='blue'" + String(strcmp(color,"blue")==0?" selected":"") + ">bleu</option>"
            "<option value='orange'" + String(strcmp(color,"orange")==0?" selected":"") + ">orange</option>"
            "<option value='white'" + String(strcmp(color,"white")==0?" selected":"") + ">gris</option>"
            "</select></div>"
            "</div>"
            "<div class='grid grid-2' style='margin-top:8px;'>"
            "<div class='f-group'><label>topic mqtt (court)</label>"
            "<input type='text' name='btn" + String(id) + "_topic' maxlen='59' value='" + String(topic) + "'></div>"
            "<div class='f-group'><label>payload (court)</label>"
            "<input type='text' name='btn" + String(id) + "_payload' maxlen='99' value='" + String(payload) + "'></div>"
            "</div>"
            "<div class='grid grid-2' style='margin-top:8px;'>"
            "<div class='f-group'><label>topic mqtt (long)</label>"
            "<input type='text' name='btn" + String(id) + "_long_topic' maxlen='59' value='" + String(l_topic) + "'></div>"
            "<div class='f-group'><label>payload (long)</label>"
            "<input type='text' name='btn" + String(id) + "_long_payload' maxlen='99' value='" + String(l_payload) + "'></div>"
            "</div>"
            "<div class='grid' style='margin-top:8px;'>"
            "<div class='f-group'><label>comportement appui long physique</label>"
            "<select name='btn" + String(id) + "_long_mode'>"
            "<option value='both'" + String(strcmp(l_mode,"both")==0?" selected":"") + ">envoi immediat du court + envoi du long si maintenu</option>"
            "<option value='long'" + String(strcmp(l_mode,"long")==0?" selected":"") + ">exclusif (court au relachement rapide, long si maintenu)</option>"
            "</select></div>"
            "</div>";
  };

  gen_btn_fields(1, btn1_name, btn1_topic, btn1_payload, btn1_color, btn1_long_topic, btn1_long_payload, btn1_long_mode);
  gen_btn_fields(2, btn2_name, btn2_topic, btn2_payload, btn2_color, btn2_long_topic, btn2_long_payload, btn2_long_mode);
  gen_btn_fields(3, btn3_name, btn3_topic, btn3_payload, btn3_color, btn3_long_topic, btn3_long_payload, btn3_long_mode);

  html += F("<div class='actions'>"
    "<a href='/' class='btn-back'>retour</a>"
    "<button type='submit' class='btn-save'>sauvegarder &amp; redemarrer</button>"
    "</div>"
    "</form>"
    "</div>"
    "<script>"
    "function toggleEye(id){"
      "const el = document.getElementById(id);"
      "if(el.type==='password'){el.type='text';}else{el.type='password';}"
    "}"
    "</script>"
    "</body></html>");

  server.send(200, "text/html", html);
}

void handle_save() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
  
  strlcpy(wifi_ssid,           server.arg("wifi_ssid").c_str(),           sizeof(wifi_ssid));
  strlcpy(wifi_pass,           server.arg("wifi_pass").c_str(),           sizeof(wifi_pass));
  strlcpy(mqtt_server,         server.arg("mqtt_server").c_str(),         sizeof(mqtt_server));
  strlcpy(mqtt_port,           server.arg("mqtt_port").c_str(),           sizeof(mqtt_port));
  strlcpy(mqtt_user,           server.arg("mqtt_user").c_str(),           sizeof(mqtt_user));
  strlcpy(mqtt_pass,           server.arg("mqtt_pass").c_str(),           sizeof(mqtt_pass));
  strlcpy(esp_hostname,        server.arg("esp_hostname").c_str(),        sizeof(esp_hostname));
  
  strlcpy(btn1_name,           server.arg("btn1_name").c_str(),           sizeof(btn1_name));
  strlcpy(btn1_color,          server.arg("btn1_color").c_str(),          sizeof(btn1_color));
  strlcpy(btn1_topic,          server.arg("btn1_topic").c_str(),          sizeof(btn1_topic));
  strlcpy(btn1_payload,        server.arg("btn1_payload").c_str(),        sizeof(btn1_payload));
  strlcpy(btn1_long_topic,     server.arg("btn1_long_topic").c_str(),     sizeof(btn1_long_topic));
  strlcpy(btn1_long_payload,   server.arg("btn1_long_payload").c_str(),   sizeof(btn1_long_payload));
  strlcpy(btn1_long_mode,      server.arg("btn1_long_mode").c_str(),      sizeof(btn1_long_mode));
  
  strlcpy(btn2_name,           server.arg("btn2_name").c_str(),           sizeof(btn2_name));
  strlcpy(btn2_color,          server.arg("btn2_color").c_str(),          sizeof(btn2_color));
  strlcpy(btn2_topic,          server.arg("btn2_topic").c_str(),          sizeof(btn2_topic));
  strlcpy(btn2_payload,        server.arg("btn2_payload").c_str(),        sizeof(btn2_payload));
  strlcpy(btn2_long_topic,     server.arg("btn2_long_topic").c_str(),     sizeof(btn2_long_topic));
  strlcpy(btn2_long_payload,   server.arg("btn2_long_payload").c_str(),   sizeof(btn2_long_payload));
  strlcpy(btn2_long_mode,      server.arg("btn2_long_mode").c_str(),      sizeof(btn2_long_mode));
  
  strlcpy(btn3_name,           server.arg("btn3_name").c_str(),           sizeof(btn3_name));
  strlcpy(btn3_color,          server.arg("btn3_color").c_str(),          sizeof(btn3_color));
  strlcpy(btn3_topic,          server.arg("btn3_topic").c_str(),          sizeof(btn3_topic));
  strlcpy(btn3_payload,        server.arg("btn3_payload").c_str(),        sizeof(btn3_payload));
  strlcpy(btn3_long_topic,     server.arg("btn3_long_topic").c_str(),     sizeof(btn3_long_topic));
  strlcpy(btn3_long_payload,   server.arg("btn3_long_payload").c_str(),   sizeof(btn3_long_payload));
  strlcpy(btn3_long_mode,      server.arg("btn3_long_mode").c_str(),      sizeof(btn3_long_mode));

  if (server.hasArg("long_press_ms")) {
    long_press_ms = server.arg("long_press_ms").toInt();
  }

  save_config();

  String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta http-equiv='refresh' content='5;url=/'>"
    "<style>body{background:#111827;color:#fff;font-family:sans-serif;text-align:center;padding-top:50px;}</style></head>"
    "<body><h2>configuration enregistree !</h2><p>redemarrage de l'esp8266 en cours...</p></body></html>");
  server.send(200, "text/html", html);
  delay(1000);
  ESP.restart();
}

// ================================================================== gestion connexion wifi

void setup_wifi() {
  WiFi.disconnect(true);
  delay(100);

  if (strlen(esp_hostname) > 0) {
    WiFi.hostname(esp_hostname);
  } else {
    WiFi.hostname("button_mqtt");
  }

  if (strlen(wifi_ssid) == 0) {
    Serial.println("[wifi] aucun ssid configure. bascule en mode point d'acces.");
    is_ap_mode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP_CONFIG_RESEAU", "12345678");
    Serial.print("[wifi] ip du point d'acces : ");
    Serial.println(WiFi.softAPIP());
    net_state = NET_WIFI_KO;
    return;
  }

  Serial.printf("[wifi] tentative de connexion au reseau : %s\n", wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pass);
  last_wifi_attempt = millis();
  net_state = NET_RECONNECTING;
}

void verify_connections() {
  if (is_ap_mode) return;

  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    net_state = NET_WIFI_KO;
    if (now - last_wifi_attempt > wifi_backoff_ms) {
      Serial.println("[wifi] reconnexion sta...");
      WiFi.begin(wifi_ssid, wifi_pass);
      last_wifi_attempt = now;
    }
    return;
  }

  if (net_state == NET_WIFI_KO || net_state == NET_RECONNECTING) {
    Serial.print("[wifi] connecte ! ip: ");
    Serial.println(WiFi.localIP());
    net_state = NET_MQTT_KO;
  }

  if (strlen(mqtt_server) == 0) return;

  if (!mqtt_client.connected()) {
    net_state = NET_MQTT_KO;
    if (now - last_mqtt_attempt > 8000) {
      last_mqtt_attempt = now;
      reconnect_count++;
      Serial.printf("[mqtt] tentative %d de connexion au broker %s...\n", reconnect_count, mqtt_server);
      
      String client_id = "esp8266-client-" + String(random(0, 0xffff), HEX);
      bool ok = false;
      if (strlen(mqtt_user) > 0) {
        ok = mqtt_client.connect(client_id.c_str(), mqtt_user, mqtt_pass);
      } else {
        ok = mqtt_client.connect(client_id.c_str());
      }

      if (ok) {
        Serial.println("[mqtt] connecte avec succes au broker.");
        net_state = NET_OK;
        reconnect_count = 0;
      } else {
        Serial.printf("[mqtt] echec, rc=%d\n", mqtt_client.state());
      }
    }
  } else {
    net_state = NET_OK;
  }
}

// ================================================================== setup & loop de base

void setup() {
  Serial.begin(115200);
  Serial.println("\n[systeme] demarrage initial...");

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  pinMode(btn1_pin, INPUT_PULLUP);
  pinMode(btn2_pin, INPUT_PULLUP);
  pinMode(btn3_pin, INPUT_PULLUP);

  load_config();
  setup_wifi();

  if (strlen(mqtt_server) > 0) {
    mqtt_client.setServer(mqtt_server, atoi(mqtt_port));
  }

  server.on("/", handle_root);
  server.on("/setup", handle_setup);
  server.on("/save", handle_save);
  server.on("/click", handle_click);

  httpUpdater.setup(&server, "/update");
  server.begin();
  Serial.println("[web] serveur HTTP pret.");
}

void loop() {
  server.handleClient();
  
  if (!is_ap_mode) {
    mqtt_client.loop();
    verify_connections();
  }

  update_led();

  // ------------------------------------------------------------------ lecture des boutons physiques
  unsigned long now = millis();
  bool r1 = digitalRead(btn1_pin);
  bool r2 = digitalRead(btn2_pin);
  bool r3 = digitalRead(btn3_pin);

  // ---- Bouton 1 (D1)
  if (r1 != last_btn1_state) { last_debounce_time1 = now; last_btn1_state = r1; }
  if ((now - last_debounce_time1) > debounce_delay) {
    if (r1 == LOW) {
      if (!pressed1) {
        pressed1     = true;
        long_fired1  = false;
        press_start1 = now;
        if (strcmp(btn1_long_mode, "long") != 0) {
          Serial.println("[action] D1 court");
          publish_if_connected(btn1_topic, btn1_payload, "btn1-court");
        }
      }
      if (!long_fired1 && (now - press_start1) >= long_press_ms) {
        long_fired1 = true;
        Serial.println("[action] D1 long");
        publish_if_connected(btn1_long_topic, btn1_long_payload, "btn1-long");
      }
    } else {
      if (pressed1 && !long_fired1 && strcmp(btn1_long_mode, "long") == 0) {
        Serial.println("[action] D1 court (mode long-seul)");
        publish_if_connected(btn1_topic, btn1_payload, "btn1-court");
      }
      pressed1 = false;
    }
  }

  // ---- Bouton 2 (D2)
  if (r2 != last_btn2_state) { last_debounce_time2 = now; last_btn2_state = r2; }
  if ((now - last_debounce_time2) > debounce_delay) {
    if (r2 == LOW) {
      if (!pressed2) {
        pressed2     = true;
        long_fired2  = false;
        press_start2 = now;
        if (strcmp(btn2_long_mode, "long") != 0) {
          Serial.println("[action] D2 court");
          publish_if_connected(btn2_topic, btn2_payload, "btn2-court");
        }
      }
      if (!long_fired2 && (now - press_start2) >= long_press_ms) {
        long_fired2 = true;
        Serial.println("[action] D2 long");
        publish_if_connected(btn2_long_topic, btn2_long_payload, "btn2-long");
      }
    } else {
      if (pressed2 && !long_fired2 && strcmp(btn2_long_mode, "long") == 0) {
        Serial.println("[action] D2 court (mode long-seul)");
        publish_if_connected(btn2_topic, btn2_payload, "btn2-court");
      }
      pressed2 = false;
    }
  }

  // ---- Bouton 3 (D5)
  if (r3 != last_btn3_state) { last_debounce_time3 = now; last_btn3_state = r3; }
  if ((now - last_debounce_time3) > debounce_delay) {
    if (r3 == LOW) {
      if (!pressed3) {
        pressed3     = true;
        long_fired3  = false;
        press_start3 = now;
        if (strcmp(btn3_long_mode, "long") != 0) {
          Serial.println("[action] D5 court");
          publish_if_connected(btn3_topic, btn3_payload, "btn3-court");
        }
      }
      if (!long_fired3 && (now - press_start3) >= long_press_ms) {
        long_fired3 = true;
        Serial.println("[action] D5 long");
        publish_if_connected(btn3_long_topic, btn3_long_payload, "btn3-long");
      }
    } else {
      if (pressed3 && !long_fired3 && strcmp(btn3_long_mode, "long") == 0) {
        Serial.println("[action] D5 court (mode long-seul)");
        publish_if_connected(btn3_topic, btn3_payload, "btn3-court");
      }
      pressed3 = false;
    }
  }
}
