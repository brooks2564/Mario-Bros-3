#include <pebble.h>

// ---------- Screen / tile constants ----------
#define SCREEN_W 200
#define SCREEN_H 228
#define TILE     8      // 8px tiles (roughly 1:1 with NES pixels)
#define HUD_H    26     // top HUD bar height
#define GROUND_Y 188    // y where ground surface starts (tiles fill 188-228)
#define MARIO_W  16     // sprite width in px
#define MARIO_H  24     // sprite height in px (3 tiles)

// Mario ground standing position (feet at GROUND_Y)
#define MARIO_GROUND (GROUND_Y - MARIO_H)

// Block row y positions (top of block)
#define BLOCK_ROW_Y 138  // elevated brick/? block row
#define PIPE_X      152  // pipe x position
#define PIPE_Y      (GROUND_Y - 32)  // pipe top y

// ---------- Weather codes ----------
#define WEATHER_CLEAR   0
#define WEATHER_CLOUDY  1
#define WEATHER_RAIN    2
#define WEATHER_SNOW    3
#define WEATHER_STORM   4

// ---------- Animation ----------
#define ANIM_INTERVAL_MS  80
#define JUMP_VY_INIT      -7
#define GRAVITY           1
#define MARIO_SPEED       2
#define MAX_PARTICLES     12

// ---------- Mario color palette ----------
// 0=transparent 1=red 2=skin 3=brown 4=blue 5=black

// Small Mario SMB3-style sprite, 3 frames (8 cols x 12 rows drawn at 2px each)
// Row data: each uint8 = 8 pixels packed (but we use nibble approach per pixel)
// Using byte array: each entry is one pixel color index (0-5)
#define SPR_W 8
#define SPR_H 12

// Frame 0: standing
static const uint8_t s_mario_stand[SPR_H][SPR_W] = {
    {0,0,1,1,1,1,0,0},
    {0,1,1,1,1,1,1,0},
    {0,2,1,2,2,3,3,0},
    {0,2,2,2,2,2,3,0},
    {0,0,4,4,4,0,0,0},
    {0,4,4,4,4,4,0,0},
    {0,4,4,0,0,4,4,0},
    {0,4,4,0,0,4,4,0},
    {0,3,3,0,0,3,3,0},
    {3,3,3,0,0,3,3,3},
    {3,3,0,0,0,0,3,3},
    {0,0,0,0,0,0,0,0},
};

