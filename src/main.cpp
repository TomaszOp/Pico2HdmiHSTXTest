// Akwarium na HDMI: Adafruit Feather RP2350 + Adafruit DVI Sock (HSTX)
// Bufor 320x240 RGB565, podwójne buforowanie, wyjście 640x480@60 Hz.

#include <Arduino.h>
#include <Adafruit_dvhstx.h>
#include <math.h>

static const int W = 320;
static const int H = 240;
static const int SAND_TOP = 205;   // gdzie zaczyna się piasek

// Feather RP2350: złącze HSTX -> domyślny pinout {12, 14, 16, 18}
DVHSTX16 display(DVHSTX_PINOUT_DEFAULT, DVHSTX_RESOLUTION_320x240, true);

// ---------------------------------------------------------------- narzędzia
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
static inline float frand(float a, float b) { return a + (b - a) * (random(0, 10001) / 10000.0f); }

static uint16_t waterRow[H];
static uint16_t sandBuf[(H - SAND_TOP) * W];

// ---------------------------------------------------------------- rośliny
struct Plant {
  float x;       // podstawa
  int segs;      // liczba segmentów
  float segLen;
  float phase;
  float speed;
  uint16_t color;
  uint16_t dark;
};
static const int NPLANTS = 9;
static Plant plants[NPLANTS];

// ---------------------------------------------------------------- bąbelki
struct Bubble {
  float x, y, r, vy, wob;
  bool alive;
};
static const int NBUB = 48;
static Bubble bubbles[NBUB];
static const float emitters[] = {40.0f, 175.0f, 290.0f};

// ---------------------------------------------------------------- ryby
struct Fish {
  float x, y, vx, vy;
  float tx, ty;     // cel
  float len, h;     // półosie ciała
  float maxSpeed;
  float facing;     // -1..1, płynnie zmienia się przy zawracaniu
  float tailPhase;
  uint16_t body, fin, stripe;
  bool striped;
};
static const int NFISH = 9;
static Fish fish[NFISH];

static void newTarget(Fish &f) {
  f.tx = frand(20, W - 20);
  f.ty = frand(25, SAND_TOP - 20);
}

static uint16_t randomFishColor(uint16_t *fin, uint16_t *stripe) {
  static const uint8_t pal[][3] = {
      {255, 140, 0},  {255, 215, 0},  {255, 60, 60},  {80, 200, 255},
      {255, 100, 200}, {170, 255, 90}, {255, 255, 255}, {160, 110, 255}};
  int i = random(0, 8);
  uint8_t r = pal[i][0], g = pal[i][1], b = pal[i][2];
  *fin = rgb(r * 3 / 4, g * 3 / 4, b * 3 / 4);
  *stripe = random(0, 2) ? rgb(255, 255, 255) : rgb(20, 20, 40);
  return rgb(r, g, b);
}

static void initFish(Fish &f) {
  f.x = frand(30, W - 30);
  f.y = frand(30, SAND_TOP - 30);
  f.len = frand(8, 20);
  f.h = f.len * frand(0.38f, 0.6f);
  f.maxSpeed = frand(18, 45) * (14.0f / f.len + 0.4f);
  f.vx = frand(-10, 10);
  f.vy = 0;
  f.facing = f.vx >= 0 ? 1 : -1;
  f.tailPhase = frand(0, 6.28f);
  f.body = randomFishColor(&f.fin, &f.stripe);
  f.striped = random(0, 3) == 0;
  newTarget(f);
}

// ---------------------------------------------------------------- scena statyczna
static void buildBackground() {
  for (int y = 0; y < H; y++) {
    float t = (float)y / SAND_TOP;
    if (t > 1) t = 1;
    uint8_t r = 10 + (uint8_t)(20 * (1 - t));
    uint8_t g = 60 + (uint8_t)(110 * (1 - t));
    uint8_t b = 110 + (uint8_t)(120 * (1 - t));
    waterRow[y] = rgb(r, g, b);
  }
  // piasek z ziarnistą teksturą i pofalowaną krawędzią
  for (int y = SAND_TOP; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int edge = SAND_TOP + 3 + (int)(3 * sinf(x * 0.05f) + 2 * sinf(x * 0.13f + 1.0f));
      uint16_t c;
      if (y < edge) {
        c = waterRow[y];
      } else {
        int n = random(-18, 18);
        int depth = (y - SAND_TOP);
        c = rgb(constrain(210 + n - depth, 0, 255), constrain(180 + n - depth, 0, 255),
                constrain(120 + n - depth, 0, 255));
      }
      sandBuf[(y - SAND_TOP) * W + x] = c;
    }
  }
}

