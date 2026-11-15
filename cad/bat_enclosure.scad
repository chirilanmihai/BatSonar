// ======================================================================
//  The Bat That Sees With Sound - enclosure
//  Arduino Nano v3 (mini USB) + HC-SR04 + 2 x 5 WS2812B (60 LED/m) + 12 mm buzzer
//
//  Coordinates (all parts modelled in PRINT orientation of the body):
//    X = left/right, Y = up (height), Z = depth from the front face (Z=0 = front, on the bed)
//
//  Export one part:   openscad -D 'part="body"' -o body.stl bat_enclosure.scad
//  part = "assembly" | "body" | "lid" | "clamp" | "wing_shell_R" | "wing_shell_L"
//         | "wing_back_R" | "wing_back_L"
// ======================================================================
part = "assembly";
$fn = 64;

// ---------------- body ----------------
W    = 70;     // outer width
H    = 80;     // outer height (without ears)
D    = 50;     // body depth (open back, closed by the lid)
R    = 8;      // corner radius seen from the front
wall = 2;
lid_t = 2;

// ---------------- HC-SR04 ----------------
eye_x   = 13;     // transducer centre = +/-13 (26 mm spacing)
eye_y   = 56;
eye_d   = 17.5;   // transducers are 16 mm; 0.75 mm play each side
sr_w    = 45;     // PCB 45 x 20
sr_h    = 20;
sr_front_z = 8;   // PCB front face depth -> transducers stick out ~4 mm
pcb_t   = 1.6;
clamp_boss_x = 28.5;

// ---------------- buzzer (12 mm, active or passive) ----------------
bz_y = 20;
bz_d = 12.6;

// ---------------- Arduino Nano (lies flat, USB to the back) ----------------
nano_x  = 19;       // board centre X
nano_w  = 18;       // 18.0 .. 18.5 boards fit
nano_y  = 11;       // underside of the PCB (9 mm space below for header pins / solder)
nano_front = 2.5;   // front end of board (stop in the front slot)
nano_back_stop = 46.8;   // back stop on the lid fins (fits 43.2 .. 43.9 mm boards)
usb_y   = nano_y + pcb_t + 2;   // mini USB receptacle centre height
usb_hole = [13, 10];            // opening in the lid for the plug's overmould

// ---------------- lid screws (M3 x 10, self-tapping into PLA) ----------------
screw_pos = [[-29.5, 74.5], [29.5, 74.5], [-29.5, 5.5]];

// ---------------- wings ----------------
wing_z0  = 6;      // wing front sits 6 mm behind the face
wing_d   = 16;     // wing depth (front skin -> back plate)
skin     = 1.0;    // front diffuser thickness (white PLA)
wwall    = 1.6;
tab_y    = [29, 41];   // tab height range (into side of body)
tab_len  = 6;
clr      = 0.2;    // fit clearance

R_TOP = [0, 72];  TIP = [112, 98];
F1 = [104, 50];   F2 = [72, 30];   F3 = [38, 22];   R_BOT = [0, 26];
strip_o  = [0, 61];               // strip centre line origin
strip_a  = atan((TIP[1]-R_TOP[1]) / (TIP[0]-R_TOP[0]));  // parallel to the arm
strip_w  = 12.5;                  // channel width (10 mm strip, or 12 mm silicone)
WRIST = [60, 82];

// ======================================================================
module outline2d() translate([-W/2 + R, R]) offset(r = R) square([W - 2*R, H - 2*R]);

module ear2d(s) {
  mirror([s < 0 ? 1 : 0, 0]) polygon([[13, 78.5], [33, 72], [28, 102]]);
}
module ear_inner2d(s) {
  mirror([s < 0 ? 1 : 0, 0]) polygon([[18, 81], [29, 78], [27.5, 95]]);
}

