// Secador de toalhas — firmware do ESP32-C3 SuperMini na placa rev A.
// Liga/desliga as 6 barras juntas (um triac), com modo normal, modo eco, timer,
// 2 horários diários e página web. A decisão de aquecer fica em control.h.
#include <Arduino.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <TM1637Display.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>
#include "control.h"

// Pinos da placa rev A. PIN_DN é o pino 3 da fileira B do módulo: IO7 ou IO10,
// dependendo do fabricante. Confira a serigrafia do seu SuperMini e ajuste aqui.
const int PIN_TRIAC = 4, PIN_NTC = 0, PIN_CLK = 3, PIN_DIO = 2, PIN_UP = 1, PIN_DN = 7, PIN_LED = 8;
const char* TZ_BRASILIA = "<-03>3";

struct Slot { bool on; uint8_t h, m; uint16_t minutes; Mode mode; };

TM1637Display disp(PIN_CLK, PIN_DIO);
WebServer web(80);
Preferences prefs;
Controller ctl;
Settings cfg;
Slot slots[2] = {{false, 6, 0, 90, Mode::Normal}, {false, 18, 0, 90, Mode::Normal}};
String wifiSsid, wifiPass, adminPass;

Mode mode = Mode::Off;          // sempre liga desligado depois de faltar energia
uint16_t autoOffMin = 180;      // desligamento automático ao ligar pelo botão/web (0 = nunca)
bool timerOn = false;
uint32_t timerStartMs = 0, timerMs = 0;
float tempC = NAN;
bool heaterOn = false, apStarted = false, mdnsStarted = false, prefsDirty = false;
uint32_t prefsDirtyAt = 0, overlayUntil = 0, lastControl = 0;
enum class Overlay { None, Setpoint, Timer } overlay = Overlay::None;

// ---------- configurações salvas ----------
void loadPrefs() {
  prefs.begin("secador", true);
  cfg.setpointC = prefs.getFloat("sp", 55);
  cfg.hystC = prefs.getFloat("hy", 3);
  cfg.offsetC = prefs.getFloat("of", 0);
  autoOffMin = prefs.getUShort("auto", 180);
  for (int i = 0; i < 2; i++) {
    String k = "s" + String(i);
    slots[i].on = prefs.getBool((k + "on").c_str(), slots[i].on);
    slots[i].h = prefs.getUChar((k + "h").c_str(), slots[i].h);
    slots[i].m = prefs.getUChar((k + "m").c_str(), slots[i].m);
    slots[i].minutes = prefs.getUShort((k + "d").c_str(), slots[i].minutes);
    slots[i].mode = (Mode)prefs.getUChar((k + "md").c_str(), (uint8_t)slots[i].mode);
  }
  wifiSsid = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  adminPass = prefs.getString("admin", "secador123");
  prefs.end();
}

void savePrefs() {
  prefs.begin("secador", false);
  prefs.putFloat("sp", cfg.setpointC);
  prefs.putFloat("hy", cfg.hystC);
  prefs.putFloat("of", cfg.offsetC);
  prefs.putUShort("auto", autoOffMin);
  for (int i = 0; i < 2; i++) {
    String k = "s" + String(i);
    prefs.putBool((k + "on").c_str(), slots[i].on);
    prefs.putUChar((k + "h").c_str(), slots[i].h);
    prefs.putUChar((k + "m").c_str(), slots[i].m);
    prefs.putUShort((k + "d").c_str(), slots[i].minutes);
    prefs.putUChar((k + "md").c_str(), (uint8_t)slots[i].mode);
  }
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);
  prefs.putString("admin", adminPass);
  prefs.end();
}

void markDirty() { prefsDirty = true; prefsDirtyAt = millis(); }  // grava 3 s depois: poupa a flash

// ---------- modo e timer ----------
void setMode(Mode m, uint16_t minutes) {
  mode = m;
  timerOn = m != Mode::Off && minutes > 0;
  timerStartMs = millis();
  timerMs = minutes * 60000UL;
}

int minutesLeft() {
  if (!timerOn) return -1;
  uint32_t elapsed = millis() - timerStartMs;
  return elapsed >= timerMs ? 0 : (timerMs - elapsed + 59999) / 60000;
}

