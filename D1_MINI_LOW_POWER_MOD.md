# Wemos D1 Mini Low-Power Modification — CH340 Back-Powering

Notes from hardware-modifying a **Wemos D1 Mini clone** for battery operation.
Removing the onboard USB-to-serial chip's power to cut deep-sleep current caused
the board to stop booting on battery alone, due to ESD-diode back-powering
through the ESP's UART TX pin. Final working solution documented at the end.

## The goal

Reduce deep-sleep current on a D1 Mini from ~9.5 mA to well under 1 mA by
removing the three main onboard current consumers:

1. Red power LED (~1–2 mA)
2. CH340 USB-to-serial chip (~3–5 mA on CH340G clones)
3. AMS1117 LDO quiescent current / back-feed (~1–3 mA)

Standard procedure (from Andreas Spiess video #324) is to cut the 3V3 traces
feeding the AMS1117 output and the CH340's VCC pin, then feed the board directly
via the 3V3 header pin from an external low-quiescent LDO (HT7330) off the
18650 battery.

## Resulting topology on this clone

After the Spiess-style cuts, the board splits into two electrically-isolated
domains:

```
  ┌──────────────────────────┐     ┌────────────────────────────┐
  │   "USB side"             │     │   "Battery side"           │
  │                          │     │                            │
  │   USB 5V                 │     │   18650 battery            │
  │       │                  │     │       │                    │
  │       ▼                  │     │       ▼                    │
  │   AMS1117 (3.3V)         │     │   HT7330 (3.3V)            │
  │       │                  │     │       │                    │
  │       ├── CH340 VCC      │     │       ▼                    │
  │       └── (nothing else) │     │   ESP8266 + pull-ups +     │
  │                          │     │   AHT20 + BMP280 + etc.    │
  └──────────────────────────┘     └────────────────────────────┘
              │                                   │
              │        shared ground              │
              └───────────────────────────────────┘
                    
  Cross-domain signal wires (through series resistors):
    ESP GPIO1 (TX) ──[470 Ω]── CH340 RX    ← back-powering problem
    CH340 TX       ──[470 Ω]── ESP GPIO3 (RX)
    CH340 DTR      ── (auto-reset transistor) ── ESP EN
    CH340 RTS      ── (auto-reset transistor) ── ESP GPIO0
```

The CH340 now auto-powers itself based on USB: plug USB in, AMS1117 outputs
3.3 V, CH340 runs; unplug USB, AMS1117 loses input, CH340 VCC drops to 0 V.
This is the intended behavior — programming via the onboard CH340 works
whenever USB is connected, and the CH340 consumes zero current otherwise.

## The symptoms

On the clone PCB used here, the published procedure broke the board in several
stages:

1. After the cuts, **board drew 25 mA stuck and wouldn't boot on battery**.
2. Measured boot-strap pins showed **GPIO0 (D3) and GPIO2 (D4) floating at
   ~0.8 V** and **GPIO15 (D8) at ~0.7 V**. A YouTube comment on the reference
   video confirmed this clone layout puts those pull-ups on the CH340 side of
   the cut — they were orphaned by the trace cut.
3. Added external pull-ups: 10 kΩ D3 → 3V3, 10 kΩ D4 → 3V3, 10 kΩ D8 → GND.
   **Boot pins now correct, but board still stuck at 25 mA on battery.**
4. Plugging in USB (alongside the battery) made the board work normally.
   Unplugging USB immediately returned it to the 25 mA stuck state.
   Pre-charged bulk capacitance didn't change this — the failure was
   instantaneous on USB removal, ruling out inrush/ramp theories.
5. Removing the auto-reset circuit's DTR/RTS transistors (suspecting stray EN
   assertion) **did not fix it**.
6. Measured **both ESP TX (GPIO1) and RX (GPIO3) at 0.7 V** — the same
   voltage as multiple other "floating" pins, suggesting a shared ~0.7 V rail.
   That rail was the dead CH340's VCC pad, being pumped up from multiple
   sources through its own ESD diodes.
7. Removing all `Serial.*` calls from firmware (via a `DEBUG` compile flag) —
   also did not fix it, because **the ESP8266 ROM bootloader prints a banner
   at 74880 baud on every wake, before any firmware runs**. This
   hardware-level UART activity can't be disabled in software.

## Root cause — ESD diode back-powering through CH340 RX

Every CMOS input pin has two parasitic ESD protection diodes, built into the
silicon and not optional:

```
                VCC pin
                  │
                  ▲   ◄── upper ESD diode
                  │       (anode at I/O pin,
    I/O pin ──────●        cathode at VCC)
                  │
                  ▲   ◄── lower ESD diode
                  │       (anode at GND,
                  │        cathode at I/O pin)
                GND pin
```