static void drawRocks() {
  display.fillCircle(70, 214, 12, rgb(90, 90, 100));
  display.fillCircle(82, 218, 9, rgb(110, 110, 120));
  display.fillCircle(66, 210, 5, rgb(130, 130, 140));
  display.fillCircle(240, 218, 14, rgb(100, 95, 90));
  display.fillCircle(256, 222, 8, rgb(120, 115, 110));
  display.fillCircle(236, 212, 5, rgb(140, 135, 130));
}

static void initPlants() {
  static const float xs[NPLANTS] = {15, 32, 100, 120, 150, 200, 268, 285, 305};
  for (int i = 0; i < NPLANTS; i++) {
    Plant &p = plants[i];
    p.x = xs[i] + frand(-4, 4);
    p.segs = random(8, 15);
    p.segLen = frand(6, 10);
    p.phase = frand(0, 6.28f);
    p.speed = frand(0.8f, 1.6f);
    uint8_t g = random(120, 200);
    p.color = rgb(random(20, 70), g, random(30, 70));
    p.dark = rgb(10, g / 2, 25);
  }
}

// ---------------------------------------------------------------- rysowanie dynamiczne
static void drawBackground(float t) {
  uint16_t *buf = display.getBuffer();
  for (int y = 0; y < SAND_TOP; y++) {
    uint16_t c = waterRow[y];
    uint16_t *row = buf + y * W;
    for (int x = 0; x < W; x++) row[x] = c;
  }
  memcpy(buf + SAND_TOP * W, sandBuf, sizeof(sandBuf));

  // delikatne promienie światła z góry
  uint16_t ray = rgb(70, 170, 220);
  for (int i = 0; i < 4; i++) {
    float x0 = 40 + i * 80 + 15 * sinf(t * 0.3f + i);
    for (int y = 0; y < 120; y += 2) {
      int x = (int)(x0 + y * 0.35f);
      int w = 6 - y / 24;
      if (w > 0 && ((y / 2 + i) & 1)) display.drawFastHLine(x, y, w, ray);
    }
  }
}

static void drawPlants(float t) {
  for (int i = 0; i < NPLANTS; i++) {
    Plant &p = plants[i];
    float x = p.x, y = SAND_TOP + 6;
    for (int s = 0; s < p.segs; s++) {
      float k = (float)(s + 1) / p.segs;
      float ang = -1.5708f + 0.35f * k * sinf(t * p.speed + p.phase + s * 0.45f);
      float nx = x + cosf(ang) * p.segLen;
      float ny = y + sinf(ang) * p.segLen;
      int thick = 3 - (int)(k * 2);
      for (int d = -thick / 2; d <= thick / 2; d++) {
        display.drawLine((int)x + d, (int)y, (int)nx + d, (int)ny, p.color);
      }
      display.drawLine((int)x - 2, (int)y, (int)nx - 2, (int)ny, p.dark);
      // listki
      if (s % 2 == 1) {
        int side = (s / 2) % 2 ? 1 : -1;
        display.fillTriangle((int)nx, (int)ny, (int)(nx + side * 7), (int)(ny - 4),
                             (int)(nx + side * 2), (int)(ny + 3), p.color);
      }
      x = nx;
      y = ny;
    }
  }
}

static void spawnBubble() {
  for (int i = 0; i < NBUB; i++) {
    if (!bubbles[i].alive) {
      Bubble &b = bubbles[i];
      b.x = emitters[random(0, 3)] + frand(-3, 3);
      b.y = SAND_TOP + 2;
      b.r = frand(1, 3.5f);
      b.vy = frand(25, 45);
      b.wob = frand(0, 6.28f);
      b.alive = true;
      return;
    }
  }
}

static void updateBubbles(float dt, float t) {
  if (random(0, 100) < 25) spawnBubble();
  uint16_t c = rgb(200, 240, 255);
  uint16_t hl = rgb(255, 255, 255);
  for (int i = 0; i < NBUB; i++) {
    Bubble &b = bubbles[i];
    if (!b.alive) continue;
    b.y -= b.vy * dt;
    b.r += dt * 0.4f;
    if (b.y < -5) { b.alive = false; continue; }
    int x = (int)(b.x + 2.5f * sinf(t * 3 + b.wob + b.y * 0.05f));
    display.drawCircle(x, (int)b.y, (int)(b.r + 0.5f), c);
    display.drawPixel(x - 1, (int)b.y - 1, hl);
  }
}

