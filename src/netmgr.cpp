#include "netmgr.h"
#include "config.h"
#include "display_mgr.h"
#include "commands.h"
#include "webui.h"
#include <ESPmDNS.h>
#include <Update.h>
#include "otamgr.h"
#include "sleepsched.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>

namespace Net {

static Preferences prefs;
static WebServer   server(80);
static DNSServer   dns;
static bool        g_dnsUp = false;

static NetState g_state       = NET_PORTAL;
static uint32_t g_stateSince  = 0;
static uint32_t g_dropSince   = 0;    // when STA first went away
static uint32_t g_nextScan    = 0;
static bool     g_scanRunning = false;
static bool     g_routes      = false;

static String g_ssid, g_pass, g_apSsid, g_tz = DEFAULT_TZ;

// Scan results are cached from scans performed while NO client is attached, so
// the config page can list networks without taking the radio off-channel and
// dropping the very phone that asked for the page.
#define SCAN_CACHE_MAX 24
static String   g_scanSsid[SCAN_CACHE_MAX];
static int32_t  g_scanRssi[SCAN_CACHE_MAX];
static uint8_t  g_scanCount = 0;
static uint32_t g_scanAt    = 0;

static void startSta();
static void startPortal();
static bool g_serverUp = false;

// Heap is the prime suspect for the hang: Bluedroid + WiFi STA + SoftAP +
// WebServer + DNS all live at once on a classic ESP32. Log it at every
// transition so the trend is visible rather than assumed.
static void logHeap(const char *where) {
  Serial.printf("[net] %-10s heap=%u min=%u largest=%u\n", where,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
}

// ---------------------------------------------------------------------------

static String macSuffix() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char s[8];
  snprintf(s, sizeof(s), "%02X%02X", mac[4], mac[5]);
  return String(s);
}

static String htmlEscape(const String &in) {
  String o;
  o.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    const char c = in[i];
    if      (c == '&')  o += "&amp;";
    else if (c == '<')  o += "&lt;";
    else if (c == '>')  o += "&gt;";
    else if (c == '"')  o += "&quot;";
    else if (c == '\'') o += "&#39;";
    else                o += c;
  }
  return o;
}

// ---------------------------------------------------------------------------
// Config portal pages
// ---------------------------------------------------------------------------

static void handleRoot() {
  // Deliberately NO scan here: scanning drops AP clients, which would kick the
  // phone off mid-request. We render the cache filled while nobody was attached.
  String p;
  p.reserve(2048);
  p += F("<!doctype html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>LED Panel Setup</title><style>"
         "body{font-family:system-ui,sans-serif;margin:0;padding:1.2rem;"
         "background:#111;color:#eee}h1{font-size:1.2rem;margin:0 0 1rem}"
         "label{display:block;margin:.7rem 0 .2rem;font-size:.9rem}"
         "input,select{width:100%;padding:.6rem;border-radius:6px;border:1px solid #444;"
         "background:#1c1c1c;color:#eee;font-size:1rem;box-sizing:border-box}"
         "button{margin-top:1.1rem;width:100%;padding:.75rem;border:0;border-radius:6px;"
         "background:#2d7;color:#062;font-weight:600;font-size:1rem}"
         ".m{color:#999;font-size:.8rem;margin-top:1rem}</style></head><body>"
         "<h1>LED Panel Wi-Fi Setup</h1><form method='POST' action='/save'>"
         "<label>Network</label><select name='ssid'>");

  if (g_scanCount == 0) {
    p += F("<option value=''>-- none found, type it below --</option>");
  } else {
    for (uint8_t i = 0; i < g_scanCount; i++) {
      p += "<option value='" + htmlEscape(g_scanSsid[i]) + "'";
      if (g_scanSsid[i] == g_ssid) p += " selected";
      p += ">" + htmlEscape(g_scanSsid[i]) + " (" + String(g_scanRssi[i]) +
           " dBm)</option>";
    }
  }

  p += F("</select>"
         "<label>Or type the network name</label>"
         "<input type='text' name='ssid_manual' placeholder='overrides the list if filled'>"
         "<label>Password</label>"
         "<input type='password' name='pass' placeholder='leave blank if open'>"
         "<button type='submit'>Save &amp; Connect</button></form>");
  p += "<p class='m'>Saved network: " +
       (g_ssid.isEmpty() ? String("(none)") : htmlEscape(g_ssid));
  if (g_scanAt) p += "<br>Network list is " + String((millis() - g_scanAt) / 1000) +
                     "s old; it refreshes whenever no one is connected here.";
  p += F("<br>Scanning is paused while you are connected - one radio cannot do "
         "both without dropping you.</p></body></html>");

  server.send(200, "text/html", p);
}

