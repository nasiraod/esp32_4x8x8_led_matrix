#pragma once
#include <Arduino.h>

namespace Cmd {

// Parses one command line and returns a single-line reply. Shared verbatim by
// the TCP server and USB serial, so both control surfaces behave identically.
String handle(const String &line);

}  // namespace Cmd