// ---------- sensor ----------
float readTempC() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(PIN_NTC);
  float c = ntcCelsius(mv / 16.0f);
  return std::isfinite(c) ? c + cfg.offsetC : c;
}

// ---------- display (4 dígitos) ----------
const uint8_t L_E = SEG_A | SEG_D | SEG_E | SEG_F | SEG_G, L_r = SEG_E | SEG_G,
              L_O = SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F, L_F = SEG_A | SEG_E | SEG_F | SEG_G,
              L_H = SEG_B | SEG_C | SEG_E | SEG_F | SEG_G, L_o = SEG_C | SEG_D | SEG_E | SEG_G,
              L_t = SEG_D | SEG_E | SEG_F | SEG_G, L_S = SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,
              L_C = SEG_A | SEG_D | SEG_E | SEG_F, L_DEG = SEG_A | SEG_B | SEG_F | SEG_G, L_DASH = SEG_G;

void show2(uint8_t seg[4], int value, int pos) {
  value = constrain(value, 0, 99);
  seg[pos] = value >= 10 ? disp.encodeDigit(value / 10) : 0;
  seg[pos + 1] = disp.encodeDigit(value % 10);
}

void updateDisplay() {
  uint8_t seg[4] = {0, 0, 0, 0};
  if (ctl.fault == Fault::SensorOpen) { seg[0] = L_E; seg[1] = L_r; seg[2] = L_r; seg[3] = disp.encodeDigit(1); }
  else if (ctl.fault == Fault::SensorShort) { seg[0] = L_E; seg[1] = L_r; seg[2] = L_r; seg[3] = disp.encodeDigit(2); }
  else if (ctl.fault == Fault::OverTemp) { seg[0] = L_H; seg[1] = L_o; seg[2] = L_t; }
  else if (overlay == Overlay::Setpoint && millis() < overlayUntil) { seg[0] = L_S; show2(seg, cfg.setpointC, 2); }
  else if (overlay == Overlay::Timer && millis() < overlayUntil) {
    seg[0] = L_t;
    int left = minutesLeft();
    if (left < 0) { seg[2] = L_DASH; seg[3] = L_DASH; }
    else show2(seg, (left + 59) / 60, 2);  // horas restantes, arredondadas para cima
  }
  else if (mode == Mode::Off) { seg[0] = L_O; seg[1] = L_F; seg[2] = L_F; }
  else if (std::isfinite(tempC)) {
    show2(seg, lround(tempC), 0);
    seg[2] = L_DEG;
    seg[3] = mode == Mode::Eco ? L_E : L_C;  // "52°C" normal, "52°E" eco
    if (heaterOn) seg[1] |= 0x80;            // dois-pontos aceso = barras aquecendo
  }
  else { seg[0] = seg[1] = seg[2] = seg[3] = L_DASH; }
  disp.setSegments(seg);
}

// ---------- botões ----------
// UP/DN curto: ajusta a temperatura. UP longo: desligado -> normal -> eco -> desligado.
// DN longo: timer de 1 h -> 2 h -> 3 h -> 4 h -> 1 h...
struct Button { int pin; bool down; uint32_t since; bool longDone; };
Button btnUp{PIN_UP}, btnDn{PIN_DN};

void onShort(bool up) {
  cfg.setpointC = constrain(cfg.setpointC + (up ? 1 : -1), 30.0f, 65.0f);
  overlay = Overlay::Setpoint; overlayUntil = millis() + 2000;
  markDirty();
}

void onLong(bool up) {
  if (up) {
    Mode next = mode == Mode::Off ? Mode::Normal : mode == Mode::Normal ? Mode::Eco : Mode::Off;
    setMode(next, autoOffMin);
  } else if (mode != Mode::Off) {
    int hours = minutesLeft() < 0 ? 1 : (minutesLeft() + 59) / 60 % 4 + 1;
    setMode(mode, hours * 60);
    overlay = Overlay::Timer; overlayUntil = millis() + 2000;
  }
}

