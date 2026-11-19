/*
  The Bat That Sees With Sound
  ----------------------------
  Arduino Nano v3 (ATmega328P) + HC-SR04 + 2 x 5 RGBW NeoPixels (SK6812, 60 LED/m) + buzzer
  + capacitive touch button (TTP223 module, 3 pins: GND / VCC / OUT)

  Two moods (START_SCARED below, or 6 quick taps to switch; remembered after power-off):

  NORMAL bat
    Nobody within 2 m : wings breathe softly in green, silent ("the bat is resting")
    Coming in         : wings go green -> yellow -> red, chirps get faster and higher
    Very close        : red, fast flutter of chirps
    Scratch (touch)   : happy -> rainbow + happy trill

  SCARED bat
    Nobody within 2 m : cold blue trembling wings, fast high whimpering chirps
    Coming in         : calms down: blue -> green -> soft yellow, chirps get slow and low
    Very close        : soft calm yellow, slow chirps
    Alone for 20 s    : gets tired: dim blue trembling, silent
    Scratch (touch)   : he doesn't like it -> angry red/purple flashing + low grumble

  Each chirp sends a light "ripple" from the body out to the wing tips.
  Double-tap (exactly 2 quick taps): chirping off / on. Remembered after power-off.

  If something stands still in front of the bat (a sofa, a wall) for STILL_TIMEOUT_MS,
  the bat stops beeping and rests until something moves again.

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

// ---------------- mood ----------------
// 0 = normal bat (calm when alone, excited when someone comes close, likes scratches)
// 1 = scared bat (scared when alone, calms down when someone is near, hates scratches)
// 6 quick taps switch the mood; the bat remembers it after unplugging.
// Changing this number and uploading again always wins over the remembered mood.
#define START_SCARED   0

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
#define FAR_CM          200   // bat reacts closer than this (further = nobody there)
#define NEAR_CM         25    // strongest reaction at or below this
#define BEEP_SLOW_MS    900   // normal bat: pause between chirps at FAR_CM
#define BEEP_FAST_MS    90    // normal bat: pause between chirps at NEAR_CM
#define CHIRP_MS        35    // length of one chirp
#define PITCH_LOW_HZ    1800  // lowest chirp pitch (passive buzzer only)
#define PITCH_HIGH_HZ   3800  // highest chirp pitch
#define BRIGHT_IDLE_MAX 40    // resting "breathing" brightness (0-255)
#define BRIGHT_MIN      70    // brightness when someone is just in range
#define BRIGHT_MAX      150   // brightness when very close (keeps USB current low)
#define STILL_TIMEOUT_MS 20000UL // nothing moved for this long -> rest (silent)
#define STILL_BAND_CM   8     // movements smaller than this count as "still"
#define SOUND_ENABLED   1     // 0 = lights only (no sound at all)

// scared bat
#define SCARED_BEEP_FAST_MS 150   // pause between whimpers when nobody is near
#define SCARED_BEEP_SLOW_MS 1400  // pause between chirps when someone is very close
#define SCARED_BRIGHT       120   // brightness when most scared
#define CALM_BRIGHT         60    // brightness when calm (someone very close)
#define TREMBLE             50    // how much the scared wings flicker (0 = steady)

#define RAINBOW_BRIGHT     110    // rainbow brightness (start-up and happy)
#define RAINBOW_CYCLE_MS   1500   // one full trip through the colours
#define START_RAINBOW_MS   3500   // rainbow at power-up
#define HAPPY_MS           0      // scratch reaction lasts this long after the touch ends
#define HAPPY_SOUND        1      // 1 = trill (normal) / grumble (scared) on a scratch
#define TAP_MAX_MS         400    // a touch shorter than this is a "tap"
#define DOUBLE_TAP_GAP_MS  450    // taps closer together than this belong to one burst
#define TAPS_MUTE          2      // exactly this many taps: chirping off / on
#define TAPS_MOOD          6      // exactly this many taps: normal <-> scared

#define MEASURE_EVERY_MS 60   // HC-SR04 needs >= 60 ms between pings
#define ECHO_TIMEOUT_US  15000UL // ~2.5 m; no echo = nobody there

// RGBW strip (SK6812, separate white chip). If red and green look swapped, use NEO_RGBW.
// For a plain RGB strip (WS2812B) use NEO_GRB.
#define PIXEL_TYPE     (NEO_GRBW + NEO_KHZ800)
#define RIPPLE_WHITE   60     // white chip flash on each chirp (0 = off)

#define EEPROM_MUTE_ADDR 0    // where "chirping off" is remembered
#define EEPROM_MUTED     0xA5 // value meaning "muted" (anything else = sound on)
#define EEPROM_MOOD_ADDR 1    // remembered mood
#define EEPROM_SCARED    0x5C // value meaning "scared" (anything else = normal)
#define EEPROM_FLAG_ADDR 2    // START_SCARED value the mood was saved with
#define EEPROM_FLAG(x)   (0xF0 | (x))

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
bool  nobody = true;             // nothing within FAR_CM
bool  still = false;             // nothing moved for STILL_TIMEOUT_MS

bool  muted = false;             // chirping off (double-tap), stored in EEPROM
bool  scared = false;            // mood, stored in EEPROM

// touch
bool  touchRaw = false, touched = false;
unsigned long touchRawChange = 0, touchStart = 0, lastRelease = 0;
uint8_t tapCount = 0;            // taps in the current burst (99 = it was a scratch / long touch)
unsigned long happyUntil = 0;
bool  happy = false;             // scratch reaction running (happy rainbow or angry flashing)

// short tunes (non-blocking, so taps are never missed)
const unsigned int trillNotes[]   = {2600, 3100, 3700, 4400, 3700, 4400};   // happy
const unsigned int grumbleNotes[] = {420, 380, 340, 300, 260, 220};         // grumpy
const unsigned int *tune = trillNotes;
uint8_t tuneLen = 0, tuneIdx = 0;
unsigned int tuneStep = 70;
unsigned long tuneNext = 0;

uint8_t trembleOfs[LEDS_PER_WING];
unsigned long lastTremble = 0;

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

// normal bat: green -> yellow -> red
void colourFor(float t, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (t < 0.5) { r = (uint8_t)(255 * (t * 2.0)); g = 255; }
  else         { r = 255; g = (uint8_t)(220 * (1.0 - (t - 0.5) * 2.0)); }
  b = 0;
}

// scared bat: fear 1 = cold blue -> 0.5 = green -> 0 = soft calm yellow
void scaredColourFor(float fear, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (fear > 0.5) { float k = (fear - 0.5) * 2.0; r = 0; g = (uint8_t)(255 * (1.0 - k)); b = (uint8_t)(255 * k); }
  else            { float k = fear * 2.0; r = (uint8_t)(255 * (1.0 - k)); g = (uint8_t)(170 + 85 * k); b = 0; }
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

void buzzerUpdate(unsigned long now) {
#if SOUND_ENABLED && !BUZZER_PASSIVE
  if (now - lastBeep >= CHIRP_MS) digitalWrite(BUZZER_PIN, LOW);   // tone() stops itself
#else
  (void)now;
#endif
}

// blocking beep for feedback sequences (setup, mute / mood toggle)
void beepWait(unsigned int hz, unsigned int ms) {
  beep(hz, ms);
  delay(ms);
#if SOUND_ENABLED && !BUZZER_PASSIVE
  digitalWrite(BUZZER_PIN, LOW);
#endif
}

void startTune(const unsigned int *notes, uint8_t len, unsigned int step, unsigned long now) {
  tune = notes; tuneLen = len; tuneIdx = 0; tuneStep = step; tuneNext = now;
}
void stopTune() { tuneIdx = tuneLen; }

void updateTune(unsigned long now) {
  if (tuneIdx >= tuneLen || (long)(now - tuneNext) < 0) return;
  beep(tune[tuneIdx], tuneStep - 15);
  tuneNext = now + tuneStep;
  tuneIdx++;
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

// angry bat: neighbouring LEDs flash red / purple, swapping quickly
void drawAngry(unsigned long now) {
  bool flip = (now / 90) % 2;
  for (uint8_t i = 0; i < LEDS_PER_WING; i++) {
    bool red = ((i % 2) == 0) != flip;
    wing.setPixelColor(i, red ? wing.Color(160, 0, 0, 0) : wing.Color(110, 0, 140, 0));
  }
  wing.show();
}

// ---------------- touch ----------------
void startHappy(unsigned long now) {
  if (!happy) {
    happy = true;
#if HAPPY_SOUND && BUZZER_PASSIVE
    if (!muted) {
      if (scared) startTune(grumbleNotes, 6, 70, now);
      else        startTune(trillNotes, 6, 70, now);
    }
#endif
  }
  if ((long)(now + HAPPY_MS - happyUntil) > 0) happyUntil = now + HAPPY_MS;
#if HAPPY_SOUND && BUZZER_PASSIVE
  // a scared bat keeps grumbling for as long as you keep scratching
  if (scared && touched && !muted && tuneIdx >= tuneLen && (long)(now - tuneNext) > 150)
    startTune(grumbleNotes, 6, 70, now);
#endif
}

void toggleMute() {
  muted = !muted;
  EEPROM.update(EEPROM_MUTE_ADDR, muted ? EEPROM_MUTED : 0x00);
  happy = false;                 // the double-tap is a command, not a scratch
  stopTune();
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

void toggleMood() {
  scared = !scared;
  EEPROM.update(EEPROM_MOOD_ADDR, scared ? EEPROM_SCARED : 0x00);
  EEPROM.update(EEPROM_FLAG_ADDR, EEPROM_FLAG(START_SCARED));
  happy = false;
  stopTune();
  // feedback: purple swirl out to the wing tips (twice) with a rising tune (now scared)
  // or a falling tune (now normal). Plays even when muted, so you know it worked.
  const unsigned int up[4]   = {1200, 1800, 2600, 3600};
  const unsigned int down[4] = {3600, 2600, 1800, 1200};
  for (uint8_t k = 0; k < 2; k++) {
    for (uint8_t i = 0; i < LEDS_PER_WING; i++) {
      wing.clear();
      wing.setPixelColor(i, wing.Color(120, 0, 160, 0));
      if (i > 0) wing.setPixelColor(i - 1, wing.Color(30, 0, 40, 0));
      wing.show();
      if (k == 0 && i < 4) beepWait(scared ? up[i] : down[i], 90);
      else delay(90);
    }
  }
  wing.clear();
  wing.show();
  lastBeep = millis();
  stillSince = millis();         // react to the new mood straight away
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
  if (touched) startHappy(now);                            // keep reacting while touched

  // a burst of taps is finished: 2 taps -> chirping off / on, 6 taps -> mood
  if (!touched && tapCount && now - lastRelease > DOUBLE_TAP_GAP_MS) {
    if (tapCount == TAPS_MUTE) toggleMute();
    else if (tapCount == TAPS_MOOD) toggleMood();
    tapCount = 0;
  }
  if (tapCount && touched && now - touchStart >= TAP_MAX_MS) tapCount = 99;
}

// ---------------- setup ----------------
void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);
  digitalWrite(BUZZER_PIN, LOW);
  muted = (EEPROM.read(EEPROM_MUTE_ADDR) == EEPROM_MUTED);

  // mood: the remembered one, unless START_SCARED was changed since it was saved
  if (EEPROM.read(EEPROM_FLAG_ADDR) == EEPROM_FLAG(START_SCARED)) {
    scared = (EEPROM.read(EEPROM_MOOD_ADDR) == EEPROM_SCARED);
  } else {
    scared = START_SCARED;
    EEPROM.update(EEPROM_MOOD_ADDR, scared ? EEPROM_SCARED : 0x00);
    EEPROM.update(EEPROM_FLAG_ADDR, EEPROM_FLAG(START_SCARED));
  }
  randomSeed(analogRead(A0));

  wing.begin();
  wing.clear();
  wing.show();

  // rainbow
  unsigned long t0 = millis();
  while (millis() - t0 < START_RAINBOW_MS) { drawRainbow(millis()); delay(15); }
  wing.clear();
  wing.show();
  delay(200);

  // "Hello": light runs out to the wing tips three times with three rising chirps
  // normal: green, yellow, red    scared: blue, green, yellow
  const uint8_t helloNormal[3][3] = {{0, 255, 0}, {255, 200, 0}, {255, 0, 0}};
  const uint8_t helloScared[3][3] = {{0, 0, 255}, {0, 255, 0}, {255, 170, 0}};
  for (uint8_t c = 0; c < 3; c++) {
    const uint8_t *col = scared ? helloScared[c] : helloNormal[c];
    for (uint8_t i = 0; i < LEDS_PER_WING; i++) {
      wing.setPixelColor(i, wing.Color(col[0] / 3, col[1] / 3, col[2] / 3));
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

  // 0. touch: scratch = happy rainbow (or angry), 2 taps = chirping off/on, 6 taps = mood
  updateTouch(now);
  updateTune(now);
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
    nobody = (smoothCm >= FAR_CM);
    still  = (now - stillSince > STILL_TIMEOUT_MS);
    now = millis();
  }

  if (happy) {                    // scratch reaction: no chirps
    if (scared) drawAngry(now);
    else        drawRainbow(now);
    delay(10);
    return;
  }

  float t = nobody ? 0.0 : closeness(smoothCm);   // 0 = nobody / far, 1 = very close

  // 3. what the bat does now
  bool chirping;                  // chirps + ripples
  unsigned long gap = 0;
  unsigned int pitch = 0;
  uint8_t r, g, b, base;
  uint8_t trembleAmp = 0;

  if (!scared) {
    // ---- normal bat ----
    bool resting = nobody || still;
    chirping = !resting;
    if (resting) {
      r = 0; g = 255; b = 0;
      float phase = (now % 4000UL) / 4000.0;                  // slow breathing, 4 s period
      float tri = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
      base = 4 + (uint8_t)((BRIGHT_IDLE_MAX - 4) * tri);
    } else {
      colourFor(t, r, g, b);
      base = BRIGHT_MIN + (uint8_t)((BRIGHT_MAX - BRIGHT_MIN) * t);
      gap = BEEP_SLOW_MS - (unsigned long)((BEEP_SLOW_MS - BEEP_FAST_MS) * t);
      pitch = PITCH_LOW_HZ + (unsigned int)((PITCH_HIGH_HZ - PITCH_LOW_HZ) * t);
    }
  } else {
    // ---- scared bat ----
    float fear = 1.0 - t;
    float phase = (now % 4000UL) / 4000.0;
    float tri = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
    if (still && nobody) {                 // alone for a long time: tired, dim blue trembling
      chirping = false;
      r = 0; g = 0; b = 255;
      base = 6 + (uint8_t)((BRIGHT_IDLE_MAX - 6) * tri);
      trembleAmp = TREMBLE / 4;
    } else if (still) {                    // someone near but nothing moving: calm breathing
      chirping = false;
      scaredColourFor(fear, r, g, b);
      base = 4 + (uint8_t)((BRIGHT_IDLE_MAX - 4) * tri);
    } else {
      chirping = true;
      scaredColourFor(fear, r, g, b);
      base = CALM_BRIGHT + (uint8_t)((SCARED_BRIGHT - CALM_BRIGHT) * fear);
      trembleAmp = (uint8_t)(TREMBLE * fear);
      gap = SCARED_BEEP_FAST_MS + (unsigned long)((SCARED_BEEP_SLOW_MS - SCARED_BEEP_FAST_MS) * t);
      pitch = PITCH_HIGH_HZ - (unsigned int)((PITCH_HIGH_HZ - PITCH_LOW_HZ) * t);
      pitch += random(-200, 201) * fear;   // nervous wobble
    }
  }

  // 4. chirp timing (lights keep the rhythm when muted)
  if (chirping && now - lastBeep >= gap) {
    lastBeep = now;
    if (!muted) beep(pitch, CHIRP_MS);
    rippleStart = now;
    rippleActive = true;
  }
  buzzerUpdate(now);

  // 5. lights
  if (now - lastTremble >= 40) {           // new random flicker every 40 ms
    lastTremble = now;
    for (uint8_t i = 0; i < LEDS_PER_WING; i++) trembleOfs[i] = random(0, 256);
  }
  for (uint8_t i = 0; i < LEDS_PER_WING; i++) {
    int level = base;
    uint8_t white = 0;
    if (trembleAmp) level -= (int)((uint16_t)trembleOfs[i] * trembleAmp >> 8);
    if (rippleActive && chirping) {
      long dt = (long)(now - rippleStart) - (long)i * 30;     // ripple: body -> wing tip
      if (dt >= 0 && dt < 140) {
        float k = 1.0 - dt / 140.0;
        level += (int)(100 * k);
        white = (uint8_t)(RIPPLE_WHITE * k);                   // white sparkle on the wave front
      }
    }
    if (level > 255) level = 255;
    if (level < 0) level = 0;
    uint16_t lv = (uint16_t)level;         // unsigned maths: 255 * 255 does not fit in an int
    wing.setPixelColor(i, wing.Color((r * lv) >> 8, (g * lv) >> 8, (b * lv) >> 8, white));
  }
  if (rippleActive && now - rippleStart > 140 + LEDS_PER_WING * 30) rippleActive = false;
  wing.show();

  delay(10);
}