static void handleSave() {
  const String manual = server.arg("ssid_manual");
  const String s = manual.length() ? manual : server.arg("ssid");
  const String p = server.arg("pass");
  if (s.isEmpty()) {
    server.send(400, "text/html",
                F("<html><body><h3>SSID required</h3><a href='/'>back</a></body></html>"));
    return;
  }
  server.send(200, "text/html",
              "<html><body style='font-family:system-ui;padding:1rem'>"
              "<h3>Saved</h3><p>Connecting to " + htmlEscape(s) +
              "&hellip; the panel will show its IP when it joins.</p>"
              "<p>If it fails, this page comes back automatically.</p></body></html>");
  setCreds(s, p);
}

static void handleStatus() {
  String j = "{\"state\":\"" + String(stateName()) + "\",\"ssid\":\"" + g_ssid +
             "\",\"ip\":\"" + ip() + "\"}";
  server.send(200, "application/json", j);
}

// Control GUI ---------------------------------------------------------------

static void handleControl() {
  server.send_P(200, "text/html", CONTROL_PAGE);
}

// Root serves the setup form while the portal is up (a captive-portal probe
// must land on something actionable), and the control GUI once connected.
static void handleIndex() {
  if (g_state == NET_PORTAL) handleRoot();
  else                       handleControl();
}

// The GUI drives the same parser as TCP and serial - one implementation of the
// control logic, so the surfaces cannot drift apart.
static void handleApiCmd() {
  const String c = server.arg("c");
  if (c.isEmpty()) { server.send(400, "text/plain", "missing ?c="); return; }
  server.send(200, "text/plain", Cmd::handle(c));
}

static void handleApiStatus() {
  String j = "{";
  j += "\"mode\":\"";   j += Display::modeName();
  j += "\",\"align\":\""; j += Display::justifyName();
  j += "\",\"scroll\":\""; j += Display::scrollName();
  j += "\",\"bright\":";  j += Display::intensity();
  j += ",\"speed\":";     j += Display::speed();
  j += ",\"cols\":";      j += Display::messageColumns();
  j += ",\"wifi\":\"";    j += stateName();
  j += "\",\"ip\":\"";    j += ip();
  j += "\",\"ssid\":\"";  j += (g_ssid.isEmpty() ? String("(none)") : g_ssid);
  j += "\"}";
  server.send(200, "application/json", j);
}

// Open the listening socket. Safe only after WiFi.mode() has been called.
// Firmware upload ----------------------------------------------------------
//
// Two callbacks: the second receives the file in chunks, the first runs once
// the whole body has arrived. Guarded by HTTP basic auth - without it anyone on
// the network could reflash the panel.

static bool otaAuthed() {
  if (server.authenticate("admin", OTA_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

static void handleUpdateDone() {
  if (!otaAuthed()) return;
  const bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(ok ? 200 : 500, "text/plain",
              ok ? "OK - rebooting into the new firmware" : "FAILED - old firmware kept");
  delay(300);
  if (ok) ESP.restart();
}

static void handleUpdateUpload() {
  HTTPUpload &up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    if (!server.authenticate("admin", OTA_PASSWORD)) return;   // ignore body
    Ota::uploadBegin();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN))
      Serial.printf("[ota] begin failed: %s\n", Update.errorString());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize)
      Serial.printf("[ota] write failed: %s\n", Update.errorString());
    Ota::uploadProgress(up.totalSize, 0);        // total unknown until the end
  } else if (up.status == UPLOAD_FILE_END) {
    const bool ok = Update.end(true);
    if (!ok) Serial.printf("[ota] end failed: %s\n", Update.errorString());
    Ota::uploadEnd(ok);
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Ota::uploadEnd(false);
  }
}

