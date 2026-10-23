#include "web.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <freertos/message_buffer.h>
#include <lwip/sockets.h>
#include <string.h>

#include "ui_html_gz.h"

namespace web {

namespace {

constexpr int kMaxEventClients = 4;
constexpr size_t kOutBuffer = 16 * 1024;
constexpr size_t kCmdBuffer = 2 * 1024;
constexpr size_t kMaxLine = 512;

MessageBufferHandle_t g_out = nullptr;  // loop -> web task (output lines)
MessageBufferHandle_t g_cmd = nullptr;  // web task -> loop (command lines)
volatile bool g_running = false;
volatile int g_clients = 0;
char g_rocket_ip[16] = {0};
portMUX_TYPE g_ip_mux = portMUX_INITIALIZER_UNLOCKED;


// Non-blocking send: gives up on a client that cannot take the data within 200 ms.
bool send_all(WiFiClient& c, const char* d, size_t n) {
  const int fd = c.fd();
  if (fd < 0) return false;
  size_t off = 0;
  const uint32_t t0 = millis();
  while (off < n) {
    const int r = ::send(fd, d + off, n - off, MSG_DONTWAIT);
    if (r > 0) {
      off += static_cast<size_t>(r);
    } else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (millis() - t0 > 200) return false;
      vTaskDelay(1);
    } else {
      return false;
    }
  }
  return true;
}

void send_text(WiFiClient& c, const char* s) { send_all(c, s, strlen(s)); }

// GET /rocket/<path>: fetch http://<rocket ip>/<path> and pass the answer on
// unchanged (HTTP/1.0: the rocket closes the connection at the end).
void proxy(WiFiClient& c, const String& path) {
  char ip[16];
  portENTER_CRITICAL(&g_ip_mux);
  memcpy(ip, g_rocket_ip, sizeof(ip));
  portEXIT_CRITICAL(&g_ip_mux);
  if (!ip[0]) {
    send_text(c, "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
                 "the rocket is not on this WiFi (send rwifi first)\n");
    return;
  }
  WiFiClient r;
  if (!r.connect(ip, 80, 3000)) {
    send_text(c, "HTTP/1.1 504 Gateway Timeout\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n"
                 "the rocket does not answer on WiFi\n");
    return;
  }
  r.printf("GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path.c_str(), ip);
  uint8_t buf[1460];
  uint32_t t_last = millis();
  while (r.connected() || r.available()) {
    const int n = r.read(buf, sizeof(buf));
    if (n > 0) {
      if (c.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) break;  // browser gone
      t_last = millis();
    } else {
      if (millis() - t_last > 15000) break;
      vTaskDelay(1);
    }
  }
  r.stop();
}

void serve_ui(WiFiClient& c, bool head) {
  char h[256];
  snprintf(h, sizeof(h),
           "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Encoding: gzip\r\n"
           "Content-Length: %u\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n",
           static_cast<unsigned>(kUiHtmlGzLen));
  send_text(c, h);
  if (!head) {
    // The UI is ~30 KB: write it in blocks, allowing for a slow phone.
    for (size_t off = 0; off < kUiHtmlGzLen;) {
      const size_t n = kUiHtmlGzLen - off < 1460 ? kUiHtmlGzLen - off : 1460;
      if (c.write(kUiHtmlGz + off, n) != n) break;
      off += n;
    }
  }
}

// Reads one HTTP request and answers it. Returns true if the client became
// an event stream (kept open by the caller).
bool handle(WiFiClient& c) {
  c.setTimeout(2);  // seconds, for the socket
  String first = c.readStringUntil('\n');
  first.trim();
  size_t content_length = 0;
  for (int i = 0; i < 40; ++i) {  // headers
    String h = c.readStringUntil('\n');
    h.trim();
    if (h.length() == 0) break;
    if (h.startsWith("Content-Length:") || h.startsWith("content-length:")) content_length = h.substring(15).toInt();
  }
  const int sp1 = first.indexOf(' ');
  const int sp2 = first.indexOf(' ', sp1 + 1);
  if (sp1 < 0) return false;
  const String method = first.substring(0, sp1);
  const String path = sp2 > sp1 ? first.substring(sp1 + 1, sp2) : first.substring(sp1 + 1);

  if (method == "GET" && path.startsWith("/events")) {
    send_text(c, "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\n"
                 "Connection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\n\r\nretry: 2000\n\n");
    return true;
  }
  if (method == "POST" && path.startsWith("/cmd")) {
    if (content_length > kCmdBuffer / 2) content_length = kCmdBuffer / 2;
    char body[kCmdBuffer / 2 + 1];
    const size_t n = c.readBytes(body, content_length);
    body[n] = '\0';
    // One message per line; an empty line is sent as " " (the ground station
    // answers it by re-printing the CSV headers).
    char* save = nullptr;
    int sent = 0;
    for (char* l = strtok_r(body, "\n", &save); l; l = strtok_r(nullptr, "\n", &save), ++sent) {
      size_t len = strlen(l);
      while (len && l[len - 1] == '\r') l[--len] = '\0';
      if (len == 0) {
        l = const_cast<char*>(" ");
        len = 1;
      }
      xMessageBufferSend(g_cmd, l, len, pdMS_TO_TICKS(50));
    }
    if (sent == 0) xMessageBufferSend(g_cmd, " ", 1, 0);  // an empty command line
    send_text(c, "HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
    return false;
  }
  if (method == "GET" && path.startsWith("/rocket/")) {
    proxy(c, path.substring(7));
    return false;
  }
  if (method == "GET" || method == "HEAD") {
    serve_ui(c, method == "HEAD");  // any path: the UI (captive portal)
    return false;
  }
  send_text(c, "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n");
  return false;
}

void task(void*) {
  WiFiServer server(80);
  server.begin();
  server.setNoDelay(true);
  DNSServer dns;
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());  // every name -> this access point
  WiFiClient events[kMaxEventClients];
  char line[kMaxLine + 8];
  uint32_t last_ping = millis();

  for (;;) {
    dns.processNextRequest();
    WiFiClient c = server.available();
    if (c) {
      if (handle(c)) {
        int slot = -1;
        for (int i = 0; i < kMaxEventClients; ++i)
          if (!events[i].connected()) slot = i;
        if (slot < 0) {  // full: replace the oldest
          events[0].stop();
          slot = 0;
        }
        events[slot] = c;
      } else {
        c.stop();
      }
    }

    // Fan the output lines out to every event stream.
    for (int k = 0; k < 64; ++k) {
      const size_t n = xMessageBufferReceive(g_out, line + 6, kMaxLine, 0);
      if (n == 0) break;
      memcpy(line, "data: ", 6);
      line[6 + n] = '\n';
      line[7 + n] = '\n';
      for (auto& e : events)
        if (e.connected() && !send_all(e, line, n + 8)) e.stop();
    }
    if (millis() - last_ping > 15000) {
      last_ping = millis();
      for (auto& e : events)
        if (e.connected() && !send_all(e, ": ping\n\n", 8)) e.stop();
    }
    int n = 0;
    for (auto& e : events) n += e.connected() ? 1 : 0;
    g_clients = n;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

const char* check_ssid(const char* s) {
  const size_t n = s ? strlen(s) : 0;
  if (n < 1 || n > 32) return "SSID must be 1..32 characters";
  for (const char* p = s; *p; ++p)
    if (*p < 0x21 || *p > 0x7E) return "SSID: printable characters, no spaces";
  return nullptr;
}

const char* check_pass(const char* s) {
  const size_t n = s ? strlen(s) : 0;
  if (n < 8 || n > 63) return "password must be 8..63 characters (WPA2)";
  for (const char* p = s; *p; ++p)
    if (*p < 0x21 || *p > 0x7E) return "password: printable characters, no spaces";
  return nullptr;
}

Config load_config() {
  Config c;
  Preferences p;
  p.begin("rocketfc", false);
  c.on = p.getBool("wifi_on", true);
  c.channel = p.getUChar("wifi_ch", 6);
  String ssid = p.getString("wifi_ssid", "");
  String pass = p.getString("wifi_pass", "");
  if (check_ssid(ssid.c_str())) {
    const uint64_t mac = ESP.getEfuseMac();
    char s[33];
    snprintf(s, sizeof(s), "rocketfc-%02X%02X", static_cast<unsigned>((mac >> 32) & 0xFF),
             static_cast<unsigned>((mac >> 40) & 0xFF));
    ssid = s;
    p.putString("wifi_ssid", ssid);
  }
  if (check_pass(pass.c_str())) {
    // First boot: a random password, stored. Printed over USB ("wifi").
    static const char kChars[] = "abcdefghjkmnpqrstuvwxyz23456789";
    char s[11];
    for (int i = 0; i < 10; ++i) s[i] = kChars[esp_random() % (sizeof(kChars) - 1)];
    s[10] = '\0';
    pass = s;
    p.putString("wifi_pass", pass);
  }
  p.end();
  strncpy(c.ssid, ssid.c_str(), sizeof(c.ssid) - 1);
  strncpy(c.pass, pass.c_str(), sizeof(c.pass) - 1);
  if (c.channel < 1 || c.channel > 13) c.channel = 6;
  return c;
}

bool save_config(const Config& c) {
  Preferences p;
  if (!p.begin("rocketfc", false)) return false;
  bool ok = p.putBool("wifi_on", c.on) > 0;
  ok &= p.putUChar("wifi_ch", c.channel) > 0;
  ok &= p.putString("wifi_ssid", c.ssid) > 0;
  ok &= p.putString("wifi_pass", c.pass) > 0;
  p.end();
  return ok;
}

bool begin(const Config& c) {
  if (!c.on) return false;
  g_out = xMessageBufferCreate(kOutBuffer);
  g_cmd = xMessageBufferCreate(kCmdBuffer);
  if (!g_out || !g_cmd) return false;
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(c.ssid, c.pass, c.channel, 0, 4)) return false;
  MDNS.begin("rocketfc");
  MDNS.addService("http", "tcp", 80);
  g_running = xTaskCreatePinnedToCore(task, "web", 8192, nullptr, 1, nullptr, 0) == pdPASS;
  return g_running;
}

bool running() { return g_running; }
int clients() { return g_clients; }

void publish(const char* line, size_t len) {
  if (!g_running || len == 0) return;
  if (len > kMaxLine) len = kMaxLine;
  xMessageBufferSend(g_out, line, len, 0);  // dropped if the browsers are not keeping up
}

bool poll_command(char* buf, size_t cap) {
  if (!g_running || cap < 2) return false;
  const size_t n = xMessageBufferReceive(g_cmd, buf, cap - 1, 0);
  if (n == 0) return false;
  buf[n] = '\0';
  return true;
}

void set_rocket_ip(const char* ip) {
  portENTER_CRITICAL(&g_ip_mux);
  strncpy(g_rocket_ip, ip, sizeof(g_rocket_ip) - 1);
  g_rocket_ip[sizeof(g_rocket_ip) - 1] = '\0';
  for (char* p = g_rocket_ip; *p; ++p)
    if (!((*p >= '0' && *p <= '9') || *p == '.')) *p = '\0';
  portEXIT_CRITICAL(&g_ip_mux);
}

}  // namespace web
