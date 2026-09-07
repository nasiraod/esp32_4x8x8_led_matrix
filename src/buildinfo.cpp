#include "buildinfo.h"
#include "build_id.h"     // generated each build by scripts/extra_targets.py

// Isolated in its own translation unit so only this file recompiles when the
// generated header changes.
const char *buildId() { return BUILD_ID; }
