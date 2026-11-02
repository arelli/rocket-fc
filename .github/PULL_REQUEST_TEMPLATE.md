## What and why


## Checklist

- [ ] `pio test -e native` passes
- [ ] `pio run -e flight` and `pio run -e ground` build without new warnings
- [ ] New decision logic is in `lib/fc_core` with a unit test
- [ ] Docs updated (settings, telemetry frames, wiring, UI) where behaviour changed
- [ ] Wiring change: `include/board_pins.h`, `docs/HARDWARE.md` and `tools/schematics.py` (+ regenerated SVGs) together
- [ ] Telemetry change: `tlm::kVersion` incremented, `docs/TELEMETRY.md`, `tools/ground_station.html` and `tools/ground_station.py` updated

## Flight safety

Does this change when or whether a charge can fire (pyro channel, state
machine, flight-safety `S` settings, remote fire / eject, launch channel)?

- [ ] No
- [ ] Yes: describe the change and how it was tested (unit tests, `sim live` on the bench, bare nichrome)