// Frame 1: run step 1 (weight on left foot)
static const uint8_t s_mario_run1[SPR_H][SPR_W] = {
    {0,0,1,1,1,1,0,0},
    {0,1,1,1,1,1,1,0},
    {0,2,1,2,2,3,3,0},
    {0,2,2,2,2,2,3,0},
    {0,0,4,4,4,0,0,0},
    {0,4,4,4,4,4,0,0},
    {0,3,4,4,0,4,0,0},
    {0,3,4,0,0,0,4,0},
    {0,3,3,0,0,0,0,0},
    {0,3,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
};

// Frame 2: run step 2 (weight on right foot)
static const uint8_t s_mario_run2[SPR_H][SPR_W] = {
    {0,0,1,1,1,1,0,0},
    {0,1,1,1,1,1,1,0},
    {0,2,1,2,2,3,3,0},
    {0,2,2,2,2,2,3,0},
    {0,0,4,4,4,0,0,0},
    {0,4,4,4,4,4,0,0},
    {0,0,4,0,4,4,3,0},
    {0,4,0,0,0,4,3,0},
    {0,0,0,0,0,3,3,0},
    {0,0,0,0,0,0,3,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
};

// Frame 3: jump
static const uint8_t s_mario_jump[SPR_H][SPR_W] = {
    {0,0,1,1,1,1,0,0},
    {0,1,1,1,1,1,1,0},
    {0,2,1,2,2,3,3,0},
    {0,2,2,2,2,2,3,0},
    {0,0,4,4,4,0,0,0},
    {0,4,4,4,4,4,0,0},
    {3,3,4,0,0,4,3,3},
    {3,0,0,0,0,0,0,3},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},
};

// ---------- Block definitions ----------
// which column positions have bricks vs ? blocks in BLOCK_ROW_Y
// Columns in TILE units from x=0 (left)
// x=4,5,6: bricks; x=12: ? block; x=16,17,18: bricks; x=21: ? block
static const int s_block_cols[]   = {4,5,6,12,16,17,18,21};
static const bool s_block_is_q[] = {false,false,false,true,false,false,false,true};
#define N_BLOCKS 8

// Which ? blocks have been hit (revealed)
static bool s_block_hit[N_BLOCKS];
// Hit animation counter per block (0 = idle)
static int s_block_bounce[N_BLOCKS];

// ---------- Particle system ----------
typedef struct {
    int x, y;
    int vx, vy;
    bool active;
} Particle;
static Particle s_particles[MAX_PARTICLES];

// ---------- State ----------
static Window   *s_window;
static Layer    *s_canvas;
static AppTimer *s_anim_timer;

static int s_mario_x      = 20;
static int s_mario_y      = MARIO_GROUND;
static int s_mario_facing = 1;    // 1=right, -1=left
static int s_mario_frame  = 0;
static int s_mario_vy     = 0;
static bool s_mario_jumping = false;
static int s_frame_tick   = 0;   // increments every ANIM_INTERVAL

static int s_steps       = 0;
static int s_heart_rate  = 0;
static int s_battery_pct = 100;
static int s_temperature = 20;
static int s_weather     = WEATHER_CLEAR;
static char s_conditions[32] = "Clear";

// Cloud positions (x scrolls slowly)
static int s_cloud_offsets[3] = {10, 80, 150};

// ---------- Helpers ----------
static GColor mario_color(int idx) {
    switch(idx) {
        case 1: return GColorRed;
        case 2: return GColorMelon;
        case 3: return GColorWindsorTan;
        case 4: return GColorCobaltBlue;
        case 5: return GColorBlack;
        default: return GColorClear;
    }
}

static void draw_mario_sprite(GContext *ctx, int bx, int by, int facing,
                               const uint8_t spr[SPR_H][SPR_W]) {
    int px = 2; // pixel scale
    for (int row = 0; row < SPR_H; row++) {
        for (int col = 0; col < SPR_W; col++) {
            int cidx = (facing == 1) ? spr[row][col] : spr[row][SPR_W-1-col];
            if (cidx == 0) continue;
            GColor c = mario_color(cidx);
            graphics_context_set_fill_color(ctx, c);
            graphics_fill_rect(ctx,
                GRect(bx + col*px, by + row*px, px, px),
                0, GCornerNone);
        }
    }
}

// Draw a single NES-style ground tile (8x8)
static void draw_ground_tile(GContext *ctx, int tx, int ty) {
    GRect r = GRect(tx, ty, TILE, TILE);
    graphics_context_set_fill_color(ctx, GColorFromHEX(0xC86010)); // dark orange
    graphics_fill_rect(ctx, r, 0, GCornerNone);
    // brick lines
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x8B3A00));
    graphics_fill_rect(ctx, GRect(tx, ty, TILE, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(tx, ty, 1, TILE), 0, GCornerNone);
}

