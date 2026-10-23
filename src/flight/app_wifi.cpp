// Rocket WiFi: joins the ground station's access point (as a client) so the
// ground station can fetch the onboard logs at WiFi speed. Only on the ground
// and never in flight mode on the pad: in LANDED, FAULT, or in bench mode.
// Stopped by "wifi off", a launch, or when the rocket leaves those conditions.
//
// The password never crosses the radio in clear (fc/crypto.h):
//   wifi key <station public key hex>   -> prints "wifi: key <rocket public key hex>"
//   wifi join <ssid> <sealed password hex> <tag hex>   (within 60 s, once)
// The ground station's "rwifi" command does both steps.
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
#include "fc/crypto.h"

using fc::FlightState;

namespace {

constexpr uint32_t kJoinTimeoutMs = 20000;
constexpr uint32_t kKeyValidMs = 60000;

// One pending key exchange (cleared after one use).
uint8_t g_key[32];
bool g_have_key = false;
uint32_t g_key_at = 0;

void wipe(void* p, size_t n) {
  volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
  while (n--) *v++ = 0;
}

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

void App::wifi_command(const char* a, const char* b, const char* c, const char* d, uint32_t now) {
  if (a && !strcmp(a, "off")) {
    wifi_stop("off");
    return;
  }
  const bool key = a && !strcmp(a, "key") && b;
  const bool join = a && !strcmp(a, "join") && b && c;
  if (!key && !join) {
    con.printf("wifi: %s%s (the ground station's \"rwifi\" starts it; wifi off stops it)\n",
               !wifi_on_ ? "off" : wifi_joined_ ? "joined ip=" : "connecting",
               wifi_joined_ ? WiFi.localIP().toString().c_str() : "");
    return;
  }
  if (!wifi_allowed()) {
    con.println("wifi: refused: only after landing, in FAULT, or in bench mode");
    return;
  }
  if (key) {
    uint8_t peer[32];
    if (fc::from_hex(b, peer, sizeof(peer)) != 32) {
      con.println("wifi: bad key");
      return;
    }
    if (wifi_on_) wifi_stop("restart");
    // The WiFi radio on makes esp_random() a true random source.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    wifi_on_ = true;
    wifi_joined_ = false;
    wifi_since_ = now;
    uint8_t secret[32], pub[32], shared[32];
    fc::Sha256 h;  // mix the hardware RNG with timing, then hash
    for (int i = 0; i < 16; ++i) {
      const uint32_t r[2] = {esp_random(), micros()};
      h.update(reinterpret_cast<const uint8_t*>(r), sizeof(r));
    }
    h.final(secret);
    fc::x25519_public(pub, secret);
    fc::x25519(shared, secret, peer);
    fc::wifi_secret::derive_key(shared, set_.rf_key, g_key);
    g_have_key = true;
    g_key_at = now;
    wipe(secret, sizeof(secret));
    wipe(shared, sizeof(shared));
    char hex[65];
    fc::to_hex(pub, 32, hex);
    con.printf("wifi: key %s\n", hex);
    return;
  }
  // join <ssid> <sealed password> <tag>
  const char* tag_hex = d;
  uint8_t ct[fc::wifi_secret::kMaxPass], tag[fc::wifi_secret::kTag], want[fc::wifi_secret::kTag];
  const size_t n = fc::from_hex(c, ct, sizeof(ct));
  if (!g_have_key || now - g_key_at > kKeyValidMs) {
    con.println("wifi: refused: no key exchange (use rwifi on the ground station)");
    return;
  }
  g_have_key = false;  // one use, whatever happens
  if (n == 0 || !tag_hex || fc::from_hex(tag_hex, tag, sizeof(tag)) != sizeof(tag)) {
    wipe(g_key, sizeof(g_key));
    con.println("wifi: refused: bad request");
    return;
  }
  fc::wifi_secret::tag(g_key, b, ct, n, want);
  uint8_t diff = 0;
  for (size_t i = 0; i < sizeof(tag); ++i) diff |= static_cast<uint8_t>(tag[i] ^ want[i]);
  if (diff) {
    wipe(g_key, sizeof(g_key));
    con.println("wifi: refused: wrong key (rf_key differs?)");
    return;
  }
  char pass[fc::wifi_secret::kMaxPass + 1];
  fc::wifi_secret::crypt(g_key, ct, reinterpret_cast<uint8_t*>(pass), n);
  pass[n] = '\0';
  wipe(g_key, sizeof(g_key));
  WiFi.setSleep(false);
  WiFi.begin(b, pass);
  wipe(pass, sizeof(pass));
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