Their **intended job** is to clamp electrostatic discharge spikes to safe
voltages. In normal operation, signals stay between GND and VCC, both diodes
are reverse-biased, and they do nothing.

**The assumption that makes this safe is that VCC is always a real power rail
higher than any input signal.** Cut VCC (or disconnect the USB that feeds
VCC) while leaving input signals driven, and this assumption collapses.

In this project's USB-disconnected state:

- CH340 VCC: **0 V** (AMS1117 has no input, drops its output)
- CH340 RX pin: wired through a 470 Ω series resistor to **ESP GPIO1 (TX)**
- ESP GPIO1: **3.3 V** whenever firmware or the ROM bootloader transmits

On every wake, the ROM bootloader emits ~200 bytes of boot banner at
74880 baud (~27 ms of UART activity with ~50 % HIGH duty cycle). During
those HIGH levels:

```
ESP GPIO1 (3.3 V) → 470 Ω → CH340 RX → upper ESD diode → CH340 VCC (dead)
```

The upper ESD diode is forward-biased (3.3 V anode, 0 V cathode) and pumps
current into the dead VCC rail. The rail charges up to ~0.7 V — enough to
**partially power** the CH340. Symptoms of this half-powered state:

- The chip can **latch up** (positive-feedback internal short between VCC
  and GND inside the silicon — once latched, it's a low-resistance dead
  short that drains current continuously).
- Output pins drive to undefined in-between voltages.
- Multiple ESP pins that happen to connect to CH340 measure ~0.7 V (they're
  being clamped by the half-powered chip pulling them toward its back-fed
  VCC rail).

The board's 25 mA stuck current was the ESP's TX pin pumping charge into the
back-powered CH340, plus the latched CH340's own draw. The boot never
completed because the CH340 was actively interfering with the UART lines and
pulling other pins on this clone layout.

## Why CH340 can't be fixed in software

The CH340G/C/N family has **no enable pin, no shutdown pin, and no proper
USB suspend support**. The chip is always-on when VCC is present, drawing
~5 mA regardless of USB state. The only "low-power mode" is "no power." This
is the root reason the Spiess-style mod exists — every battery-powered
ESP8266 project ends up doing some version of it because CH340 was never
designed for battery applications.

Modern alternatives like **CP2102 / CP2104** (Silicon Labs) and **FT230X**
(FTDI) implement proper USB suspend and drop to <100 µA automatically when
the host disconnects. Swapping CH340 for CP2104 would eliminate this entire
problem class at the cost of ~$1.50 extra and a different footprint.

## Solution — jumper (or 2-pin header) in place of the TX resistor

The simplest, cleanest fix that respects the existing topology:

1. **Remove the 470 Ω resistor on the TX line** (between ESP GPIO1 and
   CH340 RX). It's an SMD 0402 or 0603 part next to the CH340.
2. **Solder a 2-pin 0.1″ header** (or a small slide switch) across the
   same pads the resistor occupied.
3. **Add a jumper cap.**
4. **Workflow:**
   - Battery-only operation → jumper cap **off** → TX line physically broken
     → no back-powering path → board boots and runs cleanly.
   - Programming via USB → jumper cap **on** → TX line connected → CH340
     receives ESP's responses → `arduino-cli upload` works normally.

Why this works for this topology specifically:

- **CH340 is already conditionally powered** by USB via the AMS1117, so
  when USB is connected and the jumper is in place, CH340 VCC is at 3.3 V,
  the RX ESD diode is reverse-biased, and UART works cleanly in both
  directions.
- **When USB is absent**, the jumper is removed, so no physical path exists
  from ESP TX to the CH340's RX pin. The ESD diode is irrelevant because
  it has no signal source pushing against it.
- **Zero quiescent current** added to the battery budget (a removed jumper
  is a perfect open circuit).
- **Zero added components** beyond the header and a jumper cap — less than
  half the parts of any "automatic" alternative.

## Other pins — only TX matters

An audit of every electrical connection crossing from the battery side to
the USB side:

| Path | Direction | Back-powering risk? |
|---|---|---|
| **ESP GPIO1 (TX) → CH340 RX** | outbound (ESP actively drives HIGH) | **Yes — the only problem pin.** Fixed by the TX jumper. |
| CH340 TX → ESP GPIO3 (RX) | inbound (CH340 drives, but dead when USB absent) | No. Both sides high-Z when CH340 unpowered. |
| CH340 DTR → auto-reset NPN → ESP EN | inbound (via transistor) | No. DTR floats low when CH340 unpowered → NPN base at 0 V → transistor OFF → EN sits at pull-up voltage. |
| CH340 RTS → auto-reset NPN → ESP GPIO0 | inbound (via transistor) | No. Same analysis as DTR. |
| USB D+ / D- | USB-side only, not wired to ESP | No. |
| Crystal (XI, XO) | CH340-internal, not wired to ESP | No. |
| Ground plane | shared | No. Ground is a reference, not a current source. |

