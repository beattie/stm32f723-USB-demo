# SMD Reflow Process — Electric Skillet

## Equipment and Materials

- Electric skillet (dedicated, not for food)
- IR thermometer with adjustable emissivity (Surpeer GM550)
- Laser-cut stencil (0.12mm for mixed 0603 / LQFP100 0.5mm pitch)
- Solder paste: Chip Quik TS391AX50 Sn63Pb37 no-clean
- Scrap PCB (temperature reference target — see Skillet Setup)
- Squeegee or old credit card
- Tweezers (fine tip) or vacuum pen for component placement
- Isopropyl alcohol 90%+ and toothbrush for cleaning
- Silicone soldering mat (table protection)
- Magnification (loupe or microscope) for inspection
- Solder wick and flux for rework

---

## Paste Storage

- Store paste refrigerated when not in use
- Before use: remove from fridge, leave **closed** at room temperature for 30–60 minutes
- Opening a cold jar causes condensation which contaminates the paste
- Once opened, paste has a working life of several hours; do not leave uncovered
- Return unused paste to the fridge

NB: Chip Quick TS391AX50 is listed as Thermally Stable, may not need redrigeration.

---

## Skillet Setup

1. Place silicone mat on work surface, skillet on top
2. **Do not use aluminum foil** — foil acts as a heat sink and limits peak temperature to ~190°C, which is below the reflow zone for Sn63Pb37. Without foil, the skillet surface cycles correctly between 200–220°C.
3. Place a scrap PCB in the skillet as your IR temperature reference target. Set IR thermometer emissivity to ε=0.95 and read the bare PCB surface — permanent marker on PCB reads essentially the same as bare PCB, either works. Keep the reference PCB close to where your board will sit.

**What does not work for IR reference:**
- Bare skillet surface: reads 30–50°C cold (reflective metal)
- Black electrical tape on skillet: also reads cold, and puckers/deforms above 150°C (PVC melts)
- Black tape or marker must be on a PCB substrate to give accurate readings

---

## Skillet Calibration (First Use)

Map your dial to actual temperature before reflowing a real board:

1. Place scrap PCB reference in skillet; set IR thermometer to ε=0.95
2. Turn skillet to maximum (no foil)
3. Measure PCB temperature at intervals and note the time

**Observed ramp (this skillet, cold start ~28–33°C, maximum dial, no foil):**

| Time | PCB Temperature | Event |
|------|----------------|-------|
| 0:00 | 28–33°C | Start |
| 4:00 | ~150°C | |
| 6:12 | 183°C | Liquidus (up) |
| 6:21 | ~191°C | Paste goes shiny |
| 7:00 | ~200°C | |
| 8:30 | ~220°C | |

Ramp rate is ~0.4°C/s — slow enough that the paste preheat/soak profile is satisfied naturally on the way up. No need to hold at an intermediate temperature. **Liquidus onset is consistent at ~6:21 across multiple runs.**

**Time above liquidus (TAL) by kill-heat strategy:**

| Strategy | Kill heat | TAL | Peak | Suitable for components? |
|----------|-----------|-----|------|--------------------------|
| 220°C dial mark / neon out | ~8:01 | ~5 min | 222°C | No — 6× over spec |
| Max dial / neon out | ~10:17 | ~9 min | 249°C | No — way over spec |
| **Liquidus + 30–60s** | **~6:50–7:20** | **~90–108s** | **~200–205°C** | **Yes ✓** |

**Use the liquidus + 30–60s strategy for all populated boards.** The neon-out strategies give excessive TAL and excessive peak temperatures regardless of dial setting — the neon is useful only as a calibration reference, not as the reflow trigger.

**Cooldown:** Move board to silicone mat once reference PCB reads 150°C (solder solidified well above this — tap a large pad gently to confirm if unsure).

Skillet dials are inaccurate — calibrate before first use and mark the dial.

---

## Stencil and Paste Application

### Alignment
1. Secure the bare PCB flat on your work surface — tape the corners or use a frame to prevent movement
2. Lay the stencil over the board, aligning apertures to pads
3. Hold or tape the stencil edges so it cannot shift during squeegee pass
4. The stencil must lie flat with no gap between stencil and board surface — paste will bleed under a lifted stencil

### Paste Application
1. Apply a bead of paste along one edge of the stencil (slightly more than you think you need)
2. Hold squeegee or credit card at **45°** angle
3. Draw across the stencil in **one firm, smooth pass** — don't go back and forth
4. Consistent pressure and angle give consistent paste volume
5. Carefully lift the stencil **straight up** — peeling at an angle smears the deposits