void pollButton(Button& b, bool up) {
  bool pressed = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  if (pressed && !b.down) { b.down = true; b.since = now; b.longDone = false; }
  else if (pressed && !b.longDone && now - b.since >= 800) { b.longDone = true; onLong(up); }
  else if (!pressed && b.down) {
    b.down = false;
    if (!b.longDone && now - b.since >= 30) onShort(up);  // 30 ms: filtra o repique do contato
  }
}

// ---------- horários ----------
void checkSchedule() {
  static int lastMinute = -1;
  struct tm t;
  if (!getLocalTime(&t, 0) || t.tm_min == lastMinute) return;  // sem hora da internet: ignora
  lastMinute = t.tm_min;
  for (auto& s : slots)
    if (s.on && s.h == t.tm_hour && s.m == t.tm_min) setMode(s.mode, s.minutes);
}

// ---------- Wi-Fi e página ----------
void startAP() {
  if (apStarted) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Secador-Toalha", adminPass.c_str());  // página em http://192.168.4.1
  apStarted = true;
}

bool auth() {
  if (web.authenticate("admin", adminPass.c_str())) return true;
  web.requestAuthentication();
  return false;
}

String num(float v, int decimals = 1) { return std::isfinite(v) ? String(v, decimals) : String("null"); }

void sendStatus() {
  struct tm t;
  char clock[6] = "--:--";
  if (getLocalTime(&t, 0)) strftime(clock, sizeof clock, "%H:%M", &t);
  String j = "{\"t\":" + num(tempC) + ",\"sp\":" + num(cfg.setpointC, 0) + ",\"hy\":" + num(cfg.hystC) +
             ",\"of\":" + num(cfg.offsetC) + ",\"mode\":" + String((int)mode) + ",\"heat\":" + String(heaterOn) +
             ",\"fault\":" + String((int)ctl.fault) + ",\"left\":" + String(minutesLeft()) +
             ",\"auto\":" + String(autoOffMin) + ",\"clock\":\"" + clock + "\",\"ssid\":\"" + wifiSsid +
             "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"slots\":[";
  for (int i = 0; i < 2; i++)
    j += String(i ? "," : "") + "{\"on\":" + String(slots[i].on) + ",\"h\":" + String(slots[i].h) +
         ",\"m\":" + String(slots[i].m) + ",\"d\":" + String(slots[i].minutes) +
         ",\"md\":" + String((int)slots[i].mode) + "}";
  web.send(200, "application/json", j + "]}");
}