// Draw a brick block (8x8)
static void draw_brick(GContext *ctx, int tx, int ty) {
    graphics_context_set_fill_color(ctx, GColorFromHEX(0xC84800));
    graphics_fill_rect(ctx, GRect(tx, ty, TILE, TILE), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x8B2800));
    // mortar lines
    graphics_fill_rect(ctx, GRect(tx, ty, TILE, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(tx, ty+4, TILE, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(tx, ty, 1, 4), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(tx+4, ty+4, 1, 4), 0, GCornerNone);
}

// Draw a ? block (8x8)
static void draw_qblock(GContext *ctx, int tx, int ty, bool hit, int bounce) {
    int draw_ty = ty - bounce;
    GColor bg = hit ? GColorFromHEX(0xC88030) : GColorYellow;
    GColor border = GColorFromHEX(0x884400);
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, GRect(tx, draw_ty, TILE, TILE), 0, GCornerNone);
    // border
    graphics_context_set_fill_color(ctx, border);
    graphics_fill_rect(ctx, GRect(tx, draw_ty, TILE, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(tx, draw_ty+TILE-1, TILE, 1), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(tx, draw_ty, 1, TILE), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(tx+TILE-1, draw_ty, 1, TILE), 0, GCornerNone);
    if (!hit) {
        // draw ? mark
        graphics_context_set_fill_color(ctx, GColorWhite);
        // top two pixels of ?
        graphics_fill_rect(ctx, GRect(tx+2, draw_ty+1, 3, 1), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(tx+4, draw_ty+2, 1, 2), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(tx+2, draw_ty+4, 2, 1), 0, GCornerNone);
        graphics_fill_rect(ctx, GRect(tx+2, draw_ty+6, 2, 1), 0, GCornerNone);
    }
}

// Draw pipe
static void draw_pipe(GContext *ctx, int px, int py) {
    // pipe cap (top 8px, 2px wider each side)
    graphics_context_set_fill_color(ctx, GColorIslamicGreen);
    graphics_fill_rect(ctx, GRect(px-2, py, 20, 8), 0, GCornerNone);
    // pipe body
    graphics_fill_rect(ctx, GRect(px, py+8, 16, GROUND_Y - py - 8), 0, GCornerNone);
    // shading
    graphics_context_set_fill_color(ctx, GColorKellyGreen);
    graphics_fill_rect(ctx, GRect(px+2, py+1, 4, 6), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(px+2, py+9, 4, GROUND_Y - py - 10), 0, GCornerNone);
}

// Draw a NES-style cloud (at x,y = top-left)
static void draw_cloud(GContext *ctx, int cx, int cy) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    // three bumps
    graphics_fill_circle(ctx, GPoint(cx+6, cy+6), 6);
    graphics_fill_circle(ctx, GPoint(cx+14, cy+4), 8);
    graphics_fill_circle(ctx, GPoint(cx+22, cy+6), 6);
    // base
    graphics_fill_rect(ctx, GRect(cx, cy+6, 28, 8), 0, GCornerNone);
    // green outline (SMB3 clouds have colored outline)
    graphics_context_set_stroke_color(ctx, GColorIslamicGreen);
}

// Draw sun (clear weather)
static void draw_sun(GContext *ctx) {
    int sx = 170, sy = 36;
    graphics_context_set_fill_color(ctx, GColorYellow);
    graphics_fill_circle(ctx, GPoint(sx, sy), 10);
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    // rays
    for (int i = 0; i < 8; i++) {
        int32_t angle = DEG_TO_TRIGANGLE(i * 45);
        int rx = sx + (sin_lookup(angle) * 14 / TRIG_MAX_RATIO);
        int ry = sy - (cos_lookup(angle) * 14 / TRIG_MAX_RATIO);
        graphics_fill_circle(ctx, GPoint(rx, ry), 2);
    }
}

// ---------- Particle spawning ----------
static void spawn_particle(int x, int y, int vx, int vy) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!s_particles[i].active) {
            s_particles[i] = (Particle){x, y, vx, vy, true};
            return;
        }
    }
}

static void spawn_weather_particle(void) {
    if (s_weather == WEATHER_RAIN) {
        int x = rand() % SCREEN_W;
        spawn_particle(x, HUD_H, 0, 4);
    } else if (s_weather == WEATHER_SNOW) {
        int x = rand() % SCREEN_W;
        spawn_particle(x, HUD_H, (rand()%3)-1, 2);
    }
}

static void update_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!s_particles[i].active) continue;
        s_particles[i].x += s_particles[i].vx;
        s_particles[i].y += s_particles[i].vy;
        if (s_particles[i].y > GROUND_Y || s_particles[i].x < 0 ||
            s_particles[i].x > SCREEN_W) {
            s_particles[i].active = false;
        }
    }
    // spawn new ones
    if (s_weather == WEATHER_RAIN && (s_frame_tick % 2 == 0)) spawn_weather_particle();
    if (s_weather == WEATHER_SNOW && (s_frame_tick % 4 == 0)) spawn_weather_particle();
}

