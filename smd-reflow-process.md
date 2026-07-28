# SMD Reflow Process — Electric Skillet

## Equipment and Materials

- Electric skillet (dedicated, not for food)
- IR thermometer with adjustable emissivity (Surpeer GM550)
- Laser-cut stencil (0.12mm for mixed 0603 / LQFP100 0.5mm pitch)
- Solder paste: Chip Quik TS391AX50 Sn63Pb37 no-clean
- Aluminum foil
- Black electrical tape (IR reference point)
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
3. For IR temperature reference: use a scrap PCB as your target surface. Mark a small area with black permanent marker and read that spot (emissivity ε=0.95 on the Surpeer GM550). The bare PCB surface is close enough without marker, but the marker area gives a stable reference.

**Note:** Bare aluminum foil reads falsely low (reflective surface); black tape or marker on a PCB gives accurate readings at ε=0.95.

---

## Skillet Calibration (First Use)

Map your dial to actual temperature before reflowing a real board:

1. Place a scrap PCB with a black-marker reference spot in the skillet
2. Set IR thermometer emissivity to ε=0.95
3. Turn skillet to a low setting, wait for temperature to stabilize (~5 minutes)
4. Measure the marker spot on the PCB
5. Repeat at several dial positions
6. Mark the dial at key temperatures: ~150°C (preheat/soak) — the reflow zone (200–220°C) is reached at maximum without foil

**Observed behavior (this skillet):** At maximum without foil, the surface cycles 200–220°C — heater cuts out at ~220°C and re-engages at ~200°C. This is correct for Sn63Pb37 reflow (liquidus 183°C, peak target 205–215°C).

Skillet dials are inaccurate — this map is essential.

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
1. Place scrap PCB reference target in skillet (for IR temperature monitoring)
2. Place populated board on **cold** skillet — cold start gives a more controlled ramp than pre-heating
3. Turn dial to ~150°C; wait approximately **2 minutes** — this is the preheat/soak phase (flux activates, solvents evaporate, board temperature equalizes)
4. Turn dial to **maximum**
5. Watch the paste: it will go dull/gray → slightly transparent (flux working) → **bright shiny silver** (reflow). The visual transition is the most reliable indicator. **Tip:** apply a small dab of paste to a visible test point or exposed pad on the board — this gives a clear unobstructed view of the reflow transition since paste under IC pins and small components can't be seen directly.
6. Once reflow is visible across the board, hold for **30–60 seconds** to ensure all joints reach liquidus — the skillet will cycle ~200–220°C during this phase, which is correct
7. **Kill the heat** — turn dial to off
8. Leave board on skillet until temperature drops below **100°C** before moving — do not touch or disturb while solder is solidifying
9. Once below 100°C, remove board and allow to finish cooling on the silicone mat

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
