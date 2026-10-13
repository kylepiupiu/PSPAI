/*
  ESP32-S3 Control V1.0.2 - PSPAI Monitor
  Target: ESP32-S3 N16R8, Arduino-ESP32 3.x
  PSP AP: wifi4psp, OPEN, 192.168.4.1

  NOTE: PSPAI requires the old PSP client, home Wi-Fi and Mac Relay.
  This firmware does NOT call an AI service or provide Internet NAT.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_system.h>
#include "config.h"

static constexpr uint32_t STATUS_INTERVAL_MS = 1500;
static constexpr uint32_t HEALTH_INTERVAL_MS = 12000;
static constexpr uint32_t RELAY_REPORT_TTL_MS = 30000;
static constexpr uint32_t BRIDGE_IDLE_MS = 90000;
static constexpr size_t LOG_CAPACITY = 80;
static constexpr size_t STATION_LIMIT = 8;

WebServer web(80);
WiFiServer bridgeServer(PSPAI_BRIDGE_PORT);
WiFiClient pspClient;
WiFiClient macClient;
Preferences settings;

struct LogRecord {
  uint32_t seq = 0;
  uint32_t timeSeconds = 0;
  String level;
  String message;
};
LogRecord records[LOG_CAPACITY];
size_t logHead = 0;
size_t logCount = 0;
uint32_t nextLogSeq = 1;

struct Station {
  String mac;
  String ip;
};
Station stations[STATION_LIMIT];
size_t stationCount = 0;
String knownPspMac;
int selectedApChannel = PSP_AP_CHANNEL;
String adminKey;
String relayKey;

bool upstreamConfigured = false;
bool bridgeActive = false;
bool relayHealthy = false;
int relayHealthCode = 0;
uint32_t lastHealthAt = 0;
uint32_t lastStationScan = 0;
uint32_t nextRestartAt = 0;
uint32_t relayReportAt = 0;
String aiReportedStatus = "unknown";
String aiReportedDetail;
uint32_t bridgeStarted = 0;
uint32_t bridgeLastActivity = 0;
uint32_t bridgePspToMac = 0;
uint32_t bridgeMacToPsp = 0;
uint32_t bridgeRequests = 0;
uint32_t bridgeErrors = 0;
String bridgePeer;

String escapeJson(const String &s) {
  String r;
  r.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); ++i) {
    uint8_t c = (uint8_t)s[i];
    switch (c) {
      case '"': r += "\\\""; break;
      case '\\': r += "\\\\"; break;
      case '\n': r += "\\n"; break;
      case '\r': r += "\\r"; break;
      case '\t': r += "\\t"; break;
      default:
        if (c < 32) {
          char b[7]; snprintf(b, sizeof(b), "\\u%04x", c); r += b;
        } else r += (char)c;
    }
  }
  return r;
}
String q(const String &s) { return String("\"") + escapeJson(s) + "\""; }
String yesNo(bool x) { return x ? "true" : "false"; }

void logEvent(const String &level, const String &message) {
  records[logHead].seq = nextLogSeq++;
  records[logHead].timeSeconds = millis() / 1000;
  records[logHead].level = level;
  records[logHead].message = message;
  logHead = (logHead + 1) % LOG_CAPACITY;
  if (logCount < LOG_CAPACITY) ++logCount;
  Serial.printf("[%lus][%s] %s\n", (unsigned long)(millis()/1000), level.c_str(), message.c_str());
}

String createRandomToken() {
  char b[33];
  for (int i = 0; i < 4; i++) {
    snprintf(b + i * 8, 9, "%08lx", (unsigned long)esp_random());
  }
  return String(b);
}

String macAddress(const uint8_t *mac) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(b);
}

bool validMac(String s) {
  if (s.length() != 17) return false;
  for (int i = 0; i < 17; ++i) {
    if ((i+1)%3 == 0) {
      if (s[i] != ':') return false;
    } else if (!isxdigit((unsigned char)s[i])) return false;
  }
  return true;
}

void sendJson(int status, const String &value) {
  web.sendHeader("Cache-Control", "no-store, max-age=0");
  web.sendHeader("X-Content-Type-Options", "nosniff");
  web.send(status, "application/json; charset=utf-8", value);
}
void sendError(int code, const String &msg) {
  sendJson(code, String("{\"ok\":false,\"error\":") + q(msg) + "}");
}

bool authorizedAdmin() {
  if (web.header("X-Admin-Key") != adminKey) {
    sendError(401, "Invalid admin key. See USB Serial for the original key.");
    logEvent("warn", "Rejected unauthorized admin request");
    return false;
  }
  return true;
}

void scanStations() {
  // Arduino-ESP32 3.3.x / ESP-IDF 5.5: esp_netif_get_sta_list()
  // and esp_netif_sta_list_t no longer exist. Query the SoftAP DHCP server
  // by MAC using APIs declared in esp_netif.h, which is already included.
  wifi_sta_list_t staList = {};
  if (esp_wifi_ap_get_sta_list(&staList) != ESP_OK) return;

  size_t newCount = min((size_t)staList.num, STATION_LIMIT);
  esp_netif_pair_mac_ip_t macIpPairs[STATION_LIMIT] = {};
  for (size_t i = 0; i < newCount; ++i) {
    memcpy(macIpPairs[i].mac, staList.sta[i].mac, 6);
  }

  esp_netif_t *apInterface = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  bool haveIp = newCount > 0 && apInterface != nullptr &&
      esp_netif_dhcps_get_clients_by_mac(apInterface, (int)newCount, macIpPairs) == ESP_OK;

  Station newest[STATION_LIMIT];
  for (size_t i = 0; i < newCount; ++i) {
    newest[i].mac = macAddress(staList.sta[i].mac);
    newest[i].ip = "";
    if (haveIp && macIpPairs[i].ip.addr != 0) {
      newest[i].ip = IPAddress(macIpPairs[i].ip.addr).toString();
    }
  }
  for (size_t i=0; i<newCount; i++) {
    bool found = false;
    for (size_t j=0; j<stationCount; j++) if (newest[i].mac == stations[j].mac) found = true;
    if (!found) logEvent("info", String("Wi-Fi client connected: ") + newest[i].mac);
  }
  for (size_t j=0; j<stationCount; j++) {
    bool found = false;
    for (size_t i=0; i<newCount; i++) if (stations[j].mac == newest[i].mac) found = true;
    if (!found) logEvent("warn", String("Wi-Fi client disconnected: ") + stations[j].mac);
  }
  for (size_t i=0; i<newCount; i++) stations[i] = newest[i];
  stationCount = newCount;
}

bool pspAssociated() {
  if (knownPspMac.isEmpty()) return false;
  for (size_t i=0; i<stationCount; ++i) if (stations[i].mac == knownPspMac) return true;
  return false;
}

void checkRelayHealth() {
  if (!upstreamConfigured || WiFi.status() != WL_CONNECTED) {
    if (relayHealthy) logEvent("warn", "Mac Relay unreachable: upstream Wi-Fi disconnected");
    relayHealthy = false;
    relayHealthCode = 0;
    return;
  }
  // A verified HTTP 200 from the existing Mac Relay /health endpoint.
  HTTPClient http;
  String url = String("http://") + MAC_RELAY_HOST + ":" + String(MAC_RELAY_PORT) + "/health";
  http.setConnectTimeout(1200);
  http.setTimeout(1200);
  int result = -1;
  if (http.begin(url)) { result = http.GET(); http.end(); }
  bool healthy = result == HTTP_CODE_OK;
  if (healthy != relayHealthy) {
    logEvent(healthy ? "info" : "error", healthy ? "Mac Relay /health replied HTTP 200" : String("Mac Relay /health failed (code ") + result + ")");
  }
  relayHealthy = healthy;
  relayHealthCode = result;
  lastHealthAt = millis();
}

void closeBridge(const String &reason) {
  if (bridgeActive) {
    logEvent("info", String("PSPAI TCP bridge closed: ") + reason
      + " | PSP->Mac=" + bridgePspToMac + "B Mac->PSP=" + bridgeMacToPsp + "B");
  }
  pspClient.stop();
  macClient.stop();
  bridgeActive = false;
  bridgePeer = "";
}

void pumpClient(WiFiClient &from, WiFiClient &to, uint32_t &counter) {
  uint8_t buf[512];
  int available = from.available();
  if (available <= 0) return;
  int got = from.read(buf, min(available, (int)sizeof(buf)));
  if (got <= 0) return;
  int done = 0;
  while (done < got && to.connected()) {
    size_t sent = to.write(buf + done, got - done);
    if (sent == 0) break;
    done += sent;
    bridgeLastActivity = millis();
  }
  counter += done;
  if (done != got) {
    bridgeErrors++;
    closeBridge("write error / incomplete forwarding");
  }
}

void serviceBridge() {
  // The PSP client points to 192.168.4.1:8088. The Mac IP stays hidden from the PSP.
  if (!bridgeActive) {
    WiFiClient incoming = bridgeServer.accept();
    if (!incoming) return;
    IPAddress remote = incoming.remoteIP();
    if (remote[0] != 192 || remote[1] != 168 || remote[2] != 4) {
      logEvent("warn", "Rejected non-AP connection to PSPAI TCP port");
      incoming.stop(); return;
    }
    if (!upstreamConfigured || WiFi.status() != WL_CONNECTED) {
      logEvent("warn", "PSPAI request rejected: upstream Wi-Fi not connected");
      bridgeErrors++;
      incoming.stop(); return;
    }
    WiFiClient outgoing;
    outgoing.setTimeout(2);
    if (!outgoing.connect(MAC_RELAY_HOST, MAC_RELAY_PORT, 1500)) {
      logEvent("error", String("PSPAI TCP connect to Mac Relay failed: ") + MAC_RELAY_HOST);
      bridgeErrors++;
      incoming.stop(); return;
    }
    pspClient = incoming;
    macClient = outgoing;
    pspClient.setNoDelay(true);
    macClient.setNoDelay(true);
    bridgePeer = remote.toString();
    bridgePspToMac = 0;
    bridgeMacToPsp = 0;
    bridgeStarted = bridgeLastActivity = millis();
    bridgeActive = true;
    bridgeRequests++;
    logEvent("info", String("PSPAI TCP bridge connected: ") + bridgePeer + " -> " + MAC_RELAY_HOST + ":" + MAC_RELAY_PORT);
    return;
  }
  pumpClient(pspClient, macClient, bridgePspToMac);
  if (!bridgeActive) return;
  pumpClient(macClient, pspClient, bridgeMacToPsp);
  if (!bridgeActive) return;
  if ((!pspClient.connected() && !pspClient.available()) || (!macClient.connected() && !macClient.available())) {
    closeBridge("peer disconnected");
  } else if (millis() - bridgeLastActivity > BRIDGE_IDLE_MS) {
    closeBridge("idle timeout");
  }
}

String clientsJson() {
  String s = "[";
  for (size_t i=0; i<stationCount; i++) {
    if (i) s += ",";
    s += "{\"mac\":" + q(stations[i].mac)
      + ",\"ip\":" + q(stations[i].ip)
      + ",\"psp\":" + yesNo(knownPspMac == stations[i].mac && !knownPspMac.isEmpty()) + "}";
  }
  return s + "]";
}

void handleStatus() {
  bool staUp = upstreamConfigured && WiFi.status() == WL_CONNECTED;
  bool reportFresh = relayReportAt != 0 && (uint32_t)(millis() - relayReportAt) < RELAY_REPORT_TTL_MS;
  uint8_t chan = 0; wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
  esp_wifi_get_channel(&chan, &secondary);
  String s;
  s.reserve(1900);
  s = "{\"ok\":true,\"version\":\"1.0.2\",\"chip\":" + q(ESP.getChipModel());
  s += ",\"flashBytes\":" + String(ESP.getFlashChipSize());
  s += ",\"psramBytes\":" + String(ESP.getPsramSize());
  s += ",\"psramFound\":" + yesNo(psramFound());
  s += ",\"freeHeapBytes\":" + String(ESP.getFreeHeap());
  s += ",\"uptimeSeconds\":" + String(millis()/1000);
  s += ",\"ap\":{\"ssid\":" + q(PSP_AP_SSID) + ",\"open\":true,\"ip\":" + q(WiFi.softAPIP().toString());
  s += ",\"channel\":" + String(chan) + ",\"channelPreference\":" + String(selectedApChannel) + ",\"clients\":" + String(WiFi.softAPgetStationNum()) + ",\"devices\":" + clientsJson() + "}";
  s += ",\"upstream\":{\"configured\":" + yesNo(upstreamConfigured);
  s += ",\"connected\":" + yesNo(staUp);
  s += ",\"ssid\":" + q(staUp ? WiFi.SSID() : (upstreamConfigured ? UPSTREAM_SSID : ""));
  s += ",\"ip\":" + q(staUp ? WiFi.localIP().toString() : "") + "}";
  s += ",\"pspai\":{\"pspMac\":" + q(knownPspMac);
  s += ",\"pspAssociated\":" + yesNo(pspAssociated());
  s += ",\"relayHost\":" + q(MAC_RELAY_HOST);
  s += ",\"relayHealthOK\":" + yesNo(relayHealthy);
  s += ",\"relayHttpCode\":" + String(relayHealthCode);
  s += ",\"relayChecked\":" + yesNo(lastHealthAt != 0);
  s += ",\"aiState\":" + q(reportFresh ? aiReportedStatus : "unknown");
  s += ",\"aiDetail\":" + q(reportFresh ? aiReportedDetail : "No recent Mac status report");
  s += ",\"bridgeActive\":" + yesNo(bridgeActive);
  s += ",\"bridgePeer\":" + q(bridgePeer);
  s += ",\"requests\":" + String(bridgeRequests);
  s += ",\"errors\":" + String(bridgeErrors);
  s += ",\"bridgePort\":" + String(PSPAI_BRIDGE_PORT) + "}}";
  sendJson(200, s);
}

void handleLogs() {
  uint32_t after = (uint32_t)strtoul(web.arg("after").c_str(), nullptr, 10);
  String s = "{\"ok\":true,\"events\":[";
  bool first = true;
  for (size_t i=0; i<logCount; i++) {
    size_t index = (logHead + LOG_CAPACITY - logCount + i) % LOG_CAPACITY;
    const LogRecord &r = records[index];
    if (r.seq <= after) continue;
    if (!first) s += ",";
    first = false;
    s += "{\"id\":" + String(r.seq) + ",\"sec\":" + String(r.timeSeconds)
      + ",\"level\":" + q(r.level) + ",\"message\":" + q(r.message) + "}";
  }
  s += "]}";
  sendJson(200, s);
}

void handlePspMac() {
  if (!authorizedAdmin()) return;
  String mac = web.arg("mac"); mac.trim(); mac.toUpperCase();
  if (!mac.isEmpty() && !validMac(mac)) { sendError(400,"MAC must be XX:XX:XX:XX:XX:XX or blank"); return; }
  knownPspMac = mac;
  settings.putString("psp_mac", knownPspMac);
  logEvent("info", knownPspMac.isEmpty() ? "PSP MAC binding cleared" : "PSP MAC binding updated");
  sendJson(200, "{\"ok\":true}");
}

void handleApChannel() {
  if (!authorizedAdmin()) return;
  int chan = web.arg("channel").toInt();
  if (chan != 1 && chan != 6 && chan != 11) {
    sendError(400, "Use one of channels 1, 6, or 11"); return;
  }
  selectedApChannel = chan;
  settings.putInt("channel", selectedApChannel);
  logEvent("info", String("AP channel preference changed to ") + selectedApChannel + "; restarting");
  sendJson(200, "{\"ok\":true,\"message\":\"Restarting to apply channel preference\"}");
  nextRestartAt = millis() + 600;
}

void handleRestart() {
  if (!authorizedAdmin()) return;
  logEvent("warn", "Restart requested from web console");
  sendJson(200,"{\"ok\":true,\"message\":\"Restarting\"}");
  nextRestartAt = millis() + 500;
}

void handleMacReport() {
  if (web.header("X-Relay-Key") != relayKey) { sendError(401,"Invalid relay reporting key"); return; }
  String state = web.arg("status");
  if (state != "ok" && state != "error" && state != "unknown") {
    sendError(400,"status must be ok, error or unknown"); return;
  }
  String detail = web.arg("detail");
  if (detail.length() > 120) detail = detail.substring(0,120);
  bool changed = state != aiReportedStatus || detail != aiReportedDetail;
  aiReportedStatus = state;
  aiReportedDetail = detail;
  relayReportAt = millis();
  if (changed) logEvent(state == "error" ? "error" : "info", String("Mac AI report: ") + state + (detail.isEmpty()?"":" (" + detail + ")"));
  sendJson(200,"{\"ok\":true}");
}

// Stored as a C++ raw-string literal: small CSS/JS dashboard, no external CDN.
const char PAGE[] PROGMEM = R"CONTROLHTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3 Control · PSPAI</title>
<style>
:root{color-scheme:dark;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
*{box-sizing:border-box}body{margin:0;background:#0b1018;color:#e6edf7}
main{max-width:1080px;margin:0 auto;padding:24px 18px 55px}
header{display:flex;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:20px}
.mono,code,pre{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
h1{font-size:24px;letter-spacing:-.6px;margin:0}h2{font-size:16px;margin:0 0 13px}
p{margin:6px 0;color:#a5b3c9;font-size:13px;line-height:1.55}
small,.muted{color:#9aacc4;font-size:12px}strong{font-weight:650}
.status{display:inline-block;border-radius:999px;padding:6px 10px;font:12px ui-monospace,monospace;background:#1f3036;color:#8ed6bb}
.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px}
.card{background:#141d2b;border:1px solid #29394e;border-radius:14px;padding:17px;min-width:0}
.card.wide{grid-column:span 2}.card.full{grid-column:1/-1}
.metric{font-size:24px;font-weight:750;letter-spacing:-.5px;margin-top:9px;overflow-wrap:anywhere}
.k{color:#9ab0cb;font-size:12px}.v{font-size:13px;overflow-wrap:anywhere}
.kv{display:grid;grid-template-columns:minmax(105px,1fr) 2fr;gap:9px;padding:8px 0;border-bottom:1px solid #263448}
.kv:last-child{border:none}button,input{font:inherit;border-radius:8px}input{padding:10px;background:#0c1320;color:#f2f6fc;border:1px solid #42516a;max-width:100%;width:100%}
button{background:#3d76dd;color:white;padding:10px 13px;border:0;cursor:pointer}button.alt{background:#293b54}button:disabled{opacity:.5;cursor:not-allowed}
.row{display:flex;align-items:center;gap:9px;flex-wrap:wrap}label{display:block;margin:8px 0;color:#a6bad3;font-size:12px}
.table{overflow-x:auto}table{width:100%;border-collapse:collapse;font-size:12px}td,th{padding:9px 7px;border-bottom:1px solid #28384e;text-align:left}th{color:#9fb3cd}
#logs{background:#0a121d;border:1px solid #243347;border-radius:9px;padding:12px;height:230px;overflow:auto;font-size:12px;line-height:1.7;white-space:pre-wrap;overflow-wrap:anywhere}
.info{color:#8fdac1}.warn{color:#f4ca8b}.error{color:#ff919b}
.errbar{color:#ffc3c8;font-size:12px;margin:10px 0;min-height:16px}
@media(max-width:680px){.grid{grid-template-columns:repeat(2,minmax(0,1fr))}.card.wide{grid-column:span 2}h1{font-size:20px}.metric{font-size:21px}.kv{grid-template-columns:1fr}}
@media(max-width:370px){.grid{grid-template-columns:1fr}.card.wide{grid-column:span 1}}
</style></head><body><main>
<header><div><small class="mono">KYLE / EMBEDDED SYSTEMS</small><h1>ESP32-S3 Control</h1><p>V1.0.2 · PSPAI Network & Diagnostics</p></div><span class="status" id="netState">CONNECTING</span></header>
<div class="grid">
<section class="card"><div class="k">芯片型号</div><div class="metric" id="chip">—</div></section>
<section class="card"><div class="k">Flash</div><div class="metric" id="flash">—</div></section>
<section class="card"><div class="k">PSRAM</div><div class="metric" id="psram">—</div><p id="psok"></p></section>
<section class="card"><div class="k">运行时间</div><div class="metric" id="uptime">—</div><p id="heap"></p></section>
<section class="card wide"><h2>Wi-Fi 管理</h2><div class="kv"><span class="k">热点名称</span><strong class="v" id="ssid">—</strong></div><div class="kv"><span class="k">安全模式</span><span class="v">OPEN · 无密码（PSP 实验网络）</span></div><div class="kv"><span class="k">IP / 频道</span><span class="v mono" id="apip">—</span></div><div class="kv"><span class="k">连接设备</span><span class="v" id="clients">—</span></div><div class="kv"><span class="k">上游 Wi-Fi / ESP IP</span><span class="v" id="upstream">—</span></div><label for="channel">首选热点频道（上游 Wi-Fi 连接后，实际频道可能由路由器决定）</label><div class="row"><select id="channel" style="background:#0c1320;color:#e6edf7;border:1px solid #42516a;border-radius:8px;padding:9px"><option value="1">CH 1</option><option value="6">CH 6</option><option value="11">CH 11</option></select><button class="alt" id="saveChannel">保存并重启</button></div><p>热点名称固定为 wifi4psp。上游 Wi-Fi 账号只通过 USB 修改 config.h；不会在开放网页上收集路由器密码。</p></section>
<section class="card wide"><h2>PSPAI Monitor</h2><div class="kv"><span class="k">PSP 身份 / 接入</span><span class="v" id="psp">—</span></div><div class="kv"><span class="k">Mac Relay /health</span><span class="v" id="relay">—</span></div><div class="kv"><span class="k">AI 服务上报</span><span class="v" id="ai">—</span></div><div class="kv"><span class="k">TCP 桥接</span><span class="v" id="bridge">—</span></div><div class="kv"><span class="k">桥接会话 / 错误</span><span class="v" id="counts">—</span></div><p>PSPAI 在 PSP 原生客户端显示答案。没有真实连接、健康检查或 Mac 上报时，此处不会显示“正常”。</p></section>
<section class="card wide"><h2>连接设备</h2><div class="table"><table><thead><tr><th>设备 MAC</th><th>IP 地址</th><th>身份</th><th></th></tr></thead><tbody id="deviceRows"><tr><td colspan="4">读取中…</td></tr></tbody></table></div><p>只有手动绑定了 PSP 的 MAC 地址，才能确认某台连接设备是 PSP。</p></section>
<section class="card wide"><h2>远程控制</h2><label for="admin">管理员密钥（首次烧录在 USB Serial 显示）</label><input id="admin" type="password" autocomplete="off" placeholder="输入 32 位密钥"><div class="row" style="margin-top:12px"><button id="restart" class="alt">重启 ESP32-S3</button><button id="unbind" class="alt">清除 PSP 绑定</button></div><p>此网络无密码，HTTP 明文访问。只适合受控实验环境，不建议在公共场所使用。管理员密钥不会保存到浏览器。</p><div class="errbar" id="msg"></div></section>
<section class="card full"><div class="row" style="justify-content:space-between"><h2>事件日志 · 自动刷新</h2><button class="alt" id="export">导出可见日志</button></div><div id="logs" class="mono" aria-live="polite"></div><p>仅记录 ESP32 观察到的真实网络与服务事件。时间为开机后的秒数，非实时时钟。</p></section>
</div><p style="margin-top:20px">Local-only interface · 192.168.4.1 · No cloud dependencies</p></main>
<script>
'use strict';
const $=id=>document.getElementById(id);
let lastEvent=0, visibleLogs=[];
function text(id,v){$(id).textContent=String(v);}
function fmtByte(n){return (n/1048576).toFixed(1)+' MB';}
function fmtUptime(t){let s=Math.floor(t);return Math.floor(s/3600)+'h '+Math.floor(s%3600/60)+'m '+s%60+'s';}
function message(s){text('msg',s);}
async function get(url){let r=await fetch(url,{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);return r.json();}
function update(s){
 text('netState',s.upstream.connected?'AP + UPLINK':'LOCAL AP ONLY');
 text('chip',s.chip);text('flash',fmtByte(s.flashBytes));text('psram',fmtByte(s.psramBytes));
 text('psok',s.psramFound?'PSRAM detected: YES':'PSRAM detected: NO');text('uptime',fmtUptime(s.uptimeSeconds));text('heap','Free heap '+fmtByte(s.freeHeapBytes));
 text('ssid',s.ap.ssid);text('apip',s.ap.ip+' / CH '+s.ap.channel);if(!window.__channelSet){$('channel').value=String(s.ap.channelPreference);window.__channelSet=true;}text('clients',s.ap.clients+' 台');
 text('upstream',s.upstream.connected?(s.upstream.ssid+' / '+s.upstream.ip):(s.upstream.configured?'连接中或连接失败':'未配置（本地模式）'));
 text('psp',s.pspai.pspMac?(s.pspai.pspAssociated?'已连接 · '+s.pspai.pspMac:'未连接 · '+s.pspai.pspMac):'未绑定 PSP MAC，无法确认身份');
 text('relay',!s.upstream.connected?'不可检测（无上游网络）':(!s.pspai.relayChecked?'等待首次检测':(s.pspai.relayHealthOK?'HTTP 200 · 在线':'检测失败 · '+s.pspai.relayHttpCode)));
 text('ai',s.pspai.aiState==='unknown'?'未知 · '+s.pspai.aiDetail:s.pspai.aiState+' · '+s.pspai.aiDetail);
 text('bridge',s.pspai.bridgeActive?'正在转发 · '+s.pspai.bridgePeer:'未建立会话 · ESP:'+s.pspai.bridgePort);
 text('counts',s.pspai.requests+' / '+s.pspai.errors);
 const tb=$('deviceRows');tb.replaceChildren();
 if(!s.ap.devices.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=4;td.textContent='暂无连接设备';tr.appendChild(td);tb.appendChild(tr);}
 for(const d of s.ap.devices){let tr=document.createElement('tr');for(const v of [d.mac,d.ip||'尚未分配 IP',d.psp?'PSP（已绑定）':'未识别']){let td=document.createElement('td');td.textContent=v;tr.appendChild(td);}let td=document.createElement('td');let b=document.createElement('button');b.className='alt';b.textContent=d.psp?'已绑定':'设为 PSP';b.disabled=d.psp;b.onclick=()=>bind(d.mac);td.appendChild(b);tr.appendChild(td);tb.appendChild(tr);}
}
async function updateStatus(){try{update(await get('/api/status'));message('');}catch(e){text('netState','DISCONNECTED');message('状态读取失败：'+e.message);}}
async function updateLogs(){try{let d=await get('/api/logs?after='+lastEvent);for(const l of d.events){lastEvent=Math.max(lastEvent,l.id);visibleLogs.push(l);}visibleLogs=visibleLogs.slice(-100);const el=$('logs');el.replaceChildren();for(const l of visibleLogs){let line=document.createElement('div');line.className=l.level;line.textContent='['+l.sec+'s] ['+l.level.toUpperCase()+'] '+l.message;el.appendChild(line);}el.scrollTop=el.scrollHeight;}catch(e){/* status banner handles loss */}}
async function command(url,body){const key=$('admin').value;if(!key){message('请先输入 USB 串口显示的管理员密钥');return;}try{let r=await fetch(url,{method:'POST',headers:{'X-Admin-Key':key,'Content-Type':'application/x-www-form-urlencoded'},body:body||''});let j=await r.json();if(!r.ok)throw Error(j.error||('HTTP '+r.status));message('操作已发送');}catch(e){message('请求失败：'+e.message);}}
async function bind(mac){await command('/api/psp-mac','mac='+encodeURIComponent(mac));updateStatus();}
$('unbind').onclick=()=>bind('');
$('saveChannel').onclick=()=>{if(confirm('修改频道会使所有无线设备暂时断开连接，确定继续？'))command('/api/ap-channel','channel='+encodeURIComponent($('channel').value));};
$('restart').onclick=()=>{if(confirm('现在重启 ESP32-S3？PSP 连接会短暂中断。'))command('/api/restart','');};
$('export').onclick=()=>{let blob=new Blob([visibleLogs.map(x=>'['+x.sec+'s] ['+x.level+'] '+x.message).join('\n')],{type:'text/plain;charset=utf-8'});let a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='esp32s3-events.txt';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),2000);};
updateStatus();updateLogs();setInterval(updateStatus,2500);setInterval(updateLogs,2500);
</script></body></html>
)CONTROLHTML";

