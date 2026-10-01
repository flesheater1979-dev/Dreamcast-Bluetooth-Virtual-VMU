# Wiring and Safety

## Dreamcast controller port

The Dreamcast controller port exposes five signals:

1. D0 — Maple serial data
2. +5 V
3. GND
4. Sense
5. D1 — Maple serial data

Reference: https://consolemods.org/wiki/Dreamcast:Connector_Pinouts

This firmware uses one Maple port on the NiceMCU:

- GPIO21
- GPIO22

### Tested cable mapping

Development cable:

- RED → GPIO21
- WHITE → GPIO22
- GREEN → GND
- COPPER/shield → GND
- BLUE (+5 V) → **not connected for Beta 0.9**

### Critical warning

Wire colours are cable-specific. Verify an unknown cable electrically against the Dreamcast connector before applying power.

Do not:

- connect Dreamcast +5 V to a 3.3 V pin
- connect the BLUE/+5 V wire during Beta 0.9 testing
- power the board simultaneously from an unprotected Dreamcast +5 V feed and USB
- change GPIO21/GPIO22 in this firmware unless you understand the Maple timing path

## Planned controller-port power

A future revision is being tested with:

Dreamcast +5 V → resettable PTC → 1N5819 Schottky → NiceMCU 5 V/VBUS

That circuit is **not yet part of Beta 0.9**.
