// STM32F723 USB Interposer board base plate
// This is a Q&D hack to solve two problems,
// since there is a connector on the bottom
// the board tends to rock when probeing, the
// display just tends to flop around and not
// be visable. This fixes both, it is not
// well fitted or elegant.
//
// Board: 40x80mm, 4x M2.2 mounting holes
// Standoffs: 4mm dia, 4mm tall; pins: 2mm dia,
// 2mm tall (for 2mm screws / press fit)

board_w      = 40;
board_h      = 80;
margin       = 2;       // extra plate beyond board edge on each side
plate_thick  = 4;
standoff_d   = 4;
standoff_h   = 4;
pin_d        = 2;       // fits 2.2mm hole with 0.1mm clearance each side
pin_h        = 2;

display_w    = 30;
display_h    = 43.5;
display_t    = 5;
extension_w  = 36;
extension_l  = 20;

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

module keeper()
{
	difference() {
		cube([extension_w + 8, display_t + 8 + 4, 5], center=true);
		translate([extension_l - 4.5, 0, -2])
			cube([4.5, display_t + 8.5, 2.5], center = true);
		translate([-extension_l + 4.5, 0, -2])
			cube([4.5, display_t + 8.5, 2.5], center = true);
		translate([0, display_t + 1.5, -2])
			cube([display_w - 3.25, 8, 2.5], center = true);
	}
	translate([0, -display_t - 2.5, -4])
		cube([extension_w, 2, 7], center=true);
	translate([extension_l - 1.5, 0, -4])
		cube([1.5, 4, 6], center = true);
	translate([-extension_l + 1.5, 0, -4])
		cube([1.5, 4, 6], center = true);
}

module base_plate()
{
	// Base plate
	cube([plate_w, plate_l, plate_thick]);

	// Standoffs at each mounting hole
	for (h = holes)
    	standoff(h[0], h[1]);
}

module display()
{
	// Display extension
	translate([plate_w - 0.1, plate_l - extension_l, 0]) {
		cube([extension_w + 0.1, extension_l, plate_thick]);
		translate([0, 0, plate_thick]) difference() {
			union() {
				translate([0, extension_l - display_t - 8, 0])
						cube([5, display_t + 8, display_h + 3]);
				translate([extension_w - 5,
						extension_l - display_t - 8, 0])
					cube([5, display_t + 8, display_h + 3]);
			}
			translate([2.5, extension_l - 9, 0])
				cube([display_w+1, display_t + 0.5, display_h+5]);
		}
	}
	%translate([extension_w/2 + plate_w ,
			extension_l/2 + board_h - (display_t + 8)/2 - 5.7,
			display_h + 7]) keeper();
}

base_plate();
display();
translate([board_w * 2, display_t*2, 2.5]) rotate([180, 0, 0]) keeper();