### Inspection Before Placing Parts
- Check every pad under magnification before placing components
- Look for: missed pads, bridges between adjacent pads, uneven deposit volume
- Fine-pitch pads (LQFP100) are most likely to bridge — inspect carefully
- Fix problems now with a toothpick; much harder after components are placed

---

## Component Placement

- Work from smallest/lowest components to largest/tallest
- Tweezers: fine tip, grip component by the body not the leads
- Vacuum pen: faster for larger components, less control on small ones
- Place components into the paste — they should sit level and centered on the pads
- Paste is tacky enough to hold parts in place; don't press hard or you'll displace paste
- Double-check orientation on polarized components (caps, diodes, ICs) before reflow — much easier to fix now

---

## Reflow

### Temperature Profile (Sn63Pb37)
| Phase | Temperature | Notes |
|-------|------------|-------|
| Preheat | ramp to ~150°C | Activates flux, evaporates solvents |
| Soak | hold ~150°C briefly | Equalizes board temperature |
| Reflow | push to 205–215°C | Watch for paste transition |
| Cool | kill heat, leave on skillet | Let solder solidify before moving |

### Process
1. Place scrap PCB reference target in skillet next to where the board will sit
2. Place populated board on **cold** skillet
3. Turn dial to **maximum**
4. Wait — expect approximately **8–9 minutes** before reflow at this ramp rate. The slow ramp naturally covers the preheat and soak phases on the way up; no intermediate dial step needed.
5. Watch the paste: it will go dull/gray → slightly transparent (flux working) → **bright shiny silver** (reflow). The visual transition is the primary trigger — timing is only a rough guide. **Tip:** apply a small dab of paste to a visible test point or exposed pad — gives an unobstructed view of the reflow transition since paste under IC pins can't be seen directly.
6. Once reflow is visible, start a timer and hold for **30–60 seconds**
7. **Kill the heat** — turn dial to off. Do not wait for the neon pilot light to go out; that gives ~5–9 minutes TAL depending on dial setting, which is too long for components.
8. Leave board on skillet. Move to silicone mat once reference PCB reads **150°C** — solder is solid well before this point. Tap a large pad gently to confirm solidity if uncertain.

---

## Post-Reflow Inspection

Under magnification, check for:

- **Good joint**: shiny, smooth fillet, wets both pad and component lead
- **Bridge**: solder connecting two adjacent pads — most common on fine-pitch
- **Tombstone**: component standing on one end, one pad reflowed before the other — caused by uneven paste or component not centered
- **Cold joint**: dull, grainy surface — insufficient temperature or movement during solidification
- **Insufficient solder**: pad wetted but joint looks thin — check paste volume on stencil setup

---

## Rework

### Bridges
1. Apply liquid flux to the bridge
2. Touch solder wick to the bridge with a hot iron — wick pulls excess solder away
3. Clean flux residue with IPA

### Tombstoned Component
1. Apply flux
2. Reflow both pads with hot air or iron, press component flat while liquid
3. Alternatively: remove component, clean pads, re-paste, replace

### Cold Joint
1. Apply flux
2. Reflow with iron or hot air — good flux and heat usually fixes it

### Hand Application (Single Pads / Rework Only)
- Use a toothpick or syringe to apply a small amount of paste to individual pads
- Or use flux + solder wire with an iron for single joints
- Not recommended for whole-board paste application

---

## Cleaning

Chip Quik TS391AX50 is no-clean — residue is non-corrosive and can be left on the board. Cleaning is still recommended:

1. Apply IPA (90%+) to board
2. Scrub with toothbrush, working around components
3. Rinse with fresh IPA
4. Allow to dry completely before powering up

---

## Ventilation

Flux fumes during reflow are unpleasant and mildly irritating. Work near an open window or with a fan exhausting away from you. A small fume extractor is worthwhile if you do this regularly.

---

## Notes

- **Do not mix bismuth and tin-lead solder** — the alloys blend and the melting point drops unpredictably
- Bismuth paste (lower temp ~138°C) is useful for learning skillet technique but produces more brittle joints
- Tin-lead (Sn63/Pb37) is preferred for production boards — sharper melt at 183°C, more forgiving
- Stencil thickness: **0.12mm** for mixed 0603 / LQFP100 0.5mm pitch
