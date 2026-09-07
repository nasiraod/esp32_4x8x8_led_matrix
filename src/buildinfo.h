#pragma once

// Identity of the running firmware: build timestamp plus the git revision it
// came from. Reported at boot, by the `build` command, and in the JSON API.
//
// Every update now goes over the air, so "did that upload actually land?" is a
// routine question - this makes it answerable rather than a guess.
const char *buildId();
