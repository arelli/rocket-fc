// Rocket WiFi: joins the ground station's access point (as a client) so the
// ground station can fetch the onboard logs at WiFi speed. Started by the
// console command "wifi join <ssid> <password>" (the ground station sends it
// by radio), only on the ground and never in flight mode on the pad: in
// LANDED, FAULT, or in bench mode. Stopped by "wifi off", a launch, or when
// the rocket leaves those conditions.
//
// HTTP (port 80, plain text):
//   GET /          firmware and endpoints
//   GET /flog      flash log sessions (FLOG lines, as "flog list")
//   GET /flog?s=N  flash log session N as CSV (s=all: everything)
//   GET /ram       RAM log of this boot as CSV (as "dump")
// Serving a log blocks the control loop for a few seconds (watchdog and pyro
// timing are kept): that is why WiFi is refused in flight mode on the pad.
#include <WebServer.h>
#include <WiFi.h>
#include <string.h>

#include "app.h"

using fc::FlightState;

namespace {

constexpr uint32_t kJoinTimeoutMs = 20000;

// Buffers output into ~1.4 KB chunks for WebServer::sendContent.
class ChunkOut : public Print {
 public:
  explicit ChunkOut(WebServer& s) : s_(s) {}
  size_t write(uint8_t c) override {
    buf_[n_++] = static_cast<char>(c);
    if (n_ == sizeof(buf_)) flush();
    return 1;
  }
  void flush() {
    if (n_) s_.sendContent(buf_, n_);
    n_ = 0;
  }

 private:
  WebServer& s_;
  char buf_[1400];
  size_t n_ = 0;
};

WebServer* server(void* p) { return static_cast<WebServer*>(p); }

}  // namespace

bool App::wifi_allowed() const {
  if (!safe_to_interrupt() || simulating()) return false;
  const FlightState s = fc_.state();
  return s == FlightState::Landed || s == FlightState::Fault || bench_;
}

void App::wifi_command(const char* a, const char* b, const char* c, uint32_t now) {
  if (a && !strcmp(a, "off")) {
    wifi_stop("off");
    return;
  }
  if (!a || strcmp(a, "join") != 0 || !b) {
    con.printf("wifi: %s%s (usage: wifi join <ssid> <password> | wifi off)\n",
               !wifi_on_ ? "off" : wifi_joined_ ? "joined ip=" : "connecting",
               wifi_joined_ ? WiFi.localIP().toString().c_str() : "");
    return;
  }
  if (!wifi_allowed()) {
    con.println("wifi: refused: only after landing, in FAULT, or in bench mode");
    return;
  }
  if (wifi_on_) wifi_stop("restart");
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(b, c ? c : "");
  wifi_on_ = true;
  wifi_joined_ = false;
  wifi_since_ = now;
  con.printf("wifi: connecting to %s\n", b);
}

void App::wifi_stop(const char* why) {
  if (!wifi_on_) {
    con.println("wifi: off");
    return;
  }
  if (web_) {
    server(web_)->stop();
    delete server(web_);
    web_ = nullptr;
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifi_on_ = wifi_joined_ = false;
  con.printf("wifi: off (%s)\n", why);
}

void App::wifi_poll(uint32_t now) {
  if (!wifi_on_) return;
  if (!wifi_allowed()) {
    const bool was = con.capturing();
    con.capture(true);  // tell the ground station too
    wifi_stop("not allowed any more");
    con.capture(was);
    return;
  }
  if (!wifi_joined_) {
    if (WiFi.status() == WL_CONNECTED) {
      wifi_joined_ = true;
      WebServer* s = new WebServer(80);
      web_ = s;
      s->on("/", [this, s]() {
        char t[200];
        snprintf(t, sizeof(t), "rocket-fc flight computer %s\n/flog  /flog?s=N  /flog?s=all  /ram\n", FC_GIT_REV);
        s->send(200, "text/plain", t);
      });
      s->on("/flog", [this, s]() {
        s->sendHeader("Access-Control-Allow-Origin", "*");
        s->setContentLength(CONTENT_LENGTH_UNKNOWN);
        s->send(200, "text/plain", "");
        ChunkOut o(*s);
        if (!s->hasArg("s")) {
          flog_list(o);
        } else {
          const String v = s->arg("s");
          flog_dump(o, v == "all" ? 0 : static_cast<uint32_t>(strtoul(v.c_str(), nullptr, 10)));
        }
        o.flush();
        s->sendContent("");
      });
      s->on("/ram", [this, s]() {
        s->sendHeader("Access-Control-Allow-Origin", "*");
        s->setContentLength(CONTENT_LENGTH_UNKNOWN);
        s->send(200, "text/plain", "");
        ChunkOut o(*s);
        dump_log(o);
        o.flush();
        s->sendContent("");
      });
      s->begin();
      // Asynchronous news for the ground station: send it by radio too.
      const bool was = con.capturing();
      con.capture(true);
      con.printf("wifi: joined ip=%s\n", WiFi.localIP().toString().c_str());
      con.capture(was);
    } else if (now - wifi_since_ > kJoinTimeoutMs) {
      const bool was = con.capturing();
      con.capture(true);
      wifi_stop("could not join (ssid/password? in range?)");
      con.capture(was);
    }
    return;
  }
  server(web_)->handleClient();
}
