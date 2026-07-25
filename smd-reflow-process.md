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
2. Line skillet interior with a single layer of aluminum foil, pressed flat — no air pockets
3. Place a small piece of black electrical tape on the foil near your board position — this is your IR temperature reference (foil is too shiny for accurate IR readings)

---

## Skillet Calibration (First Use)

Map your dial to actual temperature before reflowing a real board:

1. Turn skillet to a low setting, wait for temperature to stabilize (~5 minutes)
2. Measure the black tape with the IR thermometer
3. Repeat at several dial positions
4. Mark the dial at key temperatures: ~150°C (preheat) and ~210°C (reflow)

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
1. Preheat skillet to ~150°C **before** placing the board — cold start causes uneven heating
2. Place board on foil, monitor temperature on the black tape reference point
3. Slowly nudge dial toward reflow temperature — avoid rapid ramp
4. Watch the paste: it will go dull/gray → slightly transparent (flux activating) → **bright shiny silver** (reflow)
5. As soon as you see reflow across the board, **kill the heat**
6. Leave board on skillet — do not move it until solder has solidified (goes from shiny to dull)
7. Once solidified, remove board and allow to finish cooling on the silicone mat

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
