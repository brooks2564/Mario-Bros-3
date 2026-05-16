#include <pebble.h>

/* ── Screen / layout ───────────────────────────────────────── */
#define SCREEN_W      200
#define SCREEN_H      228
#define HUD_H          26   /* top HUD bar height                */
#define LEVEL_Y        61   /* y where level image starts (228-167) */
#define LEVEL_H       167   /* height of each level strip        */
#define STRIP_W       200   /* each strip is exactly 200px wide  */
#define LEVEL_TOTAL_W 2813  /* full level image width            */
#define LEVEL_SCROLL_W 2613 /* scrollable range (2813-200)       */
#define GROUND_WATCH_Y 200  /* ground surface: LEVEL_Y(61)+level_y(139)=200 */
#define SKY_TOP_Y       26  /* below HUD                         */

/* ── Step goal options ─────────────────────────────────────── */
static const int STEP_GOALS[] = {5000, 8000, 10000, 15000, 20000};
#define STEP_GOAL_COUNT 5
#define PERSIST_KEY_GOAL 0

/* ── Mario animation ───────────────────────────────────────── */
#define MARIO_X           30  /* fixed screen X for Mario            */
#define ANIM_INTERVAL     80  /* ms per animation frame (12.5 fps)   */
/* Mario runs at 7px per frame (~87px/s ≈ 5.5 tiles/s, close to NES speed) */
#define RUN_PX_PER_FRAME   7
/* Each wrist flick queues up 20% of the total scrollable level */
#define FLICK_SCROLL_PX   (LEVEL_SCROLL_W / 5)
/* Jump physics */
#define JUMP_VEL         -14
#define GRAVITY            2

/* ── Resource IDs for level strips ────────────────────────── */
static const uint32_t STRIP_RES[15] = {
    RESOURCE_ID_LEVEL_STRIP_00, RESOURCE_ID_LEVEL_STRIP_01,
    RESOURCE_ID_LEVEL_STRIP_02, RESOURCE_ID_LEVEL_STRIP_03,
    RESOURCE_ID_LEVEL_STRIP_04, RESOURCE_ID_LEVEL_STRIP_05,
    RESOURCE_ID_LEVEL_STRIP_06, RESOURCE_ID_LEVEL_STRIP_07,
    RESOURCE_ID_LEVEL_STRIP_08, RESOURCE_ID_LEVEL_STRIP_09,
    RESOURCE_ID_LEVEL_STRIP_10, RESOURCE_ID_LEVEL_STRIP_11,
    RESOURCE_ID_LEVEL_STRIP_12, RESOURCE_ID_LEVEL_STRIP_13,
    RESOURCE_ID_LEVEL_STRIP_14
};

/* ── State ─────────────────────────────────────────────────── */
static Window     *s_window;
static Layer      *s_canvas;

/* level scrolling */
static int         s_step_goal_idx = 2;
static int         s_steps         = 0;
static int         s_scroll_x      = 0;
static int         s_loaded_strip[2];
static GBitmap    *s_strip_bmp[2];

/* mario animation */
#define MARIO_STAND 0
#define MARIO_WALK1 1
#define MARIO_WALK2 2
#define MARIO_WALK3 3
#define MARIO_WALK4 4
#define MARIO_JUMP  5
static GBitmap    *s_sprite[6];
static int         s_sprite_h[6];
static int         s_anim_frame        = 0;
static int         s_scroll_bonus      = 0; /* px already advanced by flicks */
static int         s_scroll_remaining  = 0; /* px still to run from queued flicks */
static bool        s_jumping           = false;
static int         s_mario_vy          = 0;
static int         s_mario_y_off       = 0; /* px above ground (positive = up) */

/* HUD data */
static int         s_battery     = 100;
static int         s_heart_rate  = 0;
static int         s_temperature = 0;
static char        s_conditions[16];

/* goal overlay */
static bool        s_show_goal   = false;
static int         s_goal_ms     = 0;

