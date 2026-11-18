/*
  The Bat That Sees With Sound
  ----------------------------
  Arduino Nano v3 (ATmega328P) + HC-SR04 + 2 x 5 RGBW NeoPixels (SK6812, 60 LED/m) + buzzer
  + capacitive touch button (TTP223 module, 3 pins: GND / VCC / OUT)

  Nobody within 1 m : wings breathe softly in green, silent ("the bat is resting")
  Coming in         : wings go green -> yellow -> red, chirps get faster and higher
  Very close        : red, fast flutter of chirps
  Each chirp sends a light "ripple" from the body out to the wing tips.

  Touch ("scratch the bat"): the bat is happy -> rainbow + happy trill for a few seconds.
  Double-tap (exactly 2 quick taps): chirping off / on. Remembered after power-off.

  If something stands still in front of the bat (a sofa, a wall) for STILL_TIMEOUT_MS,
  the bat stops beeping and goes back to resting until something moves again.

  Wiring (see README.md):
    D6  -> 330 ohm -> DIN of LEFT wing strip   (both strips on the same data pin:
    D6  -> 330 ohm -> DIN of RIGHT wing strip   they show the same picture, mirrored)
    D9  -> HC-SR04 TRIG
    D10 -> HC-SR04 ECHO
    D3  -> buzzer +   (passive buzzer: through a 220 ohm resistor)
    D2  -> touch module OUT
    5V / GND -> strips, sensor, buzzer -, touch module VCC / GND
  Library: "Adafruit NeoPixel" (Library Manager).
*/
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

// ---------------- hardware ----------------
#define LED_PIN        6
#define TRIG_PIN       9
#define ECHO_PIN       10
#define BUZZER_PIN     3
#define TOUCH_PIN      2
#define LEDS_PER_WING  5      // cut two pieces of 5 LEDs from a 60 LED/m strip

// 1 = passive buzzer (plays pitches, recommended), 0 = active buzzer (fixed beep).
// How to tell: connect the buzzer to 5V and GND for a moment. Beeps on its own -> active.
#define BUZZER_PASSIVE 1

// TTP223 default: OUT goes HIGH while touched. If your module is the other way round
// (bat is "happy" all the time and calms down when touched), set this to 0.
#define TOUCH_ACTIVE_HIGH 1

// ---------------- behaviour (tune these) ----------------
#define FAR_CM          100   // bat starts reacting closer than this (further = relaxed)
#define NEAR_CM         25    // full red / fastest chirps at or below this
#define BEEP_SLOW_MS    900   // pause between chirps at FAR_CM
#define BEEP_FAST_MS    90    // pause between chirps at NEAR_CM
#define CHIRP_MS        35    // length of one chirp
#define PITCH_LOW_HZ    1800  // chirp pitch far away (passive buzzer only)
#define PITCH_HIGH_HZ   3800  // chirp pitch very close
#define BRIGHT_IDLE_MAX 40    // resting "breathing" brightness (0-255)
#define BRIGHT_MIN      70    // brightness when someone is just in range
#define BRIGHT_MAX      150   // brightness when very close (keeps USB current low)
#define STILL_TIMEOUT_MS 20000UL // nothing moved for this long -> rest (silent)
#define STILL_BAND_CM   8     // movements smaller than this count as "still"
#define SOUND_ENABLED   1     // 0 = lights only (no sound at all)

#define RAINBOW_BRIGHT     110    // rainbow brightness (start-up and happy)
#define RAINBOW_CYCLE_MS   1500   // one full trip through the colours
#define START_RAINBOW_MS   3500   // rainbow at power-up, after the strip test
#define HAPPY_MS           4000   // rainbow after a scratch (longer if you keep touching)
#define HAPPY_SOUND        1      // 1 = happy trill on a scratch (passive buzzer, not muted)
#define TAP_MAX_MS         400    // a touch shorter than this is a "tap"
#define DOUBLE_TAP_GAP_MS  450    // taps closer together than this belong to one double-tap

#define MEASURE_EVERY_MS 60   // HC-SR04 needs >= 60 ms between pings
#define ECHO_TIMEOUT_US  9000UL  // ~1.5 m; no echo = nobody there

