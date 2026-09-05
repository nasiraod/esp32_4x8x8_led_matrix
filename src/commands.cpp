#include "commands.h"
#include "config.h"
#include "display_mgr.h"
#include "netmgr.h"

namespace Cmd {

// Splits "verb rest-of-line" - the remainder is kept intact so that message
// text and strftime formats may contain spaces.
static void splitRaw(const String &in, String &head, String &rest) {
  const int sp = in.indexOf(' ');
  if (sp < 0) { head = in; rest = ""; }
  else        { head = in.substring(0, sp); rest = in.substring(sp + 1); rest.trim(); }
}

static void split(const String &in, String &verb, String &rest) {
  splitRaw(in, verb, rest);
  verb.toLowerCase();   // keywords only - never use this where case matters
}

static String statusLine() {
  String s;
  s += "mode=";    s += Display::modeName();
  s += " align=";  s += Display::justifyName();
  s += " scroll="; s += Display::scrollName();
  s += "(";        s += Display::scrollingNow() ? "scrolling" : "static";
  s += ") cols=";  s += Display::messageColumns();
  s += "/";        s += PANEL_COLUMNS;
  s += " speed=";  s += Display::speed();
  s += " bright="; s += Display::intensity();
  s += " font=";  s += Display::textFontName();
  s += " clock="; s += Display::clockStyleName();
  s += "/";       s += Display::clockFontName();
  s += " hw=";     s += Display::hwType();
  s += " screen="; s += Display::screen() ? "on" : "off";
  s += " flip=";   s += Display::flip() ? "on" : "off";
  s += " mirror="; s += Display::mirror() ? "on" : "off";
  s += " sw=";      s += Display::swRunning() ? "run" : "stop";
  s += "/";         s += Display::swElapsedMs();
  s += "ms wifi=";  s += Net::stateName();
  s += " ip=";      s += Net::ip();
  s += " ssid=";    s += Net::ssid().isEmpty() ? String("(none)") : Net::ssid();
  return s;
}

static String helpText() {
  return F(
    "commands: "
    "text <msg> | mode text|stopwatch|clock | "
    "scroll on|off|auto | align left|center|right | font normal|narrow | "
    "speed <ms> | bright <0-15> | "
    "screen on|off|toggle | flip on|off | mirror on|off | hw <0-7> | "
    "sw start|stop|toggle|reset | clock hmbar|hmblink|ms|hms|custom|font stock|big | "
    "tz <posix> | "
    "wifi status|set <ssid> [pass]|clear|portal | status | reboot");
}

String handle(const String &raw) {
  String line = raw;
  line.trim();
  if (line.isEmpty()) return "";

  String verb, rest;
  split(line, verb, rest);

  if (verb == "help" || verb == "?") return helpText();
  if (verb == "status")              return statusLine();

  if (verb == "text") {
    if (rest.isEmpty()) return "ERR text needs a message";
    Display::setMessage(rest);
    Display::setMode(MODE_TEXT);
    return "OK text";
  }

  if (verb == "mode") {
    String m = rest; m.toLowerCase();
    if      (m == "text")      Display::setMode(MODE_TEXT);
    else if (m == "stopwatch" || m == "sw") Display::setMode(MODE_STOPWATCH);
    else if (m == "clock" || m == "time" || m == "date") Display::setMode(MODE_CLOCK);
    else return "ERR mode text|stopwatch|clock";
    return String("OK mode ") + Display::modeName();
  }

  if (verb == "scroll") {
    String v = rest; v.toLowerCase();
    if      (v == "on")   Display::setScroll(SCROLL_ON);
    else if (v == "off")  Display::setScroll(SCROLL_OFF);
    else if (v == "auto") Display::setScroll(SCROLL_AUTO);
    else return "ERR scroll on|off|auto";
    return String("OK scroll ") + Display::scrollName();
  }

  if (verb == "font") {
    String v = rest; v.toLowerCase();
    if (v.isEmpty()) return String("font=") + Display::textFontName() +
                            " (normal|narrow; narrow is 3x8 uppercase)";
    if      (v == "normal") Display::setTextFont(FONT_NORMAL);
    else if (v == "narrow") Display::setTextFont(FONT_NARROW);
    else return "ERR font normal|narrow";
    return String("OK font ") + Display::textFontName();
  }

  if (verb == "align") {
    String v = rest; v.toLowerCase();
    if      (v == "left")   Display::setJustify(JUST_LEFT);
    else if (v == "center" || v == "centre") Display::setJustify(JUST_CENTER);
    else if (v == "right")  Display::setJustify(JUST_RIGHT);
    else return "ERR align left|center|right";
    return String("OK align ") + Display::justifyName();
  }

  // Screen power - for turning the panel off overnight without unplugging it.
  if (verb == "screen" || verb == "display") {
    String v = rest; v.toLowerCase();
    if      (v == "on")     Display::setScreen(true);
    else if (v == "off")    Display::setScreen(false);
    else if (v == "toggle") Display::setScreen(!Display::screen());
    else if (!v.isEmpty())  return "ERR screen on|off|toggle";
    return String("OK screen ") + (Display::screen() ? "on" : "off");
  }

  // Upside-down mounting.
  if (verb == "flip") {
    String v = rest; v.toLowerCase();
    if      (v == "on")  Display::setFlip(true);
    else if (v == "off") Display::setFlip(false);
    else if (v == "toggle") Display::setFlip(!Display::flip());
    else if (!v.isEmpty()) return "ERR flip on|off|toggle";
    return String("OK flip ") + (Display::flip() ? "on" : "off");
  }

  // Panel tuning, kept at runtime so orientation never needs another reflash.
  if (verb == "mirror") {
    String v = rest; v.toLowerCase();
    if      (v == "on")  Display::setMirror(true);
    else if (v == "off") Display::setMirror(false);
    else return "ERR mirror on|off";
    return String("OK mirror ") + (Display::mirror() ? "on" : "off");
  }

  if (verb == "hw") {
    if (rest.isEmpty()) return String("hw=") + Display::hwType() + " (0-7)";
    const long v = rest.toInt();
    if (v < 0 || v > 7) return "ERR hw 0-7";
    Display::setHwType((uint8_t)v);
    return String("OK hw ") + Display::hwType();
  }

  if (verb == "speed") {
    const long v = rest.toInt();
    if (v < 5 || v > 1000) return "ERR speed 5-1000 ms";
    Display::setSpeed((uint16_t)v);
    return String("OK speed ") + Display::speed();
  }

  if (verb == "bright" || verb == "brightness") {
    const long v = rest.toInt();
    if (v < 0 || v > 15) return "ERR bright 0-15";
    Display::setIntensity((uint8_t)v);
    return String("OK bright ") + Display::intensity();
  }

  if (verb == "sw") {
    String v = rest; v.toLowerCase();
    if      (v == "start")  Display::swStart();
    else if (v == "stop")   Display::swStop();
    else if (v == "toggle") Display::swToggle();
    else if (v == "reset")  Display::swReset();
    else return "ERR sw start|stop|toggle|reset";
    Display::setMode(MODE_STOPWATCH);
    return String("OK sw ") + (Display::swRunning() ? "running " : "stopped ") +
           Display::swElapsedMs() + "ms";
  }

  if (verb == "clock") {
    String sub, args;
    split(rest, sub, args);
    if (sub.isEmpty() || sub == "status")
      return String("clock style=") + Display::clockStyleName() +
             " format=" + Display::clockFormat() +
             " font=" + Display::clockFontName() +
             " (hmbar|hmblink|ms|hms|custom <strftime>|font stock|big)";
    if (sub == "font") {
      String f = args; f.toLowerCase();
      if      (f == "stock" || f == "normal") Display::setClockFont(CFONT_STOCK);
      else if (f == "big")                    Display::setClockFont(CFONT_BIG);
      else return "ERR clock font stock|big";
      return String("OK clock font ") + Display::clockFontName() +
             (Display::clockFont() == CFONT_BIG && Display::clockStyle() == CLK_HM_BAR
                ? " (bar off - row 7 used by the digits)" : "");
    }
    if (sub == "hmbar")   { Display::setClockStyle(CLK_HM_BAR);   }
    else if (sub == "hmblink") { Display::setClockStyle(CLK_HM_BLINK); }
    else if (sub == "ms")      { Display::setClockStyle(CLK_MS);      }
    else if (sub == "hms")     { Display::setClockStyle(CLK_HMS);     }
    else if (sub == "custom") {
      if (args.isEmpty()) return "ERR clock custom <strftime>";
      Display::setClockFormat(args);
    }
    else return "ERR clock hmbar|hmblink|ms|hms|custom <strftime>";
    Display::setMode(MODE_CLOCK);
    return String("OK clock ") + Display::clockStyleName();
  }

  if (verb == "tz") {
    if (rest.isEmpty()) return String("tz=") + Net::tz();
    Net::setTz(rest);
    return String("OK tz ") + Net::tz();
  }

  if (verb == "wifi") {
    String sub, args;
    split(rest, sub, args);
    if (sub.isEmpty() || sub == "status")
      return String("wifi=") + Net::stateName() + " ip=" + Net::ip() +
             " ssid=" + (Net::ssid().isEmpty() ? String("(none)") : Net::ssid()) +
             " ap=" + Net::apSsid();
    if (sub == "set") {
      String ss, pw;
      splitRaw(args, ss, pw);       // SSIDs are case-sensitive - never lowercase
      if (ss.isEmpty()) return "ERR wifi set <ssid> [pass]";
      // An SSID containing spaces cannot be expressed here; use the portal.
      Net::setCreds(ss, pw);
      return String("OK wifi connecting to ") + ss;
    }
    if (sub == "clear")  { Net::clearCreds();  return "OK wifi cleared, portal up"; }
    if (sub == "portal") { Net::forcePortal(); return "OK portal up"; }
    return "ERR wifi status|set|clear|portal";
  }



  if (verb == "reboot") {
    ESP.restart();
    return "OK";
  }

  return String("ERR unknown '") + verb + "' - try help";
}

}  // namespace Cmd