static AppTimer   *s_anim_timer  = NULL;

/* ── Strip cache helpers ──────────────────────────────────── */
static void strip_ensure(int idx) {
    if (idx < 0 || idx >= 15) return;
    if (s_loaded_strip[0] == idx || s_loaded_strip[1] == idx) return;

    int evict = 0;
    if (s_loaded_strip[0] >= 0 && s_loaded_strip[1] >= 0) {
        int d0 = abs(s_loaded_strip[0] - idx);
        int d1 = abs(s_loaded_strip[1] - idx);
        evict = (d0 >= d1) ? 0 : 1;
    } else if (s_loaded_strip[0] >= 0) {
        evict = 1;
    }

    if (s_strip_bmp[evict]) {
        gbitmap_destroy(s_strip_bmp[evict]);
        s_strip_bmp[evict] = NULL;
    }
    s_strip_bmp[evict]    = gbitmap_create_with_resource(STRIP_RES[idx]);
    s_loaded_strip[evict] = idx;
}

static GBitmap *strip_get(int idx) {
    if (s_loaded_strip[0] == idx) return s_strip_bmp[0];
    if (s_loaded_strip[1] == idx) return s_strip_bmp[1];
    return NULL;
}

/* ── Scroll update from steps ────────────────────────────── */
static void update_scroll(void) {
    int goal = STEP_GOALS[s_step_goal_idx];
    int sx = (int)((int64_t)s_steps * LEVEL_SCROLL_W / goal);
    if (sx < 0) sx = 0;
    if (sx > LEVEL_SCROLL_W) sx = LEVEL_SCROLL_W;
    s_scroll_x = sx;
}

static int effective_scroll(void) {
    int ex = s_scroll_x + s_scroll_bonus;
    if (ex > LEVEL_SCROLL_W) ex = LEVEL_SCROLL_W;
    return ex;
}