// RGBW strip (SK6812, separate white chip). If red and green look swapped, use NEO_RGBW.
// For a plain RGB strip (WS2812B) use NEO_GRB.
#define PIXEL_TYPE     (NEO_GRBW + NEO_KHZ800)
#define RIPPLE_WHITE   60     // white chip flash on each chirp (0 = off)
#define STRIP_TEST     1      // 1 = R, G, B, W test at power-up (1 s each); 0 = skip

#define EEPROM_MUTE_ADDR 0    // where "chirping off" is remembered
#define EEPROM_MUTED     0xA5 // value meaning "muted" (anything else = sound on)

Adafruit_NeoPixel wing(LEDS_PER_WING, LED_PIN, PIXEL_TYPE);

// ---------------- state ----------------
int   lastReadings[3] = {999, 999, 999};
uint8_t readIdx = 0;
float smoothCm = 999;           // filtered distance used for everything
unsigned long lastMeasure = 0;

unsigned long lastBeep = 0;
unsigned long rippleStart = 0;
bool rippleActive = false;

int   stillRefCm = 999;          // distance when things last moved
unsigned long stillSince = 0;
bool  resting = true;

bool  muted = false;             // chirping off (double-tap), stored in EEPROM

// touch
bool  touchRaw = false, touched = false;
unsigned long touchRawChange = 0, touchStart = 0, lastRelease = 0;
uint8_t tapCount = 0;            // taps in the current burst (99 = it was a scratch / long touch)
unsigned long happyUntil = 0;
bool  happy = false;

// happy trill (non-blocking, so double-taps are never missed)
const unsigned int trillNotes[] = {2600, 3100, 3700, 4400, 3700, 4400};
const uint8_t TRILL_LEN = sizeof(trillNotes) / sizeof(trillNotes[0]);
uint8_t trillIdx = TRILL_LEN;
unsigned long trillNext = 0;

// ---------------- helpers ----------------
int readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long us = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (us == 0) return 999;               // no echo: nothing in range
  return (int)(us / 58UL);
}

int median3(int a, int b, int c) {
  if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
  if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
  return c;
}

// 0.0 = far (or nobody), 1.0 = very close
float closeness(float cm) {
  if (cm >= FAR_CM) return 0.0;
  if (cm <= NEAR_CM) return 1.0;
  return (FAR_CM - cm) / (float)(FAR_CM - NEAR_CM);
}

// green -> yellow -> red
void colourFor(float t, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (t < 0.5) { r = (uint8_t)(255 * (t * 2.0)); g = 255; }
  else         { r = 255; g = (uint8_t)(220 * (1.0 - (t - 0.5) * 2.0)); }
  b = 0;
}

// plain beep, used for chirps and feedback (ignores mute: callers decide)
void beep(unsigned int hz, unsigned int ms) {
#if SOUND_ENABLED
#if BUZZER_PASSIVE
  tone(BUZZER_PIN, hz, ms);
#else
  (void)hz;
  digitalWrite(BUZZER_PIN, HIGH);
#endif
#endif
}

void buzzerOn(float t) {
  beep(PITCH_LOW_HZ + (unsigned int)((PITCH_HIGH_HZ - PITCH_LOW_HZ) * t), CHIRP_MS);
}

void buzzerUpdate(unsigned long now) {
#if SOUND_ENABLED && !BUZZER_PASSIVE
  if (now - lastBeep >= CHIRP_MS) digitalWrite(BUZZER_PIN, LOW);   // tone() stops itself
#endif
}

// blocking beep for feedback sequences (setup, mute toggle)
void beepWait(unsigned int hz, unsigned int ms) {
  beep(hz, ms);
  delay(ms);
#if SOUND_ENABLED && !BUZZER_PASSIVE
  digitalWrite(BUZZER_PIN, LOW);
#endif
}

// rainbow flowing from the body out to the wing tips
void drawRainbow(unsigned long now) {
  uint16_t base = (uint16_t)((now % RAINBOW_CYCLE_MS) * 65536UL / RAINBOW_CYCLE_MS);
  for (uint8_t i = 0; i < LEDS_PER_WING; i++) {
    uint16_t hue = base - (uint16_t)(i * (65536UL / (LEDS_PER_WING + 2)));
    wing.setPixelColor(i, wing.gamma32(wing.ColorHSV(hue, 255, RAINBOW_BRIGHT)));
  }
  wing.show();
}