**TX is the entire story.** The RX-side 470 Ω can stay in place; the
auto-reset transistors can stay in place; nothing else needs surgery.

## Verification steps after the fix

After soldering the TX jumper header, on battery power only (jumper cap
removed):

1. **Voltage at CH340 VCC pad**: should read ~0 V (USB absent, AMS1117
   dropped out). **Must not read ~0.7 V** — that value would mean back-
   powering is still happening from some other path.
2. **Boot test**: board should come out of reset, complete `setup()`, and
   enter deep sleep within a few seconds.
3. **Deep-sleep current target**: under 1 mA. Expect ~200–500 µA,
   dominated by the ESP deep-sleep baseline and the 100 kΩ battery
   divider (AHT20 + BMP280 idle together at ~1 µA, negligible).
4. **Flashing verification**: install the jumper cap, plug USB, confirm
   `arduino-cli upload` still works normally.

## Alternative approaches (for reference)

If the manual-jumper workflow isn't acceptable, these are the
automatic-but-more-complex options:

### External USB-TTL adapter

Remove both 470 Ω resistors. Accept that the onboard CH340 is decorative.
Flash via an external USB-TTL adapter (CP2102 or FTDI module, ~$2) wired to
the TX/RX header pins. Two small tactile buttons (FLASH on D3, RESET on
RST) make the manual bootloader-entry ritual ergonomic. Zero new parts on
the board itself, most reliable long-term.

### Replace CH340 with CP2104

Desolder the CH340, fit a CP2104 on the same UART/USB connections. CP2104
auto-detects USB suspend/disconnect and drops to ~80 µA on its own. No
trace cuts, no jumpers, no external logic. Board behaves like stock but
with 60× less sleep drain. Costs ~$1.50 plus rework effort. The "right
chip for the job" answer.

### LDO with active discharge replacing the AMS1117

Swap the AMS1117 for a 3.3 V LDO that has "active output discharge" as a
feature (TI TPS70933, Diodes AP2112K, etc.). When USB drops, the new LDO's
internal discharge MOSFET actively pulls CH340 VCC to GND, absorbing any
back-powering current from ESP TX. One IC swap, fully automatic, no
jumper needed. Footprint differs from AMS1117 (SOT-23-5 vs SOT-223) so
requires flying-wire mounting.

### Analog bus switch on the UART lines

Insert a single analog bus switch IC (FSA2257, NLAS4157, TS3A24159, ~$0.30)
between ESP UART and CH340 UART. Enable pin tied to USB VBUS. USB present →
switch closes → programming works. USB absent → switch opens → UART lines
physically isolated. <1 µA quiescent when disabled.

### Discrete P-MOSFET load switch + active discharge MOSFET

All-discrete version of the LDO-with-active-discharge approach. 1 P-MOSFET
(switch), 1 N-MOSFET (discharge), 1 PNP (USB-presence sensor),
3 resistors. ~300 µA quiescent when USB absent. More parts than any of
the above but uses jellybean components.

## Key lessons

- **Clone PCB layouts diverge from genuine boards.** Pull-up resistors for
  boot-strap pins (GPIO0, GPIO2, GPIO15) can end up on either side of a
  3V3 cut. Always verify with a multimeter before and after.
- **The ESP8266 ROM bootloader can't be silenced in firmware.** Any
  diagnostic hypothesis that relies on "no UART activity" via
  software-only changes (DEBUG flags, removing Serial calls) is
  insufficient — the ROM prints on every reset regardless.
- **Cutting VCC to a CMOS chip while leaving its signal pins connected
  to a powered source is always wrong.** The ESD diodes guarantee
  back-powering. Either disconnect the signal pins too, clamp the dead
  rail conditionally (not passively — a static clamp conflicts with
  legitimate power sources), or use a chip that handles its own
  power-down gracefully.
- **A diode in series on a UART line can't block back-powering without
  also breaking signal propagation.** UART needs both HIGH and LOW
  voltage levels to pass; a series diode only conducts in one direction.
- **Passive fixes fail when the system needs state.** "Is USB connected
  or not" is state; passive components don't have state. Any truly
  automatic solution requires at least one switching element (MOSFET,
  LDO enable, bus switch, or the like) — or a manual jumper that encodes
  the state by its presence or absence.
- **CH340 is a cost-optimized part, not a power-optimized part.** No
  enable pin, no USB suspend. If battery operation is a requirement at
  design time, pick CP2104 / FT230X instead and skip this entire class
  of problem.
