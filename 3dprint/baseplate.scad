// STM32F723 USB Interposer board base plate
// Board: 40x80mm, 4x M2.2 mounting holes
// Standoffs: 4mm dia, 4mm tall; pins: 2mm dia, 2mm tall (for 2mm screws / press fit)

board_w      = 40;
board_h      = 80;
margin       = 2;       // extra plate beyond board edge on each side
plate_thick  = 4;
standoff_d   = 4;
standoff_h   = 4;
pin_d        = 2;       // fits 2.2mm hole with 0.1mm clearance each side
pin_h        = 2;

plate_w = board_w + 2*margin;
plate_l = board_h + 2*margin;

// Hole positions relative to board top-left corner (mm), from KiCad PCB
holes = [
    [ 3.00,  3.00 ],   // top-left
    [37.00,  3.00 ],   // top-right
    [ 2.85, 76.90 ],   // bottom-left
    [36.85, 76.90 ],   // bottom-right
];

module standoff(x, y) {
    translate([margin + x, margin + y, plate_thick]) {
        cylinder(h=standoff_h, d=standoff_d, $fn=36);
        translate([0, 0, standoff_h])
            cylinder(h=pin_h, d=pin_d, $fn=36);
    }
}

// Base plate
cube([plate_w, plate_l, plate_thick]);

// Standoffs at each mounting hole
for (h = holes)
    standoff(h[0], h[1]);