// ---------------- strip type test ----------------
// At power-up the whole strip shows RED, GREEN, BLUE, then WHITE, 1 second each.
// What you should see with the right PIXEL_TYPE: red, green, blue, warm/neutral white.
//   red and green swapped              -> use NEO_RGBW
//   colours wrong / LEDs differ along
//   the strip / white step looks like
//   a mix of colours or stays dark     -> strip is not GRBW; tell me what you saw
// The same is printed on the Serial Monitor (9600 baud).
void stripTest() {
  const uint8_t L = 100;   // test brightness, kept moderate for USB power
  const char *names[4] = {"RED", "GREEN", "BLUE", "WHITE (white chip only)"};
  const uint32_t cols[4] = {wing.Color(L, 0, 0, 0), wing.Color(0, L, 0, 0),
                            wing.Color(0, 0, L, 0), wing.Color(0, 0, 0, L)};
  Serial.begin(9600);
  Serial.println(F("Strip test: R, G, B, W - 1 s each"));
  for (uint8_t c = 0; c < 4; c++) {
    Serial.print(F("Now showing: "));
    Serial.println(names[c]);
    wing.fill(cols[c]);
    wing.show();
    delay(1000);
  }
  wing.clear();
  wing.show();
  delay(300);
}

// ---------------- touch ----------------
void startHappy(unsigned long now) {
  if (!happy) {
    happy = true;
#if HAPPY_SOUND && BUZZER_PASSIVE
    if (!muted) { trillIdx = 0; trillNext = now; }
#endif
  }
  if ((long)(now + HAPPY_MS - happyUntil) > 0) happyUntil = now + HAPPY_MS;
}

void toggleMute() {
  muted = !muted;
  EEPROM.update(EEPROM_MUTE_ADDR, muted ? EEPROM_MUTED : 0x00);
  happy = false;                 // the double-tap is a command, not a scratch
  trillIdx = TRILL_LEN;
  // feedback: muted = 2 blue flashes + 2 low beeps, sound on = 2 white flashes + 2 high beeps
  // (these 2 beeps play even when muting, so you know it worked)
  for (uint8_t k = 0; k < 2; k++) {
    wing.fill(muted ? wing.Color(0, 0, 120, 0) : wing.Color(0, 0, 0, 120));
    wing.show();
    beepWait(muted ? 900 : 3500, 120);
    wing.clear();
    wing.show();
    delay(120);
  }
  lastBeep = millis();
}

void updateTouch(unsigned long now) {
  bool raw = (digitalRead(TOUCH_PIN) == HIGH) == (TOUCH_ACTIVE_HIGH == 1);
  if (raw != touchRaw) { touchRaw = raw; touchRawChange = now; }
  if (now - touchRawChange >= 15 && raw != touched) {      // 15 ms debounce
    touched = raw;
    if (touched) {
      touchStart = now;
      startHappy(now);                                     // every touch is a scratch
    } else {
      if (now - touchStart < TAP_MAX_MS) { if (tapCount < 99) tapCount++; }
      else tapCount = 99;                                  // long touch = scratching
      lastRelease = now;
    }
  }
  if (touched) startHappy(now);                            // keep happy while touched

  // a burst of taps is finished: exactly 2 quick taps -> chirping off / on
  if (!touched && tapCount && now - lastRelease > DOUBLE_TAP_GAP_MS) {
    if (tapCount == 2) toggleMute();
    tapCount = 0;
  }
  if (tapCount && touched && now - touchStart >= TAP_MAX_MS) tapCount = 99;
}

void updateTrill(unsigned long now) {
  if (trillIdx >= TRILL_LEN || (long)(now - trillNext) < 0) return;
  beep(trillNotes[trillIdx], 55);
  trillNext = now + 70;
  trillIdx++;
}

// ---------------- setup ----------------
void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);
  digitalWrite(BUZZER_PIN, LOW);
  muted = (EEPROM.read(EEPROM_MUTE_ADDR) == EEPROM_MUTED);
  wing.begin();
  wing.clear();
  wing.show();

#if STRIP_TEST
  stripTest();
