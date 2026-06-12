#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
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
char wifi_ssid_backup[32] = "";
char wifi_pass_backup[64] = "";
char mqtt_server[40]= "";
char mqtt_port[6]   = "1883";
char mqtt_user[40]  = "";
char mqtt_pass[40]  = "";

// bouton 1
char btn1_topic[60]        = "esp/btn1/topic";
char btn1_payload[100]     = "message_btn1";
char btn1_name[20]         = "up";
char btn1_color[10]        = "green";
char btn1_long_topic[60]   = "";
char btn1_long_payload[100]= "";
char btn1_long_mode[6]     = "both";   // "both" = court+long, "long" = long seul

// bouton 2
char btn2_topic[60]        = "esp/btn2/topic";
char btn2_payload[100]     = "message_btn2";
char btn2_name[20]         = "my";
char btn2_color[10]        = "white";
char btn2_long_topic[60]   = "";
char btn2_long_payload[100]= "";
char btn2_long_mode[6]     = "both";

// bouton 3
char btn3_topic[60]        = "esp/btn3/topic";
char btn3_payload[100]     = "message_btn3";
char btn3_name[20]         = "down";
char btn3_color[10]        = "red";
char btn3_long_topic[60]   = "";
char btn3_long_payload[100]= "";
char btn3_long_mode[6]     = "both";

// seuil appui long en ms (configurable)
unsigned int long_press_ms = 800;

// variables d'amelioration : intervalles de temps et suivi
unsigned long last_ping_ms = 0;
const unsigned long ping_interval_ms = 60000; // ping toutes les minutes
bool using_backup_wifi = false;

// ------------------------------------------------------------------ etat reseau
enum netstate { net_ok, net_mqtt_ko, net_wifi_ko, net_reconnecting };
netstate net_state = net_reconnecting;

unsigned long last_wifi_attempt  = 0;
unsigned long last_mqtt_attempt  = 0;
unsigned long wifi_backoff_ms    = 5000;   // demarre a 5s, monte jusqu'a 30s
unsigned int  reconnect_count    = 0;

// clignotement led non bloquant
unsigned long last_led_toggle    = 0;
bool          led_state          = false;

// ------------------------------------------------------------------ debounce
bool last_btn1_state = HIGH;
bool last_btn2_state = HIGH;
bool last_btn3_state = HIGH;
unsigned long last_debounce_time1 = 0;
unsigned long last_debounce_time2 = 0;
unsigned long last_debounce_time3 = 0;
const unsigned long debounce_delay = 50;

// timestamps d'appui pour detection appui long
unsigned long press_start1 = 0;
unsigned long press_start2 = 0;
unsigned long press_start3 = 0;
bool long_fired1 = false;
bool long_fired2 = false;
bool long_fired3 = false;

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

// ================================================================== config json