/* ── Draw callback ──────────────────────────────────────────*/
static void draw_layer(Layer *layer, GContext *ctx) {
    /* 1. Sky strip (HUD bottom → level top) */
    graphics_context_set_fill_color(ctx, GColorFromRGB(156, 252, 240));
    graphics_fill_rect(ctx, GRect(0, SKY_TOP_Y, SCREEN_W, LEVEL_Y - SKY_TOP_Y),
                       0, GCornerNone);

    /* 2. Level image via strips */
    int sx     = effective_scroll();
    int s_idx  = sx / STRIP_W;
    int s_idx2 = (sx + SCREEN_W - 1) / STRIP_W;
    if (s_idx2 >= 15) s_idx2 = 14;

    strip_ensure(s_idx);
    if (s_idx2 != s_idx) strip_ensure(s_idx2);

    /* left strip */
    GBitmap *ba = strip_get(s_idx);
    if (ba) {
        int src_x = sx - s_idx * STRIP_W;
        src_x = src_x & ~1;  /* 4bpp alignment */
        int dst_w = STRIP_W - src_x;
        if (dst_w > SCREEN_W) dst_w = SCREEN_W;
        GBitmap *sub = gbitmap_create_as_sub_bitmap(ba, GRect(src_x, 0, dst_w, LEVEL_H));
        if (sub) {
            graphics_draw_bitmap_in_rect(ctx, sub, GRect(0, LEVEL_Y, dst_w, LEVEL_H));
            gbitmap_destroy(sub);
        }
    }

    /* right strip (partial) */
    if (s_idx2 != s_idx) {
        GBitmap *bb = strip_get(s_idx2);
        if (bb) {
            int dst_x = s_idx2 * STRIP_W - sx;
            int src_w = SCREEN_W - dst_x;
            if (src_w > STRIP_W) src_w = STRIP_W;
            if (src_w > 0) {
                GBitmap *sub = gbitmap_create_as_sub_bitmap(bb, GRect(0, 0, src_w, LEVEL_H));
                if (sub) {
                    graphics_draw_bitmap_in_rect(ctx, sub, GRect(dst_x, LEVEL_Y, src_w, LEVEL_H));
                    gbitmap_destroy(sub);
                }
            }
        }
    }

    /* 3. Mario — GCompOpSet respects the transparent palette entry */
    int sp_idx;
    if (s_jumping || s_mario_y_off > 0) {
        sp_idx = MARIO_JUMP;
    } else if (s_scroll_remaining > 0) {
        static const int wmap[] = {MARIO_WALK1, MARIO_WALK2, MARIO_WALK3, MARIO_WALK4};
        sp_idx = wmap[s_anim_frame % 4];
    } else {
        sp_idx = MARIO_STAND;
    }
    GBitmap *mario_bmp = s_sprite[sp_idx];
    if (mario_bmp) {
        int mh      = s_sprite_h[sp_idx];
        int mario_y = GROUND_WATCH_Y - mh - s_mario_y_off;
        GRect mb    = gbitmap_get_bounds(mario_bmp);
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, mario_bmp,
            GRect(MARIO_X, mario_y, mb.size.w, mh));
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    }

    /* 4. HUD bar */
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, 0, SCREEN_W, HUD_H), 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);

    GFont fnt = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

    /* MARIO / steps */
    graphics_draw_text(ctx, "MARIO", fnt, GRect(2, 0, 50, 13),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    char sbuf[12];
    snprintf(sbuf, sizeof(sbuf), "%d", s_steps);
    graphics_draw_text(ctx, sbuf, fnt, GRect(2, 13, 60, 13),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    /* heart rate */
    char hbuf[12];
    snprintf(hbuf, sizeof(hbuf), "\xe2\x99\xa5%d", s_heart_rate);
    graphics_draw_text(ctx, hbuf, fnt, GRect(68, 0, 44, 13),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    /* WORLD 1-1 */
    graphics_draw_text(ctx, "WORLD 1-1", fnt, GRect(70, 13, 60, 13),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    /* TIME / battery */
    graphics_draw_text(ctx, "TIME", fnt, GRect(150, 0, 48, 13),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    char bbuf[8];
    snprintf(bbuf, sizeof(bbuf), "%d%%", s_battery);
    graphics_draw_text(ctx, bbuf, fnt, GRect(150, 13, 48, 13),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    /* weather */
    if (s_conditions[0]) {
        char wx[24];
        snprintf(wx, sizeof(wx), "%d\xc2\xb0 %s", s_temperature, s_conditions);
        graphics_draw_text(ctx, wx, fonts_get_system_font(FONT_KEY_GOTHIC_14),
            GRect(SCREEN_W/2 - 40, 0, 80, 13),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }

    /* goal overlay */
    if (s_show_goal) {
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, GRect(40, 88, 120, 44), 4, GCornersAll);
        graphics_context_set_text_color(ctx, GColorYellow);
        char gbuf[24];
        snprintf(gbuf, sizeof(gbuf), "GOAL: %d", STEP_GOALS[s_step_goal_idx]);
        graphics_draw_text(ctx, gbuf,
            fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
            GRect(44, 97, 112, 28),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
}

/* ── Animation timer ────────────────────────────────────────────────── */
static void anim_callback(void *ctx) {
    s_anim_timer = NULL;

    /* scroll advance */
    if (s_scroll_remaining > 0) {
        int step = s_scroll_remaining < RUN_PX_PER_FRAME ? s_scroll_remaining : RUN_PX_PER_FRAME;
        int max_bonus = LEVEL_SCROLL_W - s_scroll_x;
        if (s_scroll_bonus + step > max_bonus) step = max_bonus - s_scroll_bonus;
        if (step < 0) step = 0;
        s_scroll_bonus    += step;
        s_scroll_remaining -= step;
        if (s_scroll_remaining < 0) s_scroll_remaining = 0;
        s_anim_frame = (s_anim_frame + 1) % 4;
    }

    /* jump physics */
    if (s_jumping) {
        s_mario_vy    += GRAVITY;
        s_mario_y_off -= s_mario_vy;
        if (s_mario_y_off <= 0) {
            s_mario_y_off = 0;
            s_mario_vy    = 0;
            s_jumping     = false;
        }
    }

    if (s_show_goal) {
        s_goal_ms -= ANIM_INTERVAL;
        if (s_goal_ms <= 0) s_show_goal = false;
    }

    layer_mark_dirty(s_canvas);

    if (s_scroll_remaining > 0 || s_jumping || s_mario_y_off > 0 || s_show_goal) {
        s_anim_timer = app_timer_register(ANIM_INTERVAL, anim_callback, NULL);
    }
}

/* ── Tick handler ───────────────────────────────────────────*/
static void tick_handler(struct tm *tick_time, TimeUnits changed) {
    if (health_service_metric_accessible(HealthMetricStepCount,
            time_start_of_today(), time(NULL)) & HealthServiceAccessibilityMaskAvailable) {
        s_steps = (int)health_service_sum_today(HealthMetricStepCount);
    }
    if (health_service_metric_accessible(HealthMetricHeartRateBPM,
            time(NULL) - 60, time(NULL)) & HealthServiceAccessibilityMaskAvailable) {
        s_heart_rate = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
    }
    update_scroll();
    if (tick_time->tm_min % 30 == 0) {
        DictionaryIterator *iter;
        if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
            dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, 1);
            app_message_outbox_send();
        }
    }
}

/* ── Health events ──────────────────────────────────────────*/
static void health_evt_cb(HealthEventType event, void *ctx) {
    if (event == HealthEventMovementUpdate || event == HealthEventMetricAlert) {
        if (health_service_metric_accessible(HealthMetricStepCount,
                time_start_of_today(), time(NULL)) & HealthServiceAccessibilityMaskAvailable) {
            s_steps = (int)health_service_sum_today(HealthMetricStepCount);
        }
        update_scroll();
        layer_mark_dirty(s_canvas);
    }
}

/* ── Battery ────────────────────────────────────────────────*/
static void battery_handler(BatteryChargeState state) {
    s_battery = state.charge_percent;
    layer_mark_dirty(s_canvas);
}

/* ── Ensure timer is running (used when animation needs to start) ── */
static void ensure_timer(void) {
    if (!s_anim_timer) {
        s_anim_timer = app_timer_register(ANIM_INTERVAL, anim_callback, NULL);
    }
}

/* ── AccelTap → jump + run forward 20% of level ────────────────── */
static void tap_handler(AccelAxisType axis, int32_t direction) {
    /* jump (only if on ground) */
    if (!s_jumping && s_mario_y_off == 0) {
        s_jumping   = true;
        s_mario_vy  = JUMP_VEL;
    }
    /* queue 20% more run distance */
    int headroom = LEVEL_SCROLL_W - s_scroll_x - s_scroll_bonus - s_scroll_remaining;
    int add = FLICK_SCROLL_PX < headroom ? FLICK_SCROLL_PX : headroom;
    if (add < 0) add = 0;
    s_scroll_remaining += add;
    ensure_timer();
}

/* ── Buttons: change step goal ──────────────────────────────*/
static void up_click(ClickRecognizerRef rec, void *ctx) {
    s_step_goal_idx = (s_step_goal_idx + 1) % STEP_GOAL_COUNT;
    persist_write_int(PERSIST_KEY_GOAL, s_step_goal_idx);
    s_scroll_bonus = 0;
    s_scroll_remaining = 0;
    update_scroll();
    s_show_goal = true;
    s_goal_ms   = 2000;
    ensure_timer();
}

static void down_click(ClickRecognizerRef rec, void *ctx) {
    s_step_goal_idx = (s_step_goal_idx + STEP_GOAL_COUNT - 1) % STEP_GOAL_COUNT;
    persist_write_int(PERSIST_KEY_GOAL, s_step_goal_idx);
    s_scroll_bonus = 0;
    s_scroll_remaining = 0;
    update_scroll();
    s_show_goal = true;
    s_goal_ms   = 2000;
    ensure_timer();
}

static void click_config(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_UP,   up_click);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
}

/* ── AppMessage (weather) ───────────────────────────────────*/
static void inbox_received(DictionaryIterator *iter, void *ctx) {
    Tuple *t = dict_find(iter, MESSAGE_KEY_TEMPERATURE);
    Tuple *c = dict_find(iter, MESSAGE_KEY_CONDITIONS);
    if (t) s_temperature = (int)t->value->int32;
    if (c) {
        strncpy(s_conditions, c->value->cstring, sizeof(s_conditions) - 1);
        s_conditions[sizeof(s_conditions) - 1] = '\0';
    }
    layer_mark_dirty(s_canvas);
}

/* ── Window handlers ────────────────────────────────────────*/
static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);

    s_canvas = layer_create(GRect(0, 0, SCREEN_W, SCREEN_H));
    layer_set_update_proc(s_canvas, draw_layer);
    layer_add_child(root, s_canvas);

    s_loaded_strip[0] = s_loaded_strip[1] = -1;
    s_strip_bmp[0]    = s_strip_bmp[1]    = NULL;

    s_sprite[MARIO_STAND] = gbitmap_create_with_resource(RESOURCE_ID_MARIO_STAND);
    s_sprite[MARIO_WALK1] = gbitmap_create_with_resource(RESOURCE_ID_MARIO_WALK1);
    s_sprite[MARIO_WALK2] = gbitmap_create_with_resource(RESOURCE_ID_MARIO_WALK2);
    s_sprite[MARIO_WALK3] = gbitmap_create_with_resource(RESOURCE_ID_MARIO_WALK3);
    s_sprite[MARIO_WALK4] = gbitmap_create_with_resource(RESOURCE_ID_MARIO_WALK4);
    s_sprite[MARIO_JUMP]  = gbitmap_create_with_resource(RESOURCE_ID_MARIO_JUMP);

    int hs[] = {22, 27, 27, 26, 26, 28};
    for (int i = 0; i < 6; i++) s_sprite_h[i] = hs[i];

    BatteryChargeState bcs = battery_state_service_peek();
    s_battery = bcs.charge_percent;

    accel_tap_service_subscribe(tap_handler);
}

static void window_unload(Window *window) {
    if (s_anim_timer) { app_timer_cancel(s_anim_timer); s_anim_timer = NULL; }
    accel_tap_service_unsubscribe();

    for (int i = 0; i < 2; i++) {
        if (s_strip_bmp[i]) { gbitmap_destroy(s_strip_bmp[i]); s_strip_bmp[i] = NULL; }
    }
    for (int i = 0; i < 6; i++) {
        if (s_sprite[i]) { gbitmap_destroy(s_sprite[i]); s_sprite[i] = NULL; }
    }
    layer_destroy(s_canvas);
}

/* ── App init / deinit ──────────────────────────────────────*/
static void init(void) {
    if (persist_exists(PERSIST_KEY_GOAL)) {
        int v = persist_read_int(PERSIST_KEY_GOAL);
        if (v >= 0 && v < STEP_GOAL_COUNT) s_step_goal_idx = v;
    }

    if (health_service_metric_accessible(HealthMetricStepCount,
            time_start_of_today(), time(NULL)) & HealthServiceAccessibilityMaskAvailable) {
        s_steps = (int)health_service_sum_today(HealthMetricStepCount);
    }
    if (health_service_metric_accessible(HealthMetricHeartRateBPM,
            time(NULL) - 60, time(NULL)) & HealthServiceAccessibilityMaskAvailable) {
        s_heart_rate = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
    }
    update_scroll();

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_handler);
    health_service_events_subscribe(health_evt_cb, NULL);

    app_message_register_inbox_received(inbox_received);
    app_message_open(256, 64);

    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_set_click_config_provider(s_window, click_config);
    window_stack_push(s_window, true);
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
}