// ---------- Update Mario ----------
static void update_mario(void) {
    // move horizontally
    s_mario_x += s_mario_facing * MARIO_SPEED;
    // bounce off walls (leave room for pipe)
    if (s_mario_x > SCREEN_W - MARIO_W - 4) {
        s_mario_facing = -1;
    }
    if (s_mario_x < 4) {
        s_mario_facing = 1;
    }
    // check pipe collision: avoid pipe area
    if (s_mario_facing == 1 && s_mario_x + MARIO_W >= PIPE_X - 2 &&
        s_mario_x < PIPE_X + 16 && !s_mario_jumping) {
        s_mario_facing = -1;
    }

    // gravity + jump physics
    if (s_mario_jumping) {
        s_mario_y += s_mario_vy;
        s_mario_vy += GRAVITY;
        if (s_mario_y >= MARIO_GROUND) {
            s_mario_y = MARIO_GROUND;
            s_mario_jumping = false;
            s_mario_vy = 0;
        }
        // check if mario hit a block from below
        if (s_mario_vy < 0) { // rising
            int mario_top = s_mario_y;
            if (mario_top <= BLOCK_ROW_Y + TILE && mario_top >= BLOCK_ROW_Y - 4) {
                int mario_cx = s_mario_x + MARIO_W/2;
                for (int i = 0; i < N_BLOCKS; i++) {
                    int bx = s_block_cols[i] * TILE;
                    if (mario_cx >= bx && mario_cx <= bx + TILE) {
                        if (!s_block_hit[i]) {
                            s_block_hit[i] = true;
                            s_block_bounce[i] = 4;
                            vibes_short_pulse();
                        }
                        s_mario_vy = 2; // bounce down
                        break;
                    }
                }
            }
        }
    }

    // update block bounce animations
    for (int i = 0; i < N_BLOCKS; i++) {
        if (s_block_bounce[i] > 0) s_block_bounce[i]--;
    }

    // cycle run frames when on ground
    if (!s_mario_jumping && (s_frame_tick % 3 == 0)) {
        s_mario_frame = (s_mario_frame + 1) % 3;
    }

    // scroll clouds
    if (s_frame_tick % 4 == 0) {
        for (int i = 0; i < 3; i++) {
            s_cloud_offsets[i] -= 1;
            if (s_cloud_offsets[i] < -32) s_cloud_offsets[i] = SCREEN_W;
        }
    }
}

// ---------- Draw ----------
static GColor sky_color(void) {
    switch (s_weather) {
        case WEATHER_RAIN:  return GColorFromHEX(0x607090);
        case WEATHER_SNOW:  return GColorFromHEX(0x9090A8);
        case WEATHER_STORM: return GColorFromHEX(0x404858);
        case WEATHER_CLOUDY: return GColorFromHEX(0x7090B0);
        default:            return GColorFromHEX(0x5898D4); // SMB3 blue sky
    }
}