void servePage() {
  web.sendHeader("Cache-Control", "no-store");
  web.send_P(200, "text/html; charset=utf-8", PAGE);
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n=== ESP32-S3 Control V1.0.2 ===");
  settings.begin("s3-control", false);
  knownPspMac = settings.getString("psp_mac", "");
  selectedApChannel = settings.getInt("channel", PSP_AP_CHANNEL);
  if (selectedApChannel != 1 && selectedApChannel != 6 && selectedApChannel != 11) selectedApChannel = PSP_AP_CHANNEL;
  adminKey = settings.getString("admin", "");
  relayKey = settings.getString("relay", "");
  if (adminKey.length() != 32) { adminKey=createRandomToken(); settings.putString("admin",adminKey); }
  if (relayKey.length() != 32) { relayKey=createRandomToken(); settings.putString("relay",relayKey); }
  Serial.print("ADMIN KEY (keep private): "); Serial.println(adminKey);
  Serial.print("MAC REPORT KEY (keep private): "); Serial.println(relayKey);
  Serial.println("Keep credentials off the open PSP Wi-Fi. They survive restart.");

  upstreamConfigured = strlen(UPSTREAM_SSID) > 0;
  WiFi.setSleep(false);
  WiFi.mode(upstreamConfigured ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  if (!WiFi.softAP(PSP_AP_SSID, nullptr, selectedApChannel, 0, PSP_AP_MAX_CLIENTS)) {
    Serial.println("FATAL: softAP failed");
  }
  // Leave support for legacy 802.11b enabled; mixed b/g/n for newer phones too.
  esp_err_t wifiProto = esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  logEvent(wifiProto == ESP_OK ? "info" : "warn", wifiProto == ESP_OK ? "Legacy 802.11b/g/n AP protocol enabled" : "Wi-Fi protocol config rejected");
  logEvent("info", String("Open PSP Wi-Fi started: ") + PSP_AP_SSID + " / " + WiFi.softAPIP().toString());
  if (upstreamConfigured) {
    WiFi.setAutoReconnect(true);
    WiFi.begin(UPSTREAM_SSID, UPSTREAM_PASSWORD);
    logEvent("info", String("Connecting to upstream Wi-Fi: ") + UPSTREAM_SSID);
  } else logEvent("warn", "Upstream Wi-Fi not configured; Mac bridge disabled");

  const char *headers[] = {"X-Admin-Key", "X-Relay-Key"};
  web.collectHeaders(headers, 2);
  web.on("/", HTTP_GET, servePage);
  web.on("/api/status", HTTP_GET, handleStatus);
  web.on("/api/logs", HTTP_GET, handleLogs);
  web.on("/api/psp-mac", HTTP_POST, handlePspMac);
  web.on("/api/restart", HTTP_POST, handleRestart);
  web.on("/api/ap-channel", HTTP_POST, handleApChannel);
  web.on("/api/relay/report", HTTP_POST, handleMacReport);
  web.onNotFound([](){ sendError(404, "Not found"); });
  web.begin();
  bridgeServer.begin();
  logEvent("info", "Web dashboard ready: http://192.168.4.1");
  logEvent("info", String("PSPAI TCP relay listener on port ") + PSPAI_BRIDGE_PORT);
  String chipName = ESP.getChipModel();
  Serial.printf("Chip=%s Flash=%lu MB PSRAM=%lu MB\n",
    chipName.c_str(), (unsigned long)(ESP.getFlashChipSize()/1048576),
    (unsigned long)(ESP.getPsramSize()/1048576));
}

void loop() {
  web.handleClient();
  serviceBridge();
  uint32_t now = millis();
  if (now - lastStationScan >= STATUS_INTERVAL_MS) { lastStationScan=now; scanStations(); }
  if (upstreamConfigured && !bridgeActive && now - lastHealthAt >= HEALTH_INTERVAL_MS) {
    checkRelayHealth();
    // Prevent fast repeat on the first health failure.
    if (lastHealthAt == 0) lastHealthAt = millis();
  }
  if (nextRestartAt && (int32_t)(millis() - nextRestartAt) >= 0) {
    Serial.flush(); ESP.restart();
  }
  delay(2);
}