module body() {
  difference() {
    union() {
      difference() {
        union() {
          linear_extrude(D) outline2d();
          for (s = [-1, 1]) linear_extrude(6) ear2d(s);
        }
        translate([0, 0, wall]) linear_extrude(D) offset(delta = -wall) outline2d();
      }
      // --- lid screw bosses (full depth, so they print without overhang)
      for (p = screw_pos) translate([p[0], p[1], 1]) cylinder(d = 7, h = D - 1);
      // --- HC-SR04 front stops (PCB corners)
      for (sx = [-1, 1]) for (yy = [[44.5, 49], [63, 67.5]])
        translate([sx > 0 ? 19.5 : -24, yy[0], 1]) cube([4.5, yy[1] - yy[0], sr_front_z - 1]);
      // --- HC-SR04 clamp bosses
      for (sx = [-1, 1]) translate([sx * clamp_boss_x, eye_y, 1])
        cylinder(d = 6.5, h = sr_front_z + pcb_t - 1);
      // --- buzzer pocket
      translate([0, bz_y, 1]) cylinder(d = bz_d + 3, h = 7);
      // --- Nano front slot pillars
      for (xr = [[nano_x - nano_w/2 - 1.6, nano_x - nano_w/2 + 3],
                 [nano_x + nano_w/2 - 3, nano_x + nano_w/2 + 1.6]])
        translate([xr[0], 1, 1]) cube([xr[1] - xr[0], nano_y + pcb_t + 2.4 - 1, nano_front + 2.5 - 1]);
    }
    // eyes: through holes + shallow "eye socket" on the face
    for (sx = [-1, 1]) translate([sx * eye_x, eye_y, -1]) {
      cylinder(d = eye_d, h = 20);
      cylinder(d = 23, h = 1.8);
    }
    // ear detail (recessed 0.8 mm)
    for (s = [-1, 1]) translate([0, 0, -1]) linear_extrude(1.8) ear_inner2d(s);
    // buzzer pocket bore
    translate([0, bz_y, wall]) cylinder(d = bz_d, h = 20);
    // mouth: smile of sound holes + two fangs
    for (a = [200 : 140/6 : 340]) translate([5 * cos(a), bz_y + 3 + 5 * sin(a), -1]) cylinder(d = 1.8, h = 5, $fn = 16);
    for (s = [-1, 1]) translate([0, 0, -1]) linear_extrude(5)
      polygon([[s * 1.5, 17.4], [s * 3.3, 17.4], [s * 2.4, 15]]);
    // clamp boss pilot holes (M3 self-tapping)
    for (sx = [-1, 1]) translate([sx * clamp_boss_x, eye_y, 3]) cylinder(d = 2.5, h = 20);
    // lid screw pilot holes
    for (p = screw_pos) translate([p[0], p[1], D - 12]) cylinder(d = 2.6, h = 13);
    // wing tab holes in the side walls
    for (sx = [-1, 1]) translate([sx * (W/2 - wall/2) - 2, tab_y[0] - clr, wing_z0 - clr])
      cube([4, tab_y[1] - tab_y[0] + 2*clr, wing_d + 2*clr]);
    // Nano front slots (board front end slides in from the back)
    translate([nano_x - nano_w/2 - 0.4, nano_y, nano_front])
      cube([nano_w + 0.8, pcb_t + 0.3, 10]);
  }
}

// HC-SR04 clamp bar: presses the PCB ends against the front stops, clears the back components
module clamp() {
  difference() {
    union() {
      for (sx = [-1, 1]) translate([sx > 0 ? 18 : -31.75, eye_y - 5, sr_front_z + pcb_t])
        cube([13.75, 10, 6]);
      translate([-20, eye_y - 5, sr_front_z + pcb_t + 3]) cube([40, 10, 3]);
    }
    for (sx = [-1, 1]) translate([sx * clamp_boss_x, eye_y, 0]) cylinder(d = 3.3, h = 30);
  }
}

module lid() {
  difference() {
    union() {
      translate([0, 0, D]) linear_extrude(lid_t) offset(delta = -0.1) outline2d();
      // Nano back fins with notches: hold the USB end of the board
      for (xr = [[nano_x - nano_w/2 - 1.6, nano_x - nano_w/2 + 2.5],
                 [nano_x + nano_w/2 - 2.5, nano_x + nano_w/2 + 1.6]])
        translate([xr[0], 4, nano_back_stop - 2.5]) cube([xr[1] - xr[0], 11, D - nano_back_stop + 2.5 + 0.01]);
    }
    for (p = screw_pos) translate([p[0], p[1], D - 1]) {
      cylinder(d = 3.4, h = 5);
      translate([0, 0, lid_t + 1 - 1.7]) cylinder(d1 = 3.4, d2 = 6.6, h = 1.71);  // countersink
    }
    // USB plug opening
    translate([nano_x, usb_y, D - 1]) linear_extrude(5)
      offset(r = 2) square([usb_hole[0] - 4, usb_hole[1] - 4], center = true);
    // board notches
    translate([nano_x - nano_w/2 - 0.4, nano_y, nano_back_stop - 3]) cube([nano_w + 0.8, pcb_t + 0.3, 3]);
  }
}