static void draw_layer(Layer *layer, GContext *ctx) {
    (void)layer;

    // ---- SKY ----
    graphics_context_set_fill_color(ctx, sky_color());
    graphics_fill_rect(ctx, GRect(0, HUD_H, SCREEN_W, GROUND_Y - HUD_H), 0, GCornerNone);

    // weather: sun or storm clouds
    if (s_weather == WEATHER_CLEAR) {
        draw_sun(ctx);
        // clouds
        for (int i = 0; i < 3; i++) {
            draw_cloud(ctx, s_cloud_offsets[i], 35 + i*18);
        }
    } else if (s_weather == WEATHER_CLOUDY || s_weather == WEATHER_RAIN ||
               s_weather == WEATHER_SNOW   || s_weather == WEATHER_STORM) {
        for (int i = 0; i < 3; i++) {
            // darker storm clouds
            graphics_context_set_fill_color(ctx,
                s_weather == WEATHER_STORM ? GColorFromHEX(0x484860) : GColorLightGray);
            int cx = s_cloud_offsets[i];
            int cy = 32 + i*18;
            graphics_fill_circle(ctx, GPoint(cx+6,  cy+6),  5);
            graphics_fill_circle(ctx, GPoint(cx+13, cy+3),  7);
            graphics_fill_circle(ctx, GPoint(cx+20, cy+6),  5);
            graphics_fill_rect(ctx, GRect(cx, cy+5, 26, 7), 0, GCornerNone);
        }
    }

    // weather particles
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!s_particles[i].active) continue;
        if (s_weather == WEATHER_RAIN) {
            graphics_context_set_stroke_color(ctx, GColorCeleste);
            graphics_context_set_stroke_width(ctx, 1);
            graphics_draw_line(ctx,
                GPoint(s_particles[i].x, s_particles[i].y),
                GPoint(s_particles[i].x-1, s_particles[i].y+4));
        } else {
            graphics_context_set_fill_color(ctx, GColorWhite);
            graphics_fill_circle(ctx, GPoint(s_particles[i].x, s_particles[i].y), 2);
        }
    }

    // temperature label in sky
    static char temp_buf[12];
    snprintf(temp_buf, sizeof(temp_buf), "%d\xc2\xb0", s_temperature);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, temp_buf,
        fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD),
        GRect(4, HUD_H+2, 40, 16),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    // ---- BLOCKS ----
    for (int i = 0; i < N_BLOCKS; i++) {
        int bx = s_block_cols[i] * TILE;
        if (s_block_is_q[i]) {
            draw_qblock(ctx, bx, BLOCK_ROW_Y, s_block_hit[i], s_block_bounce[i]);
        } else {
            draw_brick(ctx, bx, BLOCK_ROW_Y);
        }
    }

    // ---- PIPE ----
    draw_pipe(ctx, PIPE_X, PIPE_Y);

    // ---- GROUND ----
    for (int row = GROUND_Y; row < SCREEN_H; row += TILE) {
        for (int col = 0; col < SCREEN_W; col += TILE) {
            draw_ground_tile(ctx, col, row);
        }
    }
    // green ground top strip
    graphics_context_set_fill_color(ctx, GColorIslamicGreen);
    graphics_fill_rect(ctx, GRect(0, GROUND_Y, SCREEN_W, 4), 0, GCornerNone);
    // cover pipe footprint in ground color (pipe emerges from ground)
    graphics_context_set_fill_color(ctx, GColorIslamicGreen);
    graphics_fill_rect(ctx, GRect(PIPE_X, GROUND_Y, 16, 4), 0, GCornerNone);

    // ---- MARIO ----
    {
        const uint8_t (*spr)[SPR_W];
        if (s_mario_jumping) {
            spr = s_mario_jump;
        } else {
            switch (s_mario_frame % 3) {
                case 1:  spr = s_mario_run1; break;
                case 2:  spr = s_mario_run2; break;
                default: spr = s_mario_stand; break;
            }
        }
        draw_mario_sprite(ctx, s_mario_x, s_mario_y, s_mario_facing, spr);
    }

    // ---- HUD ----
    // Black HUD background
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, HUD_H), 0, GCornerNone);

    // HUD text: steps as score, coins as HR, battery as TIME
    // Row layout: MARIO [steps]    *[hr]    TIME [bat]
    static char steps_buf[16], hr_buf[10], bat_buf[8];
    snprintf(steps_buf, sizeof(steps_buf), "%06d", s_steps > 999999 ? 999999 : s_steps);
    snprintf(hr_buf,    sizeof(hr_buf),    "x%d", s_heart_rate > 0 ? s_heart_rate : 0);
    snprintf(bat_buf,   sizeof(bat_buf),   "%d", s_battery_pct);

    GFont hud_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
    graphics_context_set_text_color(ctx, GColorWhite);

    // MARIO label
    graphics_draw_text(ctx, "MARIO",    hud_font, GRect(2,   0, 40, 13),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    // score (steps)
    graphics_draw_text(ctx, steps_buf,  hud_font, GRect(2,  13, 50, 13),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    // coin icon (yellow circle + HR)
    graphics_context_set_fill_color(ctx, GColorYellow);
    graphics_fill_circle(ctx, GPoint(62, 6), 4);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, hr_buf, hud_font, GRect(68, 0, 40, 13),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);

    // WORLD 1-1
    graphics_draw_text(ctx, "WORLD",    hud_font, GRect(106, 0,  50, 13),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, "1-1",      hud_font, GRect(110, 13, 40, 13),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // TIME (battery %)
    graphics_draw_text(ctx, "TIME",     hud_font, GRect(154, 0,  36, 13),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, bat_buf,    hud_font, GRect(156, 13, 36, 13),
        GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

// ---------- Animation timer ----------
static void anim_callback(void *data) {
    s_frame_tick++;
    update_mario();
    update_particles();
    layer_mark_dirty(s_canvas);
    s_anim_timer = app_timer_register(ANIM_INTERVAL_MS, anim_callback, NULL);
}

// ---------- Tap handler (wrist flick = jump) ----------
static void tap_handler(AccelAxisType axis, int32_t direction) {
    if (!s_mario_jumping) {
        s_mario_jumping = true;
        s_mario_vy = JUMP_VY_INIT;
    }
}

// ---------- Health ----------
static void update_health(void) {
    HealthServiceAccessibilityMask mask =
        health_service_metric_accessible(HealthMetricStepCount, time_start_of_today(), time(NULL));
    if (mask & HealthServiceAccessibilityMaskAvailable) {
        HealthValue steps = health_service_sum_today(HealthMetricStepCount);
        if (steps > 0) s_steps = (int)steps;
    }

    HealthServiceAccessibilityMask hr_mask =
        health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL) - 60, time(NULL));
    if (hr_mask & HealthServiceAccessibilityMaskAvailable) {
        HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
        if (hr > 0) s_heart_rate = (int)hr;
    }
}

// ---------- Battery ----------
static void battery_cb(BatteryChargeState state) {
    s_battery_pct = state.charge_percent;
}

// ---------- AppMessage (weather) ----------
static void inbox_cb(DictionaryIterator *iter, void *ctx) {
    Tuple *temp = dict_find(iter, MESSAGE_KEY_TEMPERATURE);
    Tuple *cond = dict_find(iter, MESSAGE_KEY_CONDITIONS);
    if (temp) s_temperature = (int)temp->value->int32;
    if (cond) {
        snprintf(s_conditions, sizeof(s_conditions), "%s", cond->value->cstring);
        const char *c = s_conditions;
        if      (strcmp(c, "Clear")   == 0) s_weather = WEATHER_CLEAR;
        else if (strcmp(c, "Cloudy")  == 0 || strcmp(c, "Fog") == 0) s_weather = WEATHER_CLOUDY;
        else if (strcmp(c, "Rain")    == 0 || strcmp(c, "Drizzle") == 0 ||
                 strcmp(c, "Showers") == 0) s_weather = WEATHER_RAIN;
        else if (strcmp(c, "Snow")    == 0 || strcmp(c, "Snow Grains") == 0 ||
                 strcmp(c, "Snow Shwrs") == 0) s_weather = WEATHER_SNOW;
        else if (strcmp(c, "T-Storm") == 0) s_weather = WEATHER_STORM;
        else s_weather = WEATHER_CLOUDY;
    }
}

// ---------- Tick ----------
static void tick_cb(struct tm *t, TimeUnits u) {
    (void)u;
    update_health();
    // request weather every 30 min
    if (t->tm_min % 30 == 0) {
        DictionaryIterator *out;
        if (app_message_outbox_begin(&out) == APP_MSG_OK) {
            dict_write_uint8(out, MESSAGE_KEY_REQUEST_WEATHER, 1);
            app_message_outbox_send();
        }
    }
}

// ---------- Window ----------
static void window_load(Window *win) {
    Layer *root = window_get_root_layer(win);
    GRect bounds = layer_get_bounds(root);
    s_canvas = layer_create(bounds);
    layer_set_update_proc(s_canvas, draw_layer);
    layer_add_child(root, s_canvas);

    srand(time(NULL));
    update_health();
    BatteryChargeState bat = battery_state_service_peek();
    s_battery_pct = bat.charge_percent;

    s_anim_timer = app_timer_register(ANIM_INTERVAL_MS, anim_callback, NULL);
    accel_tap_service_subscribe(tap_handler);
}

static void window_unload(Window *win) {
    if (s_anim_timer) { app_timer_cancel(s_anim_timer); s_anim_timer = NULL; }
    accel_tap_service_unsubscribe();
    layer_destroy(s_canvas);
}

static void health_evt_cb(HealthEventType event, void *ctx) {
    (void)ctx;
    if (event == HealthEventMovementUpdate || event == HealthEventSignificantUpdate) {
        update_health();
    }
}

// ---------- App lifecycle ----------
static void init(void) {
    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_cb);
    battery_state_service_subscribe(battery_cb);
    health_service_events_subscribe(health_evt_cb, NULL);

    app_message_register_inbox_received(inbox_cb);
    app_message_open(128, 128);

    // request weather immediately
    DictionaryIterator *out;
    if (app_message_outbox_begin(&out) == APP_MSG_OK) {
        dict_write_uint8(out, MESSAGE_KEY_REQUEST_WEATHER, 1);
        app_message_outbox_send();
    }
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    health_service_events_unsubscribe();
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
