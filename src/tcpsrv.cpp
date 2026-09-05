#include "tcpsrv.h"
#include "config.h"
#include "commands.h"
#include <WiFi.h>

namespace Tcp {

static WiFiServer server(TCP_PORT);
static WiFiClient clients[TCP_MAX_CLIENTS];
static String     bufs[TCP_MAX_CLIENTS];
static bool       started = false;

// ---------------------------------------------------------------------------
// Telnet protocol handling.
//
// A real telnet client opens the session with IAC negotiation: 0xFF followed by
// a command and an option byte. Treating those as command text glued them onto
// the user's first lines - "help" arriving as FF FD 'help' - which printed as
// "help" but never compared equal, so the first several commands looked like
// they were ignored. (nc never showed this, because nc sends no negotiation.)
//
// We strip the sequences and actively REFUSE every option (DO -> WONT,
// WILL -> DONT), so the client stops asking instead of retrying.
// ---------------------------------------------------------------------------
#define TN_IAC   255
#define TN_SB    250
#define TN_SE    240
#define TN_WILL  251
#define TN_WONT  252
#define TN_DO    253
#define TN_DONT  254

enum TState : uint8_t { T_TEXT, T_IAC, T_OPT, T_SUB, T_SUB_IAC };
static uint8_t tState[TCP_MAX_CLIENTS];
static uint8_t tCmd[TCP_MAX_CLIENTS];

void begin() {
  if (started) return;
  server.begin();
  server.setNoDelay(true);
  started = true;
}

void tick() {
  if (!started) return;

  // Accept - the listener is bound to all interfaces, so this works while the
  // config portal's AP is up just as well as on the client network.
  if (server.hasClient()) {
    int slot = -1;
    for (int i = 0; i < TCP_MAX_CLIENTS; i++) {
      if (!clients[i] || !clients[i].connected()) { slot = i; break; }
    }
    WiFiClient incoming = server.accept();
    if (slot < 0) {
      incoming.println("busy - too many clients");
      incoming.stop();
    } else {
      if (clients[slot]) clients[slot].stop();
      clients[slot] = incoming;
      bufs[slot]    = "";
      tState[slot]  = T_TEXT;      // fresh negotiation state per connection
      tCmd[slot]    = 0;
      clients[slot].println("LEDPanel ready - type 'help'");
    }
  }

  for (int i = 0; i < TCP_MAX_CLIENTS; i++) {
    if (!clients[i] || !clients[i].connected()) continue;

    while (clients[i].available()) {
      const uint8_t b = (uint8_t)clients[i].read();

      switch (tState[i]) {
        case T_IAC:
          if      (b == TN_IAC) tState[i] = T_TEXT;                 // escaped FF
          else if (b == TN_SB)  tState[i] = T_SUB;
          else if (b >= TN_WILL && b <= TN_DONT) { tCmd[i] = b; tState[i] = T_OPT; }
          else                  tState[i] = T_TEXT;                 // 2-byte cmd
          continue;

        case T_OPT: {                                               // refuse it
          const uint8_t resp[3] = {
            TN_IAC,
            (uint8_t)(tCmd[i] == TN_DO   ? TN_WONT :
                      tCmd[i] == TN_WILL ? TN_DONT : 0),
            b
          };
          if (resp[1]) clients[i].write(resp, 3);
          tState[i] = T_TEXT;
          continue;
        }

        case T_SUB:
          if (b == TN_IAC) tState[i] = T_SUB_IAC;
          continue;

        case T_SUB_IAC:
          tState[i] = (b == TN_SE) ? T_TEXT : T_SUB;
          continue;

        default:
          break;
      }

      if (b == TN_IAC) { tState[i] = T_IAC; continue; }

      const char c = (char)b;
      if (c == '\n' || c == '\r') {
        if (bufs[i].length()) {
          const String reply = Cmd::handle(bufs[i]);
          if (reply.length()) clients[i].println(reply);
          bufs[i] = "";
        }
      } else if (c >= 0x20 && c < 0x7F && bufs[i].length() < 512) {
        bufs[i] += c;        // printable ASCII only - drop stray control bytes
      }
    }
  }
}

}  // namespace Tcp
