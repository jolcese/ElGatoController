#pragma once

// Human-readable firmware version. Bump this on each release and add a
// matching entry to CHANGELOG.md.
#define FW_VERSION "0.6.0"

// Short git commit hash, injected at build time by scripts/version.py.
// Falls back to "unknown" when building outside a git checkout.
#ifndef GIT_REV
#define GIT_REV "unknown"
#endif
