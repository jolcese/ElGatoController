# Changelog

All notable changes to the ElGato Controller firmware are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The firmware version lives in [`include/version.h`](include/version.h) and is
printed over serial at startup, together with the git commit it was built from.

## [Unreleased]

## [0.6.0] - 2026-10-01

### Changed
- Button presses now update the on-screen state (brightness/temperature/on-off)
  immediately for instant visual feedback, instead of only after the network
  request. The light PUT follows a moment later, batched. Makes rapid repeated
  clicks feel responsive. `actionLight()` now just sends the current state.
- While no lights are known, discovery retries every 30s instead of waiting the
  full 5-minute poll interval, so a transient discovery miss at startup recovers
  quickly.

## [0.5.0] - 2026-10-01

### Changed
- Much faster button-to-light response: the action batching window dropped from
  600 ms to 150 ms, the blocking GET that ran before every action was removed
  (deltas apply to locally tracked state instead), and Nagle buffering is
  disabled on the PUT so the request goes out immediately.

## [0.4.0] - 2026-10-01

### Added
- Periodic mDNS re-discovery (every 30 min) plus automatic re-discovery when a
  known light stops answering, so a light that changed IP (DHCP) self-heals
  without needing to restart the controller or the light.
- Lights connectivity indicator on the display (top-left): a filled dot when the
  lights are reachable, a hollow circle with a slash when they are not.

### Fixed
- Color temperature bar moved opposite to the Kelvin number; the bar now tracks
  the displayed K value (low K = short bar).
- Disabled WiFi modem power save, which was dropping multicast packets and making
  mDNS discovery intermittently find only some (or none) of the lights.

## [0.3.0] - 2026-10-01

### Added
- OLED anti burn-in power management: the display dims after 15s idle and fully
  blanks after 60s idle, waking instantly on any button press. The GDDRAM buffer
  keeps refreshing while blanked, so content is current the moment it wakes.

### Changed
- mDNS discovery now runs multiple query passes and merges unique light IPs,
  instead of trusting a single query. Fixes only some lights being found when
  one doesn't answer the first query.

### Fixed
- Color temperature up/down buttons were inverted; swapped SW4/SW6 so each
  button changes temperature in the expected direction.

## [0.2.0] - 2026-10-01

### Added
- Automatic discovery of Elgato Key Lights over mDNS (`_elg._tcp`), replacing
  the two hardcoded IP addresses. Up to `MAX_LIGHTS` lights are now controlled.
- Firmware version tracking: `FW_VERSION` plus the git commit hash are compiled
  into the firmware and printed on the serial console at boot.

### Changed
- `getLightState()` reads from the first discovered light; `actionLight()` pushes
  updates to every discovered light.
- mDNS discovery retries a few times, since the first query after connecting
  often misses responses.

## [0.1.0] - initial

### Added
- WiFi provisioning via WiFiManager, OLED display, physical buttons for on/off,
  brightness and color temperature, controlling two Elgato lights at hardcoded
  IP addresses.

[Unreleased]: https://github.com/
[0.2.0]: https://github.com/
[0.1.0]: https://github.com/