void handleSet() {
  if (!auth()) return;
  if (web.hasArg("mode")) setMode((Mode)constrain(web.arg("mode").toInt(), 0, 2),
                                  web.hasArg("timer") ? web.arg("timer").toInt() : autoOffMin);
  if (web.hasArg("sp")) cfg.setpointC = constrain(web.arg("sp").toFloat(), 30.0f, 65.0f);
  if (web.hasArg("hy")) cfg.hystC = constrain(web.arg("hy").toFloat(), 1.0f, 10.0f);
  if (web.hasArg("of")) cfg.offsetC = constrain(web.arg("of").toFloat(), -10.0f, 10.0f);
  if (web.hasArg("auto")) autoOffMin = constrain(web.arg("auto").toInt(), 0, 720);
  for (int i = 0; i < 2; i++) {
    String k = "s" + String(i);
    if (!web.hasArg(k + "h")) continue;
    slots[i].on = web.arg(k + "on") == "1";
    slots[i].h = constrain(web.arg(k + "h").toInt(), 0, 23);
    slots[i].m = constrain(web.arg(k + "m").toInt(), 0, 59);
    slots[i].minutes = constrain(web.arg(k + "d").toInt(), 1, 720);
    slots[i].mode = web.arg(k + "md") == "2" ? Mode::Eco : Mode::Normal;
  }
  bool restart = false;
  if (web.hasArg("ssid") && web.arg("ssid").length()) {
    wifiSsid = web.arg("ssid"); wifiPass = web.arg("pass"); restart = true;
  }
  if (web.hasArg("admin")) {
    if (web.arg("admin").length() < 8) return web.send(400, "text/plain", "A senha precisa de pelo menos 8 caracteres.");
    adminPass = web.arg("admin"); restart = true;
  }
  savePrefs();
  web.send(200, "text/plain", restart ? "Salvo. Reiniciando..." : "Salvo.");
  if (restart) { digitalWrite(PIN_TRIAC, LOW); delay(500); ESP.restart(); }
}

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="pt-BR"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Secador de toalhas</title>
<style>body{font:16px system-ui;margin:0 auto;max-width:520px;padding:16px;background:#f2f4f5;color:#15212b}
fieldset{border:1px solid #d3dadf;border-radius:8px;margin:0 0 14px;background:#fff}legend{font-weight:600}
label{display:flex;justify-content:space-between;align-items:center;margin:8px 0;gap:8px}
input,select{font:inherit;width:9em}button{font:inherit;padding:8px 14px;border-radius:6px;border:0;background:#1d6a86;color:#fff;margin:4px 4px 4px 0}
#big{font:700 3rem system-ui}.err{color:#a33a3a}</style>
<h1>Secador de toalhas</h1>
<fieldset><legend>Agora</legend><div id="big">--</div><div id="st"></div>
<button onclick="mode(1)">Ligar normal</button><button onclick="mode(2)">Ligar eco</button><button onclick="mode(0)">Desligar</button>
<label>Timer ao ligar (min) <input id="timer" type="number" min="0" max="720" value="180"></label></fieldset>
<form id="cfg"><fieldset><legend>Temperatura</legend>
<label>Alvo da barra (°C) <input name="sp" type="number" min="30" max="65"></label>
<label>Histerese (°C) <input name="hy" type="number" step="0.5" min="1" max="10"></label>
<label>Calibração do sensor (°C) <input name="of" type="number" step="0.1" min="-10" max="10"></label>
<label>Desligamento automático (min, 0 = nunca) <input name="auto" type="number" min="0" max="720"></label></fieldset>
<fieldset><legend>Horários diários</legend><div id="slots"></div></fieldset><button>Salvar</button></form>
<form id="wifi"><fieldset><legend>Wi-Fi e senha</legend>
<label>Rede <input name="ssid"></label><label>Senha da rede <input name="pass" type="password"></label>
<label>Nova senha da página <input name="admin" type="password" minlength="8"></label>
<button>Salvar e reiniciar</button></fieldset></form>
<form method="post" action="/update" enctype="multipart/form-data"><fieldset><legend>Atualizar firmware</legend>
<input type="file" name="fw" accept=".bin"> <button>Enviar</button></fieldset></form>
<script>
const $=s=>document.querySelector(s),F=['','Sensor desconectado','Sensor em curto','Temperatura acima de 75 °C'];
const post=d=>fetch('/api',{method:'POST',body:new URLSearchParams(d)}).then(r=>r.text()).then(alert);
function mode(m){post({mode:m,timer:$('#timer').value})}
let loaded=false;
async function tick(){const s=await (await fetch('/api')).json();
 $('#big').textContent=s.t===null?'--':s.t.toFixed(1)+' °C';
 $('#st').innerHTML=s.fault?'<b class=err>'+F[s.fault]+'. Desligue da tomada para rearmar.</b>':
  ['Desligado','Normal','Eco'][s.mode]+(s.mode?(s.heat?' · aquecendo':' · aguardando')+(s.left>=0?' · desliga em '+s.left+' min':''):'')+' · '+s.clock;
 if(loaded)return;loaded=true;const f=$('#cfg');['sp','hy','of','auto'].forEach(k=>f[k].value=s[k]);
 $('#slots').innerHTML=s.slots.map((x,i)=>`<label><span><input type=checkbox name=s${i}on value=1 ${x.on?'checked':''} style="width:auto"> ${i+1}º horário</span>
  <input name=s${i}t type=time value="${String(x.h).padStart(2,'0')}:${String(x.m).padStart(2,'0')}"></label>
  <label>Duração (min) <input name=s${i}d type=number min=1 max=720 value=${x.d}></label>
  <label>Modo <select name=s${i}md><option value=1>Normal</option><option value=2 ${x.md==2?'selected':''}>Eco</option></select></label>`).join('');
 $('#wifi').ssid.value=s.ssid}
$('#cfg').onsubmit=e=>{e.preventDefault();const d=Object.fromEntries(new FormData(e.target));
 [0,1].forEach(i=>{[d['s'+i+'h'],d['s'+i+'m']]=d['s'+i+'t'].split(':');d['s'+i+'on']=d['s'+i+'on']||'0'});post(d)};
$('#wifi').onsubmit=e=>{e.preventDefault();const d=Object.fromEntries(new FormData(e.target));if(!d.admin)delete d.admin;post(d)};
tick();setInterval(tick,3000);
</script></html>)HTML";

void setupWeb() {
  web.on("/", HTTP_GET, [] { if (auth()) web.send_P(200, "text/html", PAGE); });
  web.on("/api", HTTP_GET, [] { if (auth()) sendStatus(); });
  web.on("/api", HTTP_POST, handleSet);
  web.on("/update", HTTP_POST,
    [] {
      if (!auth()) return;
      web.send(200, "text/plain", Update.hasError() ? "Falha na atualização." : "Atualizado. Reiniciando...");
      delay(500);
      ESP.restart();
    },
    [] {
      if (!web.authenticate("admin", adminPass.c_str())) return;
      HTTPUpload& up = web.upload();
      if (up.status == UPLOAD_FILE_START) {
        setMode(Mode::Off, 0);
        digitalWrite(PIN_TRIAC, LOW);  // barras desligadas durante a gravação
        Update.begin(UPDATE_SIZE_UNKNOWN);
      } else if (up.status == UPLOAD_FILE_WRITE) {
        Update.write(up.buf, up.currentSize);
      } else if (up.status == UPLOAD_FILE_END) {
        Update.end(true);
      }
      esp_task_wdt_reset();
    });
  web.begin();
}

// ---------- principal ----------
void setup() {
  pinMode(PIN_TRIAC, OUTPUT);
  digitalWrite(PIN_TRIAC, LOW);  // primeira coisa: garantir as barras desligadas
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);   // LED azul do módulo é ativo em LOW
  pinMode(PIN_UP, INPUT_PULLUP);
  pinMode(PIN_DN, INPUT_PULLUP);
  Serial.begin(115200);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdt = {.timeout_ms = 8000, .idle_core_mask = 0, .trigger_panic = true};
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
#else
  esp_task_wdt_init(8, true);
#endif
  esp_task_wdt_add(NULL);  // se o loop travar por 8 s, o chip reinicia (e o triac fica desligado)

  loadPrefs();
  disp.setBrightness(4);
  analogSetAttenuation(ADC_11db);

  WiFi.setHostname("secador");
  WiFi.setAutoReconnect(true);
  if (wifiSsid.length()) { WiFi.mode(WIFI_STA); WiFi.begin(wifiSsid.c_str(), wifiPass.c_str()); }
  else startAP();
  configTzTime(TZ_BRASILIA, "a.st1.ntp.br", "pool.ntp.org");
  setupWeb();
}

void loop() {
  esp_task_wdt_reset();
  uint32_t now = millis();

  pollButton(btnUp, true);
  pollButton(btnDn, false);

  if (now - lastControl >= 500) {
    lastControl = now;
    if (timerOn && minutesLeft() == 0) setMode(Mode::Off, 0);
    checkSchedule();
    tempC = readTempC();
    heaterOn = ctl.update(mode, cfg, tempC, now);
    digitalWrite(PIN_TRIAC, heaterOn ? HIGH : LOW);
    digitalWrite(PIN_LED, heaterOn ? LOW : HIGH);
    updateDisplay();
  }

  if (prefsDirty && now - prefsDirtyAt > 3000) { prefsDirty = false; savePrefs(); }

  // Sem conexão 30 s depois de ligar: abre também a rede "Secador-Toalha" para configurar.
  if (!apStarted && WiFi.status() != WL_CONNECTED && now > 30000) startAP();
  if (!mdnsStarted && WiFi.status() == WL_CONNECTED) mdnsStarted = MDNS.begin("secador");

  web.handleClient();
  delay(5);
}