// ---------------- wing ----------------
function bez(A, C, B, n = 14) = [for (i = [0 : n - 1]) let(t = i / n) (1-t)*(1-t)*A + 2*t*(1-t)*C + t*t*B];
function ctrl(A, B, s) = let(d = B - A, L = norm(d)) (A + B) / 2 + [d[1], -d[0]] / L * 2 * s;
wing_pts = concat([R_TOP],
                  bez(TIP, ctrl(TIP, F1, 8), F1),
                  bez(F1,  ctrl(F1, F2, 7),  F2),
                  bez(F2,  ctrl(F2, F3, 5),  F3),
                  bez(F3,  ctrl(F3, R_BOT, 4), R_BOT),
                  [R_BOT]);
module wing2d() polygon(wing_pts);

module rib(a, b, w = 1.2) hull() { translate(a) circle(d = w, $fn = 12); translate(b) circle(d = w, $fn = 12); }

module wing_shell() {   // RIGHT wing, front skin on the bed
  difference() {
    union() {
      difference() {
        linear_extrude(wing_d) wing2d();
        translate([0, 0, skin]) linear_extrude(wing_d) offset(delta = -wwall) wing2d();
      }
      // ledge the back plate rests on
      translate([0, 0, wing_d - 3]) linear_extrude(1)
        difference() { offset(delta = -wwall + 0.01) wing2d(); offset(delta = -wwall - 0.8) wing2d(); }
      // "finger bones" on the inside of the skin: show as darker lines when lit
      translate([0, 0, skin - 0.01]) linear_extrude(1.4) intersection() {
        offset(delta = -0.5) wing2d();
        union() {
          rib(WRIST, [101, 52]); rib(WRIST, [71, 32.5]); rib(WRIST, [39, 24.5]); rib(WRIST, [3, 70.5]);
        }
      }
      // tab that plugs into the body side (U channel, open at the back for wires)
      translate([-tab_len, tab_y[0], 0]) cube([tab_len + wwall, tab_y[1] - tab_y[0], wing_d]);
    }
    // wire channel through tab and root wall
    translate([-tab_len - 1, tab_y[0] + wwall, wwall]) cube([tab_len + 1 + wwall + 1, tab_y[1] - tab_y[0] - 2*wwall, wing_d]);
  }
}

module wing_back() {   // RIGHT wing back plate, modelled in place (Z = wing_d-2 .. wing_d)
  translate([0, 0, wing_d - 2]) linear_extrude(2) offset(delta = -wwall - 0.25) wing2d();
  // strip guide rails
  translate([strip_o[0], strip_o[1], wing_d - 3.5]) rotate([0, 0, strip_a])
    for (s = [-1, 1]) translate([4, s > 0 ? strip_w/2 : -strip_w/2 - 1.2, 0]) cube([86, 1.2, 1.51]);
}

// ======================================================================
module wing_place(sx) translate([sx * W/2, 0, wing_z0]) mirror([sx < 0 ? 1 : 0, 0, 0]) children();

if (part == "assembly") {
  color("DimGray") body();
  color("Silver", 0.9) clamp();
  color("SlateGray", 0.6) lid();
  for (sx = [-1, 1]) wing_place(sx) { color("WhiteSmoke", 0.85) wing_shell(); color("Gray") wing_back(); }
  // dummy electronics
  color("SteelBlue") translate([-sr_w/2, eye_y - sr_h/2, sr_front_z]) cube([sr_w, sr_h, pcb_t]);
  color("Silver") for (sx = [-1, 1]) translate([sx * eye_x, eye_y, sr_front_z - 12]) cylinder(d = 16, h = 12);
  color("RoyalBlue") translate([nano_x - nano_w/2, nano_y, nano_front]) cube([nano_w, pcb_t, 43.2]);
  color("Black") translate([0, bz_y, wall]) cylinder(d = 12, h = 9.5);
}
if (part == "body")  body();
if (part == "clamp") translate([0, 0, sr_front_z + pcb_t + 6]) mirror([0, 0, 1]) clamp();   // flat side down
if (part == "lid")   translate([0, 0, D + lid_t]) mirror([0, 0, 1]) lid();                   // outer face down
if (part == "wing_shell_R") wing_shell();
if (part == "wing_shell_L") mirror([1, 0, 0]) wing_shell();
if (part == "wing_back_R")  translate([0, 0, wing_d]) mirror([0, 0, 1]) wing_back();
if (part == "wing_back_L")  mirror([1, 0, 0]) translate([0, 0, wing_d]) mirror([0, 0, 1]) wing_back();