#endif

  // rainbow
  unsigned long t0 = millis();
  while (millis() - t0 < START_RAINBOW_MS) { drawRainbow(millis()); delay(15); }
  wing.clear();
  wing.show();
  delay(200);

  // "Hello": light runs out to the wing tips green, yellow, red, with three rising chirps
  const uint8_t hello[3][3] = {{0, 255, 0}, {255, 200, 0}, {255, 0, 0}};
  for (uint8_t c = 0; c < 3; c++) {
    for (uint8_t i = 0; i < LEDS_PER_WING; i++) {
      wing.setPixelColor(i, wing.Color(hello[c][0] / 3, hello[c][1] / 3, hello[c][2] / 3));
      wing.show();
      delay(40);
    }
    if (!muted) beepWait(PITCH_LOW_HZ + (PITCH_HIGH_HZ - PITCH_LOW_HZ) / 2 * c, CHIRP_MS);
    else delay(CHIRP_MS);
    delay(120);
  }
  wing.clear();
  wing.show();
}

// ---------------- main loop ----------------
void loop() {
  unsigned long now = millis();

  // 0. touch: scratch = happy rainbow, double-tap = chirping off/on
  updateTouch(now);
  updateTrill(now);
  if (happy && (long)(now - happyUntil) >= 0 && !touched) {
    happy = false;
    lastBeep = now;               // start chirping calmly again
  }

  // 1. measure (median of last 3 pings, then smoothing)
  if (now - lastMeasure >= MEASURE_EVERY_MS) {
    lastMeasure = now;
    lastReadings[readIdx] = readDistanceCm();
    readIdx = (readIdx + 1) % 3;
    int cm = median3(lastReadings[0], lastReadings[1], lastReadings[2]);
    if (cm >= 999 || smoothCm >= 999) smoothCm = cm;           // jump straight in/out
    else smoothCm += (cm - smoothCm) * 0.4;

    // 2. "is anything moving?" -> rest after STILL_TIMEOUT_MS without movement
    if (abs(cm - stillRefCm) > STILL_BAND_CM) {
      stillRefCm = cm;
      stillSince = now;
    }
    resting = (smoothCm >= FAR_CM) || (now - stillSince > STILL_TIMEOUT_MS);
    now = millis();
  }

  if (happy) {                    // happy bat: rainbow, no chirps
    drawRainbow(now);
    delay(10);
    return;
  }

  float t = resting ? 0.0 : closeness(smoothCm);

  // 3. chirp timing: slow far away, fast up close (lights keep the rhythm when muted)
  if (!resting) {
    unsigned long gap = BEEP_SLOW_MS - (unsigned long)((BEEP_SLOW_MS - BEEP_FAST_MS) * t);
    if (now - lastBeep >= gap) {
      lastBeep = now;
      if (!muted) buzzerOn(t);
      rippleStart = now;
      rippleActive = true;
    }
  }
  buzzerUpdate(now);

  // 4. lights
  uint8_t r, g, b;
  uint8_t base;
  if (resting) {
    r = 0; g = 255; b = 0;
    // slow breathing, 4 s period
    float phase = (now % 4000UL) / 4000.0;
    float tri = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
    base = 4 + (uint8_t)((BRIGHT_IDLE_MAX - 4) * tri);
  } else {
    colourFor(t, r, g, b);
    base = BRIGHT_MIN + (uint8_t)((BRIGHT_MAX - BRIGHT_MIN) * t);
  }

  for (uint8_t i = 0; i < LEDS_PER_WING; i++) {
    uint16_t level = base;
    uint8_t white = 0;
    if (rippleActive && !resting) {
      long dt = (long)(now - rippleStart) - (long)i * 30;     // ripple: body -> wing tip
      if (dt >= 0 && dt < 140) {
        float k = 1.0 - dt / 140.0;
        level += (uint16_t)(100 * k);
        white = (uint8_t)(RIPPLE_WHITE * k);                   // white sparkle on the wave front
      }
    }
    if (level > 255) level = 255;
    wing.setPixelColor(i, wing.Color((r * level) >> 8, (g * level) >> 8, (b * level) >> 8, white));
  }
  if (rippleActive && now - rippleStart > 140 + LEDS_PER_WING * 30) rippleActive = false;
  wing.show();

  delay(10);
}