static void updateFish(Fish &f, float dt) {
  float dx = f.tx - f.x, dy = f.ty - f.y;
  float d = sqrtf(dx * dx + dy * dy);
  if (d < 8 || random(0, 1000) < 3) newTarget(f);
  // sterowanie w stronę celu
  float acc = f.maxSpeed * 1.2f;
  if (d > 0.01f) {
    f.vx += dx / d * acc * dt;
    f.vy += dy / d * acc * dt * 0.6f;
  }
  // ściany
  if (f.x < 15) f.vx += acc * 2 * dt;
  if (f.x > W - 15) f.vx -= acc * 2 * dt;
  if (f.y < 15) f.vy += acc * 2 * dt;
  if (f.y > SAND_TOP - 15) f.vy -= acc * 2 * dt;

  float sp = sqrtf(f.vx * f.vx + f.vy * f.vy);
  if (sp > f.maxSpeed) {
    f.vx *= f.maxSpeed / sp;
    f.vy *= f.maxSpeed / sp;
    sp = f.maxSpeed;
  }
  f.vy *= (1 - 0.8f * dt);
  f.x += f.vx * dt;
  f.y += f.vy * dt;

  // płynne zawracanie
  float want = f.vx >= 0 ? 1.0f : -1.0f;
  f.facing += (want - f.facing) * min(1.0f, dt * 4.0f);
  f.tailPhase += dt * (4 + sp * 0.25f);
}

static void drawFish(const Fish &f) {
  float dir = f.facing >= 0 ? 1.0f : -1.0f;
  float sx = fabsf(f.facing);
  if (sx < 0.25f) sx = 0.25f;  // przy zawracaniu ryba "spłaszcza się"
  int L = max(2, (int)(f.len * sx));
  int Hh = max(2, (int)f.h);
  int cx = (int)f.x, cy = (int)f.y;

  // ogon (falujący)
  float wave = sinf(f.tailPhase);
  int tbx = cx - (int)(dir * L * 0.85f);
  int ttx = cx - (int)(dir * L * 1.6f);
  int tw = (int)(Hh * 0.9f);
  int toff = (int)(wave * Hh * 0.6f);
  display.fillTriangle(tbx, cy, ttx, cy - tw + toff, ttx, cy + tw + toff, f.fin);

  // płetwa grzbietowa i brzuszna
  display.fillTriangle(cx - (int)(dir * L * 0.4f), cy - Hh + 1, cx + (int)(dir * L * 0.25f), cy - Hh + 1,
                       cx - (int)(dir * L * 0.55f), cy - Hh - (int)(Hh * 0.6f), f.fin);
  display.fillTriangle(cx - (int)(dir * L * 0.1f), cy + Hh - 1, cx + (int)(dir * L * 0.2f), cy + Hh - 1,
                       cx - (int)(dir * L * 0.2f) + (int)(wave * 2), cy + Hh + (int)(Hh * 0.4f), f.fin);

  // ciało
  display.fillEllipse(cx, cy, L, Hh, f.body);
  if (f.striped) {
    for (int k = -1; k <= 1; k++) {
      int xs = cx + (int)(k * L * 0.4f);
      int hs = (int)(Hh * sqrtf(max(0.0f, 1.0f - (k * 0.4f) * (k * 0.4f)))) - 1;
      display.drawFastVLine(xs, cy - hs, hs * 2 + 1, f.stripe);
    }
  }
  // oko
  if (sx > 0.5f) {
    int ex = cx + (int)(dir * L * 0.55f);
    int ey = cy - Hh / 4;
    int er = max(1, Hh / 4);
    display.fillCircle(ex, ey, er, rgb(255, 255, 255));
    display.drawPixel(ex + (int)dir, ey, rgb(0, 0, 0));
    if (er > 1) display.fillCircle(ex + (int)dir, ey, er / 2, rgb(0, 0, 0));
  }
}

// ---------------------------------------------------------------- Arduino
void setup() {
  Serial.begin(115200);
  randomSeed(rp2040.hwrand32());
  if (!display.begin()) {
    // brak pamięci / błąd HSTX: miganie diodą
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(100);
    }
  }
  buildBackground();
  initPlants();
  for (int i = 0; i < NFISH; i++) initFish(fish[i]);
  // sortowanie: małe ryby rysowane najpierw (wydają się dalej)
  for (int i = 0; i < NFISH; i++)
    for (int j = i + 1; j < NFISH; j++)
      if (fish[j].len < fish[i].len) { Fish tmp = fish[i]; fish[i] = fish[j]; fish[j] = tmp; }
}

void loop() {
  static uint32_t last = millis();
  static uint32_t fpsT = millis();
  static int frames = 0;
  uint32_t now = millis();
  float dt = (now - last) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  last = now;
  float t = now / 1000.0f;

  drawBackground(t);
  drawPlants(t);
  drawRocks();
  for (int i = 0; i < NFISH; i++) {
    updateFish(fish[i], dt);
    drawFish(fish[i]);
  }
  updateBubbles(dt, t);

  display.swap();  // czeka na vsync i zamienia bufory

  frames++;
  if (now - fpsT >= 5000) {
    Serial.printf("FPS: %.1f\n", frames * 1000.0f / (now - fpsT));
    frames = 0;
    fpsT = now;
  }
}