void load_config() {
    if (!LittleFS.begin()) return;
    if (!LittleFS.exists("/config.json")) return;
    File f = LittleFS.open("/config.json", "r");
    if (!f) return;
    StaticJsonDocument<2048> json;
    if (deserializeJson(json, f) == DeserializationError::Ok) {
        strlcpy(wifi_ssid,           json["wifi_ssid"]           | "",              sizeof(wifi_ssid));
        strlcpy(wifi_pass,           json["wifi_pass"]           | "",              sizeof(wifi_pass));
        strlcpy(wifi_ssid_backup,    json["wifi_ssid_bk"]        | "",              sizeof(wifi_ssid_backup));
        strlcpy(wifi_pass_backup,    json["wifi_pass_bk"]        | "",              sizeof(wifi_pass_backup));
        strlcpy(mqtt_server,         json["mqtt_server"]         | "",              sizeof(mqtt_server));
        strlcpy(mqtt_port,           json["mqtt_port"]           | "1883",          sizeof(mqtt_port));
        strlcpy(mqtt_user,           json["mqtt_user"]           | "",              sizeof(mqtt_user));
        strlcpy(mqtt_pass,           json["mqtt_pass"]           | "",              sizeof(mqtt_pass));
        strlcpy(btn1_topic,          json["btn1_topic"]          | "esp/btn1/topic",sizeof(btn1_topic));
        strlcpy(btn1_payload,        json["btn1_payload"]        | "message_btn1",  sizeof(btn1_payload));
        strlcpy(btn1_name,           json["btn1_name"]           | "up",            sizeof(btn1_name));
        strlcpy(btn1_color,          json["btn1_color"]          | "green",         sizeof(btn1_color));
        strlcpy(btn1_long_topic,     json["btn1_long_topic"]     | "",              sizeof(btn1_long_topic));
        strlcpy(btn1_long_payload,   json["btn1_long_payload"]   | "",              sizeof(btn1_long_payload));
        strlcpy(btn1_long_mode,      json["btn1_long_mode"]      | "both",          sizeof(btn1_long_mode));
        strlcpy(btn2_topic,          json["btn2_topic"]          | "esp/btn2/topic",sizeof(btn2_topic));
        strlcpy(btn2_payload,        json["btn2_payload"]        | "message_btn2",  sizeof(btn2_payload));
        strlcpy(btn2_name,           json["btn2_name"]           | "my",            sizeof(btn2_name));
        strlcpy(btn2_color,          json["btn2_color"]          | "white",         sizeof(btn2_color));
        strlcpy(btn2_long_topic,     json["btn2_long_topic"]     | "",              sizeof(btn2_long_topic));
        strlcpy(btn2_long_payload,   json["btn2_long_payload"]   | "",              sizeof(btn2_long_payload));
        strlcpy(btn2_long_mode,      json["btn2_long_mode"]      | "both",          sizeof(btn2_long_mode));
        strlcpy(btn3_topic,          json["btn3_topic"]          | "esp/btn3/topic",sizeof(btn3_topic));
        strlcpy(btn3_payload,        json["btn3_payload"]        | "message_btn3",  sizeof(btn3_payload));
        strlcpy(btn3_name,           json["btn3_name"]           | "down",          sizeof(btn3_name));
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
    json["wifi_ssid_bk"]      = wifi_ssid_backup;
    json["wifi_pass_bk"]      = wifi_pass_backup;
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

// ================================================================== led statut

void update_led() {
    unsigned long now = millis();
    unsigned long interval = 0;

    switch (net_state) {
        case net_ok:           interval = 0; break;
        case net_mqtt_ko:      interval = 800;  break;
        case net_wifi_ko:      interval = 150;  break;
        case net_reconnecting: interval = 300;  break;
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

void handle_status_api() {
    StaticJsonDocument<256> json;
    json["wifi"] = (WiFi.status() == WL_CONNECTED);
    json["mqtt"] = mqtt_client.connected();
    json["ip"] = WiFi.localIP().toString();
    json["recon"] = reconnect_count;
    json["uptime"] = millis() / 1000;
    
    String response;
    serializeJson(json, response);
    server.send(200, "application/json", response);
}

void handle_root() {
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);
    bool mqtt_ok = mqtt_client.connected();

    String html = "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>t&eacute;l&eacute;commande</title>"
        "<style>"
        "body{margin:0;padding:0;background:#111827;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;display:flex;flex-direction:column;align-items:center;min-height:100vh;}"
        ".card{width:92%;max-width:340px;margin:0 auto;}"
        ".hdr{padding:14px 0 10px;width:92%;max-width:340px;display:flex;flex-direction:column;align-items:center;gap:6px;}"
        ".status-row{display:flex;gap:8px;}"
        ".pill{display:flex;align-items:center;gap:5px;font-size:12px;padding:4px 10px;border-radius:99px;font-weight:500;transition:all 0.3s;}"
        ".pill-ok{background:#0d2b1a;color:#52c98a;border:1px solid #1e5c33;}"
        ".pill-err{background:#2b0d0d;color:#e05555;border:1px solid #5c1e1e;}"
        ".dot{width:7px;height:7px;border-radius:50%;transition:all 0.3s;}"
        ".dot-ok{background:#52c98a;}"
        ".dot-err{background:#e05555;}"
        ".ip{font-size:11px;color:#4b5563;margin-top:2px;}"
        ".recon{font-size:10px;color:#374151;margin-top:1px;}"
        ".btns{display:flex;flex-direction:column;gap:12px;padding:8px 0 16px;width:100%;}"
        ".btn{width:100%;padding:22px 0;border-radius:14px;border:none;cursor:pointer;font-size:24px;font-weight:500;letter-spacing:0.02em;display:flex;flex-direction:column;align-items:center;gap:4px;box-sizing:border-box;transition:all 0.1s;}"
        ".btn-icon{font-size:13px;opacity:0.5;}"
        ".btn:active{opacity:0.75;transform:scale(0.98);}"
        ".footer{padding:12px 0 24px;border-top:1px solid #1f2937;width:100%;text-align:center;}"
        ".footer a{color:#374151;font-size:13px;text-decoration:none;}"
        "#toast{visibility:hidden;min-width:200px;background-color:#333;color:#fff;text-align:center;border-radius:8px;padding:12px;position:fixed;z-index:20;bottom:30px;font-size:14px;box-shadow:0 4px 12px rgba(0,0,0,0.5);opacity:0;transition:opacity 0.3s, visibility 0.3s;}"
        "#toast.show{visibility:visible;opacity:1;}"
        "</style></head><body>";
    
    html += "<div class='hdr'><div class='status-row'>";
    html += "<div id='wifi-pill' class='pill " + String(wifi_ok ? "pill-ok" : "pill-err") + "'>";
    html += "<div id='wifi-dot' class='dot " + String(wifi_ok ? "dot-ok" : "dot-err") + "'></div>WiFi</div>";
    html += "<div id='mqtt-pill' class='pill " + String(mqtt_ok ? "pill-ok" : "pill-err") + "'>";
    html += "<div id='mqtt-dot' class='dot " + String(mqtt_ok ? "dot-ok" : "dot-err") + "'></div>MQTT</div>";
    html += "</div>";
    
    html += "<div id='ip-val' class='ip'>";
    if (wifi_ok) html += WiFi.localIP().toString();
    html += "</div>";
    
    html += "<div id='recon-val' class='recon'>";
    if (reconnect_count > 0) html += "reconnexions : " + String(reconnect_count);
    html += "</div></div>";
    
    html += "<div class='card'><div class='btns'>";
    html += "<button class='btn' style='" + color_styles(btn1_color) + "' onclick='pub(1)'>";
    html += "<span class='btn-icon'>" + btn_icon(1) + "</span><span>" + String(btn1_name) + "</span></button>";
    html += "<button class='btn' style='" + color_styles(btn2_color) + "' onclick='pub(2)'>";
    html += "<span class='btn-icon'>" + btn_icon(2) + "</span><span>" + String(btn2_name) + "</span></button>";
    html += "<button class='btn' style='" + color_styles(btn3_color) + "' onclick='pub(3)'>";
    html += "<span class='btn-icon'>" + btn_icon(3) + "</span><span>" + String(btn3_name) + "</span></button>";
    html += "</div>";
    
    html += "<div class='footer'><a href='/setup'>&#9881; configuration</a></div>";
    html += "</div><div id='toast'></div>";
    
    html += "<script>"
        "function showtoast(msg,iserr){"
            "var t=document.getElementById('toast');t.textContent=msg;"
            "t.style.border=iserr?'1px solid #5c1e1e':'1px solid #1e5c33';"
            "t.style.background=iserr?'#2b0d0d':'#0d2b1a';"
            "t.className='show';"
            "setTimeout(function(){t.className=t.className.replace('show','');},2500);"
        "}"
        "function pub(id){"
            "var x=new XMLHttpRequest();"
            "x.open('GET','/trigger?id='+id,true);"
            "x.onload=function(){"
                "if(x.status==200){showtoast('trame envoy\u00e9e avec succ\u00e8s',false);}"
                "else{showtoast('\u00e9chec de l\\'envoi : '+x.responseText,true);}"
            "};"
            "x.onerror=function(){showtoast('erreur r\u00e9seau local',true);};"
            "x.send();"
        "}"
        "function checkstatus(){"
            "var x=new XMLHttpRequest();x.open('GET','/api/status',true);"
            "x.onload=function(){"
                "if(x.status==200){"
                    "var r=JSON.parse(x.responseText);"
                    "var wp=document.getElementById('wifi-pill'),wd=document.getElementById('wifi-dot');"
                    "var mp=document.getElementById('mqtt-pill'),md=document.getElementById('mqtt-dot');"
                    "if(r.wifi){wp.className='pill pill-ok';wd.className='dot dot-ok';}else{wp.className='pill pill-err';wd.className='dot dot-err';}"
                    "if(r.mqtt){mp.className='pill pill-ok';md.className='dot dot-ok';}else{mp.className='pill pill-err';md.className='dot dot-err';}"
                    "document.getElementById('ip-val').textContent=r.wifi?r.ip:'';"
                    "document.getElementById('recon-val').textContent=r.recon>0?'reconnexions : '+r.recon:'';"
                "}"
            "};"
            "x.send();"
        "}"
        "setInterval(checkstatus,5000);"
        "</script></body></html>";
    
    server.send(200, "text/html", html);
}

// ---------- helpers setup ----------

String color_option(const char* val, const char* lbl, const char* cur) {
    return "<option value='" + String(val) + "'" + (strcmp(val, cur) == 0 ? " selected" : "") + ">" + String(lbl) + "</option>";
}

String btn_card(int n, const char* pin_lbl, const char* name, const char* color, const char* topic, const char* payload, const char* ltopic, const char* lpayload, const char* lmode) {
    String p   = String(n);
    String dot = "dot" + p;
    String s = "";
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

    s += "<div class='row'><label>topic court</label><input type='text' name='b" + p + "_t' value='" + String(topic) + "'></div>";
    s += "<div class='row'><label>payload court</label><input type='text' name='b" + p + "_p' value='" + String(payload) + "'></div>";
    s += "<div class='long-hdr'>&#128336; appui long</div>";
    s += "<div class='row'><label>topic long <span class='opt'>(optionnel)</span></label><input type='text' name='b" + p + "_lt' value='" + String(ltopic) + "' placeholder='laisser vide pour d&eacute;sactiver'></div>";
    s += "<div class='row'><label>payload long</label><input type='text' name='b" + p + "_lp' value='" + String(lpayload) + "'></div>";
    s += "<div class='row'><label>mode</label><select name='b" + p + "_lm'>";
    s += "<option value='both'" + String(strcmp(lmode,"both")==0?" selected":"") + ">court + long (deux actions)</option>";
    s += "<option value='long'" + String(strcmp(lmode,"long")==0?" selected":"") + ">long seul (le court attend le rel&acirc;chement)</option>";
    s += "</select></div>";
    s += "</div>";
    return s;
}

void handle_setup() {
    int n = WiFi.scanNetworks();
    String html = "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>configuration</title>"
        "<style>"
        "body{margin:0;padding:0 0 40px;background:#f5f5f5;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;color:#111;}"
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
        ".pass-wrapper{position:relative;display:flex;align-items:center;}"
        ".eye-btn{position:absolute;right:10px;background:none;border:none;cursor:pointer;color:#6b7280;font-size:16px;padding:0;z-index:2;}"
        "</style></head><body>";
    
    html += "<div class='topbar'><a class='back' href='/'>&#8592; retour</a><span class='topbar-title'>Configuration</span><span></span></div>";

    // ---- ① Réseau principal et de secours ----
    html += "<div class='section'><div class='sec-hdr sec-net'>&#9312; r&eacute;seaux wifi</div>";
    html += "<div class='row'><label>R&eacute;seau d&eacute;tect&eacute; (principal)</label><select name='wifi_ssid' id='ssid_sel'>";
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
    
    html += "<div class='row'><label>Mot de passe principal</label><div class='pass-wrapper'><input type='password' name='wifi_pass' id='wp' value='" + String(wifi_pass) + "'><button type='button' class='eye-btn' onclick='tgl(\"wp\")'>&#128065;</button></div></div>";
    
    html += "<div class='row'><label>SSID de secours (optionnel)</label><input type='text' name='wifi_ssid_bk' value='" + String(wifi_ssid_backup) + "' placeholder='ex: partage de connexion'></div>";
    html += "<div class='row'><label>Mot de passe de secours</label><div class='pass-wrapper'><input type='password' name='wifi_pass_bk' id='wp_bk' value='" + String(wifi_pass_backup) + "'><button type='button' class='eye-btn' onclick='tgl(\"wp_bk\")'>&#128065;</button></div></div>";
    html += "</div>";

    // ---- ② MQTT ----
    html += "<div class='section'><div class='sec-hdr sec-mqtt'>&#9313; broker mqtt</div>";
    html += "<div class='grid2'>";
    html += "<div class='row'><label>Serveur</label><input type='text' name='mqtt_server' value='" + String(mqtt_server) + "'></div>";
    html += "<div class='row'><label>Port</label><input type='text' name='mqtt_port' value='" + String(mqtt_port) + "'></div>";
    html += "</div><div class='grid2'>";
    html += "<div class='row'><label>Login</label><input type='text' name='mqtt_user' value='" + String(mqtt_user) + "'></div>";
    html += "<div class='row'><label>Mot de passe</label><div class='pass-wrapper'><input type='password' name='mqtt_pass' id='mqp' value='" + String(mqtt_pass) + "'><button type='button' class='eye-btn' onclick='tgl(\"mqp\")'>&#128065;</button></div></div>";
    html += "</div></div>";

    // ---- ③ Boutons ----
    html += "<div class='section'><div class='sec-hdr sec-btns'>&#9314; boutons</div>";
    html += btn_card(1, "D1", btn1_name, btn1_color, btn1_topic, btn1_payload, btn1_long_topic, btn1_long_payload, btn1_long_mode);
    html += btn_card(2, "D2", btn2_name, btn2_color, btn2_topic, btn2_payload, btn2_long_topic, btn2_long_payload, btn2_long_mode);
    html += btn_card(3, "D5", btn3_name, btn3_color, btn3_topic, btn3_payload, btn3_long_topic, btn3_long_payload, btn3_long_mode);
    html += "</div>";

    // ---- ④ Robustesse ----
    html += "<div class='section'><div class='sec-hdr sec-rob'>&#9315; robustesse r&eacute;seau</div>";
    html += "<div class='row'><label>Seuil appui long (ms)</label><input type='number' name='long_ms' value='" + String(long_press_ms) + "' min='300' max='3000'></div>";
    html += "<div class='row'><label style='color:#6b7280;font-size:12px;'>LED de statut sur D4 (onboard) :</label>";
    html += "<div style='font-size:12px;color:#374151;background:#f3f4f6;border-radius:6px;padding:8px 10px;'>";
    html += "&#9679; WiFi+MQTT OK &rarr; LED &eacute;teinte<br>";
    html += "&#9679; MQTT d&eacute;connect&eacute; &rarr; clignotement lent (0.6 Hz)<br>";
    html += "&#9679; WiFi coup&eacute; &rarr; clignotement rapide (3 Hz)<br>";
    html += "&#9679; Reconnexion &rarr; clignotement moyen (1.6 Hz)";
    html += "</div></div></div>";

    // ---- ⑤ OTA ----
    if (!is_ap_mode) {
        html += "<div class='section'><div class='sec-hdr sec-ota'>&#9316; mise &agrave; jour firmware</div>";
        html += "<p style='font-size:13px;color:#6b7280;margin:0 0 12px;'>Chargez un fichier <b>.bin</b> g&eacute;n&eacute;r&eacute; par PlatformIO ou Arduino IDE.</p>";
        html += "<div id='ota-area'>";
        html += "<div style='display:flex;align-items:center;gap:10px;flex-wrap:wrap;'>";
        html += "<label for='ota-file' style='display:inline-flex;align-items:center;gap:6px;padding:9px 14px;"
                  "background:#f0f4ff;border:1px solid #c7d7fd;border-radius:8px;cursor:pointer;font-size:13px;color:#3b5bdb;'>"
                  "&#128193; choisir un .bin</label>";
        html += "<input type='file' id='ota-file' accept='.bin' style='display:none;'>";
        html += "<span id='ota-fname' style='font-size:12px;color:#9ca3af;'>aucun fichier s&eacute;lectionn&eacute;</span>";
        html += "</div>";
        html += "<div id='ota-progress-wrap' style='display:none;margin-top:12px;'>";
        html += "<div style='background:#e5e7eb;border-radius:99px;height:8px;overflow:hidden;'>";
        html += "<div id='ota-bar' style='background:#3b82f6;height:8px;width:0%;transition:width 0.3s;'></div></div>";
        html += "<div id='ota-status' style='font-size:12px;color:#6b7280;margin-top:6px;text-align:center;'>0%</div></div>";
        html += "<button id='ota-btn' onclick='doOta()' style='margin-top:12px;width:100%;padding:11px;background:#3b82f6;"
                  "color:#fff;border:none;border-radius:8px;font-size:14px;font-weight:500;cursor:pointer;'>";
        html += "&#8593; flasher le firmware</button>";
        html += "</div></div>";
    }

    // ---- Bouton save ----
    html += "<button class='save' onclick='doSave()'>&#128190; sauvegarder et red&eacute;marrer</button>";
    
    // ---- Script ----
    html += "<script>"
        "var colorMap={green:'#52c98a',white:'#cccccc',red:'#e05555',blue:'#5b8dee',orange:'#e09b40'};"
        "function upd(sel,dotId){var d=document.getElementById(dotId);if(d)d.style.background=colorMap[sel.value]||'#ccc';}"
        "function tgl(id){var el=document.getElementById(id);if(el.type==='password'){el.type='text';}else{el.type='password';}}"

        // Affichage nom fichier OTA
        "var fi=document.getElementById('ota-file');"
        "if(fi)fi.addEventListener('change',function(){"
            "var fn=document.getElementById('ota-fname');"
            "if(fn)fn.textContent=this.files[0]?this.files[0].name:'aucun fichier s\u00e9lectionn\u00e9';"
        "});"

        // OTA upload
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
            "onerror=function(){"
                "document.getElementById('ota-status').textContent='Erreur r\u00e9seau';"
                "document.getElementById('ota-btn').disabled=false;"
                "document.getElementById('ota-btn').textContent='\u2191 flasher le firmware';"
            "};"
            "x.send(fd);"
        "}"

        // Save config
        "function doSave(){"
            "var params=["
                "'wifi_ssid='+encodeURIComponent(document.getElementById('ssid_sel').value),"
                "'wifi_pass='+encodeURIComponent(document.getElementById('wp').value)"
            "];"
            "var names=['wifi_ssid_bk','wifi_pass_bk','mqtt_server','mqtt_port','mqtt_user','mqtt_pass','long_ms',"
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
        "</script>";

    html += "</body></html>";
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
    strlcpy(wifi_ssid_backup,    server.arg("wifi_ssid_bk").c_str(),sizeof(wifi_ssid_backup));
    strlcpy(wifi_pass_backup,    server.arg("wifi_pass_bk").c_str(),sizeof(wifi_pass_backup));
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
    String html = "<!DOCTYPE html><html><head>"
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
        "</div></body></html>";
    server.begin();
    server.send(200, "text/html", html);
}

// ================================================================== routage

void setup_routing() {
    if (is_ap_mode) server.on("/", handle_ap_root);
    else            server.on("/", handle_root);
    server.on("/setup",   handle_setup);
    server.on("/trigger", handle_trigger);
    server.on("/save",    HTTP_POST, handle_save);
    server.on("/api/status", handle_status_api);
    httpupdater.setup(&server, "/update");
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
    pinMode(pin_led,  OUTPUT);
    digitalWrite(pin_led, HIGH);  // éteinte par défaut (active low)

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
            using_backup_wifi = false;
            Serial.print("[wifi] ip principal : "); Serial.println(WiFi.localIP());
            mqtt_client.setServer(mqtt_server, atoi(mqtt_port));
            net_state = net_reconnecting;
            setup_routing();
        } else if (strlen(wifi_ssid_backup) > 0) {
            Serial.println("[wifi] echec principal, bascule sur le reseau de secours...");
            WiFi.begin(wifi_ssid_backup, wifi_pass_backup);
            timeout = 0;
            while (WiFi.status() != WL_CONNECTED && timeout < 40) {
                delay(500); Serial.print("."); timeout++;
            }
            if (WiFi.status() == WL_CONNECTED) {
                is_ap_mode = false;
                using_backup_wifi = true;
                Serial.print("[wifi] ip secours : "); Serial.println(WiFi.localIP());
                mqtt_client.setServer(mqtt_server, atoi(mqtt_port));
                net_state = net_reconnecting;
                setup_routing();
            } else {
                net_state = net_wifi_ko;
                setup_ap();
            }
        } else {
            net_state = net_wifi_ko;
            setup_ap();
        }
    } else {
        net_state = net_wifi_ko;
        setup_ap();
    }
}

// ================================================================== gestion réseau

void manage_network() {
    unsigned long now = millis();

    // redémarrage préventif de sécurité automatique tous les 7 jours (604800 secondes)
    if (now > 604800000) {
        Serial.println("[systeme] redemarrage automatique preventif apres 7 jours...");
        delay(500);
        ESP.restart();
    }

    // -- WiFi perdu --
    if (WiFi.status() != WL_CONNECTED) {
        if (net_state != net_wifi_ko && net_state != net_reconnecting) {
            Serial.println("[wifi] connexion perdue");
            reconnect_count++;
            net_state = net_wifi_ko;
            last_wifi_attempt = 0;
            wifi_backoff_ms   = 5000;
        }
        // Tentative de reconnexion WiFi avec backoff alterné principal / secours
        if (now - last_wifi_attempt >= wifi_backoff_ms) {
            last_wifi_attempt = now;
            net_state = net_reconnecting;
            WiFi.disconnect();
            
            if (strlen(wifi_ssid_backup) > 0 && !using_backup_wifi) {
                Serial.println("[wifi] tentative sur reseau de secours...");
                WiFi.begin(wifi_ssid_backup, wifi_pass_backup);
                using_backup_wifi = true;
            } else {
                Serial.println("[wifi] tentative sur reseau principal...");
                WiFi.begin(wifi_ssid, wifi_pass);
                using_backup_wifi = false;
            }
            wifi_backoff_ms = min((unsigned long)30000, wifi_backoff_ms * 2);
        }
        return;
    }

    // WiFi OK — reset backoff
    wifi_backoff_ms = 5000;

    // -- MQTT déconnecté --
    if (!mqtt_client.connected()) {
        if (net_state == net_ok) {
            Serial.println("[mqtt] connexion perdue");
            reconnect_count++;
        }
        net_state = net_mqtt_ko;

        if (now - last_mqtt_attempt >= 5000) {
            last_mqtt_attempt = now;
            Serial.println("[mqtt] tentative reconnexion...");
            String clientId = "esp-btn-" + String(ESP.getChipId(), HEX);
            
            // configuration du testament lwt avant connexion
            bool ok = false;
            if (strlen(mqtt_user) > 0) {
                ok = mqtt_client.connect(clientId.c_str(), mqtt_user, mqtt_pass, "esp/status/testament", 1, true, "offline");
            } else {
                ok = mqtt_client.connect(clientId.c_str(), "esp/status/testament", 1, true, "offline");
            }
            
            if (ok) {
                Serial.println("[mqtt] connecte");
                mqtt_client.publish("esp/status/testament", "online", true);
                net_state = net_ok;
            } else {
                Serial.print("[mqtt] erreur code="); Serial.println(mqtt_client.state());
            }
        }
        return;
    }

    // Tout OK — gestion du ping régulier
    net_state = net_ok;
    mqtt_client.loop();
    
    if (now - last_ping_ms >= ping_interval_ms) {
        last_ping_ms = now;
        String ping_topic = "esp-btn-" + String(ESP.getChipId(), HEX) + "/ping";
        StaticJsonDocument<128> ping_json;
        ping_json["uptime"] = now / 1000;
        ping_json["rssi"] = WiFi.RSSI();
        
        char buffer[128];
        serializeJson(ping_json, buffer);
        mqtt_client.publish(ping_topic.c_str(), buffer);
    }
}

// ================================================================== boutons physiques

void publish_if_connected(const char* topic, const char* payload, const char* label) {
    if (strlen(topic) == 0) return;
    if (!mqtt_client.connected()) { Serial.println("[mqtt] non connecte, commande ignoree"); return; }
    if (mqtt_client.publish(topic, payload)) {
        String log_msg = "[mqtt] " + String(label) + " envoye";
        Serial.println(log_msg);
    } else {
        String log_msg = "[mqtt] echec envoi " + String(label);
        Serial.println(log_msg);
    }
}

void check_buttons() {
    int r1 = digitalRead(btn1_pin);
    int r2 = digitalRead(btn2_pin);
    int r3 = digitalRead(btn3_pin);
    unsigned long now = millis();

    static bool pressed1 = false, pressed2 = false, pressed3 = false;
    
    // ---- Bouton 1 (D1) ----
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
                Serial.println("[action] D1 court (mode long-seul, relachement rapide)");
                publish_if_connected(btn1_topic, btn1_payload, "btn1-court");
            }
            pressed1 = false;
        }
    }

    // ---- Bouton 2 (D2) ----
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
                Serial.println("[action] D2 court (mode long-seul, relachement rapide)");
                publish_if_connected(btn2_topic, btn2_payload, "btn2-court");
            }
            pressed2 = false;
        }
    }

    // ---- Bouton 3 (D5) ----
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
                Serial.println("[action] D5 court (mode long-seul, relachement rapide)");
                publish_if_connected(btn3_topic, btn3_payload, "btn3-court");
            }
            pressed3 = false;
        }
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