static void ensureServer() {
  if (!g_serverUp) { server.begin(); g_serverUp = true; }
}

static void ensureRoutes() {
  if (g_routes) return;
  server.on("/", HTTP_GET, handleIndex);
  server.on("/control", HTTP_GET, handleControl);
  server.on("/wifi", HTTP_GET, handleRoot);       // setup form, always reachable
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/api/cmd", HTTP_GET, handleApiCmd);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() {
    // Redirect unknown paths only while the portal is up - that is what makes a
    // phone's connectivity probe open the setup page. Once connected, a wrong
    // URL should be an honest 404 rather than a surprise redirect.
    if (g_state == NET_PORTAL) {
      server.sendHeader("Location", "/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "not found");
    }
  });
  g_routes = true;
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------

static void onConnected() {
  g_state      = NET_CONNECTED;
  g_stateSince = millis();
  g_dropSince  = 0;

  // Drop the AP once we are a client, per the spec's "switch to client".
  if (g_dnsUp) { dns.stop(); g_dnsUp = false; }
  if (WiFi.getMode() & WIFI_MODE_AP) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", g_tz.c_str(), 1);
  tzset();

  // http://ledpanel.local - saves hunting for the IP on a phone.
  if (MDNS.begin("ledpanel")) MDNS.addService("http", "tcp", 80);

  Display::setStatusMessage(WiFi.localIP().toString());
  logHeap("connected");
}

static void startSta() {
  WiFi.mode(WIFI_STA);
  // Modem sleep lets the APB clock scale, which shifts the UART baud rate out
  // from under us. Keep the radio awake so clocks stay put.
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(g_ssid.c_str(), g_pass.c_str());
  ensureServer();            // WiFi.mode() has run, so this is safe now
  g_state      = NET_CONNECTING;
  g_stateSince = millis();
  Display::setStatusMessage("WIFI...");
  logHeap("sta");
}

static void startPortal() {
  g_apSsid = "LEDPanel-" + macSuffix();

  // AP_STA, not AP: the config page is served while the saved network is still
  // hunted for in the background, so an outage that fixes itself recovers
  // without anyone touching the portal.
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  // Check the result: the panel used to announce the AP unconditionally, which
  // told us nothing about whether it actually came up.
  const bool apOk = WiFi.softAP(g_apSsid.c_str(), AP_PASSWORD);
  Serial.printf("[net] softAP %s ssid=%s ip=%s ch=%d mode=%d\n",
                apOk ? "OK" : "FAILED", g_apSsid.c_str(),
                WiFi.softAPIP().toString().c_str(), (int)WiFi.channel(),
                (int)WiFi.getMode());

  ensureRoutes();
  ensureServer();            // once only - begin() leaks a socket per call

  // Captive portal: answer every DNS query with our own address so the phone's
  // connectivity check lands on us. Without this the check fails, the OS marks
  // the network as having no internet, and silently drops the association.
  if (g_dnsUp) { dns.stop(); g_dnsUp = false; }   // never stack two instances
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  g_dnsUp = true;
  logHeap("portal");

  g_state      = NET_PORTAL;
  g_stateSince = millis();
  g_nextScan   = millis() + 2000;
  g_scanRunning = false;

  Display::setStatusMessage("AP " + g_apSsid);
}

// While the portal is up, keep looking for the saved SSID and rejoin it the
// moment it reappears.
//
// The ESP32 has a single radio: a scan takes it off the AP's channel and drops
// associated clients, and joining a network on a different channel drags the AP
// along with it. So while somebody is actually connected to the portal, stop
// hunting entirely - they are mid-configuration and a dropped connection is far
// worse than a delayed reconnect. Hunting resumes as soon as they disconnect.
static void portalHunt() {
  if (g_ssid.isEmpty()) return;


  if (WiFi.softAPgetStationNum() > 0) {
    if (g_scanRunning) { WiFi.scanDelete(); g_scanRunning = false; }
    g_nextScan = millis() + PORTAL_RESCAN_MS;
    return;
  }

  if (g_scanRunning) {
    const int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    g_scanRunning = false;
    if (n > 0) {
      // Cache for the config page, and rejoin if the saved network is back.
      g_scanCount = 0;
      bool found  = false;
      for (int i = 0; i < n; i++) {
        const String s = WiFi.SSID(i);
        if (s.isEmpty()) continue;
        if (g_scanCount < SCAN_CACHE_MAX) {
          g_scanSsid[g_scanCount] = s;
          g_scanRssi[g_scanCount] = WiFi.RSSI(i);
          g_scanCount++;
        }
        if (s == g_ssid) found = true;
      }
      g_scanAt = millis();
      if (found) WiFi.begin(g_ssid.c_str(), g_pass.c_str());
    }
    WiFi.scanDelete();
    g_nextScan = millis() + PORTAL_RESCAN_MS;
    return;
  }

  if ((int32_t)(millis() - g_nextScan) >= 0) {
    g_nextScan = millis() + PORTAL_RESCAN_MS;

    // Scan first, and only call WiFi.begin() once the saved SSID is actually
    // seen (handled in the scan-complete branch above).
    //
    // Do NOT call WiFi.begin() blind on a timer. setAutoReconnect(true) is
    // already retrying underneath us, so blind calls stack on top of in-flight
    // attempts, and in AP_STA each one drags the AP onto a new channel and
    // rebuilds it. An earlier version of this function did exactly that and
    // reintroduced the freeze.
    WiFi.scanNetworks(true, false);   // async
    g_scanRunning = true;
  }
}

// ---------------------------------------------------------------------------

void begin() {
  prefs.begin(NVS_NAMESPACE, false);
  g_ssid = prefs.getString("ssid", "");
  g_pass = prefs.getString("pass", "");
  g_tz   = prefs.getString("tz", DEFAULT_TZ);

  setenv("TZ", g_tz.c_str(), 1);
  tzset();

  // Register routes now, but do NOT call server.begin() yet: opening the
  // listening socket before WiFi is initialised takes a null semaphore inside
  // lwIP and aborts with "assert failed: xQueueSemaphoreTake". The socket is
  // opened by ensureServer() once a WiFi mode is actually up.
  ensureRoutes();

  WiFi.persistent(false);
  if (g_ssid.isEmpty()) startPortal();
  else                  startSta();
}

void tick() {
  if (g_state == NET_PORTAL) {
    if (g_dnsUp) dns.processNextRequest();
    portalHunt();
    if (WiFi.status() == WL_CONNECTED) onConnected();
    return;
  }

  // Control GUI is reachable in every state - but never touch the server
  // before its socket exists (same trap as the premature begin()).
  if (g_serverUp) server.handleClient();

  if (g_state == NET_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) { onConnected(); return; }
    if (millis() - g_stateSince > WIFI_CONNECT_TIMEOUT_MS) startPortal();
    return;
  }

  // NET_CONNECTED - allow auto-reconnect a grace window before falling back,
  // so a brief AP glitch does not bounce everyone onto the config portal.
  if (WiFi.status() != WL_CONNECTED) {
    if (g_dropSince == 0) {
      g_dropSince = millis();
      Display::setStatusMessage("WIFI LOST");
    } else if (millis() - g_dropSince > WIFI_DROP_GRACE_MS) {
      startPortal();
    }
  } else if (g_dropSince != 0) {
    g_dropSince = 0;
    Display::setStatusMessage(WiFi.localIP().toString());
  }
}

NetState state() { return g_state; }
const char *stateName() {
  switch (g_state) {
    case NET_CONNECTED:  return "connected";
    case NET_CONNECTING: return "connecting";
    default:             return "portal";
  }
}
bool isConnected() { return g_state == NET_CONNECTED; }

String ip() {
  if (g_state == NET_CONNECTED) return WiFi.localIP().toString();
  return WiFi.softAPIP().toString();
}
String ssid()   { return g_ssid; }
String apSsid() { return g_apSsid; }

void setCreds(const String &s, const String &p) {
  g_ssid = s;
  g_pass = p;
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  startSta();
}

void clearCreds() {
  g_ssid = "";
  g_pass = "";
  prefs.remove("ssid");
  prefs.remove("pass");
  startPortal();
}

void forcePortal() { startPortal(); }

void setTz(const String &t) {
  g_tz = t;
  prefs.putString("tz", t);
  setenv("TZ", g_tz.c_str(), 1);
  tzset();
}
String tz() { return g_tz; }

}  // namespace Net
