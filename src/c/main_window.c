#include "main_window.h"

#include "data.h"

#include <pebble.h>

// The 12 rim colours, clockwise starting at 12 o'clock. A hand takes on the
// colour of whichever segment it currently points at on the outer rim.
static const GColor PRIDE_COLORS[12] = {
  GColorJaegerGreen,
  GColorMalachite,
  GColorMediumAquamarine,
  GColorCyan,
  GColorVividCerulean,
  GColorBlueMoon,
  GColorIndigo,
  GColorJazzberryJam,
  GColorFolly,
  GColorOrange,
  GColorRajah,
  GColorIcterine,
};

#define SEG_ANGLE (TRIG_MAX_ANGLE / 12)
#define HALF_SEG  (TRIG_MAX_ANGLE / 24)
#define ANIM_DURATION 1000
#define ANIM_DELAY    300

typedef struct {
  int hours;    // 0..11
  int minutes;  // 0..59
  int seconds;  // 0..59
} Clock;

static Window *s_window;
static Layer *s_canvas_layer;
static TextLayer *s_weekday_layer;
static TextLayer *s_day_layer;
static TextLayer *s_month_layer;
static TextLayer *s_step_layer;

// Current time and the pseudo-time used while the intro hands sweep.
static Clock s_now;
static Clock s_anim;

static char s_weekday_buffer[8];
static char s_day_buffer[3];
static char s_month_buffer[8];
static char s_step_buffer[16];

static bool s_animating;
static bool s_connected = true;
static bool s_info_right;
static int s_step_count;
static int s_battery = 100;
static bool s_show_battery;
static AppTimer *s_overlay_timer;

// Geometry, recomputed for the current display in window_load().
static GPoint s_center;
static int s_radius;
static int s_rim_width;
static int s_inner_radius;
static int s_minute_length;
static int s_hour_length;
static int s_hand_width;
static int s_cap_radius;

/******************************* Helpers *******************************/

static GColor segment_color(int32_t angle) {
  angle %= TRIG_MAX_ANGLE;
  if (angle < 0) {
    angle += TRIG_MAX_ANGLE;
  }
  int index = (int)(angle / SEG_ANGLE);
  return PRIDE_COLORS[index % 12];
}

static GPoint polar_point(GPoint center, int32_t angle, int radius) {
  angle %= TRIG_MAX_ANGLE;
  if (angle < 0) {
    angle += TRIG_MAX_ANGLE;
  }
  return (GPoint) {
    .x = (int16_t)(sin_lookup(angle) * (int32_t)radius / TRIG_MAX_RATIO) + center.x,
    .y = (int16_t)(-cos_lookup(angle) * (int32_t)radius / TRIG_MAX_RATIO) + center.y,
  };
}

/************************** Drawing **************************/

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GPoint c = s_center;

  graphics_context_set_antialiased(ctx, true);

  // 12 filled segments forming the coloured rim. On round displays the rim is
  // a ring hugging the bezel; on rectangular displays each segment is extended
  // outwards until it reaches the screen edges (the central dial stays round).
#ifdef PBL_ROUND
  GRect rim_rect = GRect(c.x - s_radius, c.y - s_radius, s_radius * 2, s_radius * 2);
  uint16_t rim_inset = s_rim_width;
#else
  GRect bounds = layer_get_bounds(layer);
  int outer = (bounds.size.w > bounds.size.h) ? bounds.size.w : bounds.size.h;
  GRect rim_rect = GRect(c.x - outer, c.y - outer, outer * 2, outer * 2);
  uint16_t rim_inset = outer - s_inner_radius;
#endif
  for (int i = 0; i < 12; i++) {
    int32_t start = i * SEG_ANGLE;
    int32_t end = (i + 1) * SEG_ANGLE;
    graphics_context_set_fill_color(ctx, PRIDE_COLORS[i]);
    graphics_fill_radial(ctx, rim_rect, GOvalScaleModeFitCircle, rim_inset, start, end);
  }

  // Crisp separators between adjacent segments.
  GRect inner_rect = GRect(c.x - s_inner_radius, c.y - s_inner_radius,
                           s_inner_radius * 2, s_inner_radius * 2);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < 12; i++) {
    int32_t a = i * SEG_ANGLE;
    GPoint rim_pt = gpoint_from_polar(rim_rect, GOvalScaleModeFitCircle, a);
    GPoint inner = gpoint_from_polar(inner_rect, GOvalScaleModeFitCircle, a);
    graphics_draw_line(ctx, inner, rim_pt);
  }

  // Battery gauge: hidden by default and revealed for a few seconds on a
  // shake. A full track sits just inside the rim, with the charge drawn over
  // it so the ring reads as complete. Charge colour warns as the battery
  // drains (green >= 40%, orange 39-15%, red <= 14%).
  if (data_get(DataKeyBattery) && s_show_battery && s_inner_radius > 10) {
    int br = s_inner_radius - 4;
    GRect batt_rect = GRect(c.x - br, c.y - br, br * 2, br * 2);
    GColor charge = (s_battery >= 40) ? GColorGreen
                  : (s_battery >= 15) ? GColorOrange
                                      : GColorRed;

    graphics_context_set_stroke_width(ctx, 2);

    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_draw_circle(ctx, c, br);

    if (s_battery >= 100) {
      graphics_context_set_stroke_color(ctx, charge);
      graphics_draw_circle(ctx, c, br);
    } else if (s_battery > 0) {
      // Keep a minimum span so a very low charge still shows as a sliver.
      int32_t span = TRIG_MAX_ANGLE * s_battery / 100;
      int32_t min_span = TRIG_MAX_ANGLE / 30;  // ~12 degrees
      if (span < min_span) {
        span = min_span;
      }
      graphics_context_set_stroke_color(ctx, charge);
      graphics_draw_arc(ctx, batt_rect, GOvalScaleModeFitCircle, 0, span);
    }
  }

  Clock t = s_animating ? s_anim : s_now;
  int32_t minute_angle = TRIG_MAX_ANGLE * t.minutes / 60;
  int32_t hour_angle = s_animating
      ? TRIG_MAX_ANGLE * t.hours / 60
      : TRIG_MAX_ANGLE * t.hours / 12 + minute_angle / 12;

  // Hour hand (drawn behind the minute hand).
  graphics_context_set_stroke_color(ctx, segment_color(hour_angle));
  graphics_context_set_stroke_width(ctx, s_hand_width);
  graphics_draw_line(ctx, c, polar_point(c, hour_angle, s_hour_length));

  // Minute hand.
  graphics_context_set_stroke_color(ctx, segment_color(minute_angle));
  graphics_context_set_stroke_width(ctx, s_hand_width);
  graphics_draw_line(ctx, c, polar_point(c, minute_angle, s_minute_length));

  // Optional second hand (thinner, with a small tail).
  if (data_get(DataKeySecondHand)) {
    int32_t second_angle = TRIG_MAX_ANGLE * t.seconds / 60;
    graphics_context_set_stroke_color(ctx, segment_color(second_angle));
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx,
                       polar_point(c, second_angle + TRIG_MAX_ANGLE / 2, s_radius / 6),
                       polar_point(c, second_angle, s_minute_length));
  }

  // Centre cap; a light hub when connected, dark when the phone is away.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, c, s_cap_radius);
  graphics_context_set_fill_color(ctx,
      (data_get(DataKeyBT) && !s_connected) ? GColorBlack : GColorDarkGray);
  graphics_fill_circle(ctx, c, s_cap_radius / 2);
}

/************************** Layout **************************/

static void update_geometry(GRect bounds) {
  s_center = grect_center_point(&bounds);
  int min_dim = (bounds.size.w < bounds.size.h) ? bounds.size.w : bounds.size.h;

  s_radius = min_dim / 2 - min_dim * 4 / 100;
  s_rim_width = s_radius * 14 / 100;
  if (s_rim_width < 6) {
    s_rim_width = 6;
  }
  s_inner_radius = s_radius - s_rim_width;
  s_minute_length = s_inner_radius - s_radius / 18;
  s_hour_length = s_minute_length - s_radius * 23 / 100;
  s_hand_width = (s_radius * 7 / 100) | 1;
  if (s_hand_width < 3) {
    s_hand_width = 3;
  }
  s_cap_radius = s_radius * 8 / 100;
  if (s_cap_radius < 4) {
    s_cap_radius = 4;
  }
}

static void update_text_layout(void) {
  GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
  int w = bounds.size.w;
  int cy = s_center.y;
  int r = s_radius;

  int box_w = w * 26 / 100;

  // Anchor to the dial centre/radius so the block scales with the bezel
  // rather than the (taller) screen, keeping clear of the battery ring.
#if defined(PBL_PLATFORM_GABBRO)
  // The round 260x260 screen takes the largest block, eased slightly outward.
  int x = s_info_right ? (w * 58 / 100) : (w * 16 / 100);
  layer_set_frame(text_layer_get_layer(s_weekday_layer),
                  GRect(x, cy - r * 50 / 100, box_w, 32));
  layer_set_frame(text_layer_get_layer(s_day_layer),
                  GRect(x, cy - r * 22 / 100, box_w, 48));
  layer_set_frame(text_layer_get_layer(s_month_layer),
                  GRect(x, cy + r * 16 / 100, box_w, 32));
#elif defined(PBL_PLATFORM_EMERY)
  // Bigger glyphs need the block pulled inward off the ring and the three
  // lines spread apart so the taller text does not collide.
  int x = s_info_right ? (w * 56 / 100) : (w * 18 / 100);
  layer_set_frame(text_layer_get_layer(s_weekday_layer),
                  GRect(x, cy - r * 50 / 100, box_w, 22));
  layer_set_frame(text_layer_get_layer(s_day_layer),
                  GRect(x, cy - r * 24 / 100, box_w, 40));
  layer_set_frame(text_layer_get_layer(s_month_layer),
                  GRect(x, cy + r * 14 / 100, box_w, 22));
#elif defined(PBL_PLATFORM_BASALT)
  // Nudged inward so the block no longer crosses the battery ring.
  int x = s_info_right ? (w * 58 / 100) : (w * 16 / 100);
  layer_set_frame(text_layer_get_layer(s_weekday_layer),
                  GRect(x, cy - r * 42 / 100, box_w, 20));
  layer_set_frame(text_layer_get_layer(s_day_layer),
                  GRect(x, cy - r * 26 / 100, box_w, 36));
  layer_set_frame(text_layer_get_layer(s_month_layer),
                  GRect(x, cy + r * 6 / 100, box_w, 20));
#else
  int x = s_info_right ? (w * 63 / 100) : (w * 11 / 100);
  layer_set_frame(text_layer_get_layer(s_weekday_layer),
                  GRect(x, cy - r * 42 / 100, box_w, 20));
  layer_set_frame(text_layer_get_layer(s_day_layer),
                  GRect(x, cy - r * 26 / 100, box_w, 36));
  layer_set_frame(text_layer_get_layer(s_month_layer),
                  GRect(x, cy + r * 6 / 100, box_w, 20));
#endif
}

/************************** Time **************************/

static void update_time(struct tm *tick_time) {
  s_now.hours = tick_time->tm_hour % 12;
  s_now.minutes = tick_time->tm_min;
  s_now.seconds = tick_time->tm_sec;

  snprintf(s_day_buffer, sizeof(s_day_buffer), "%d", tick_time->tm_mday);
  strftime(s_weekday_buffer, sizeof(s_weekday_buffer), "%a", tick_time);
  strftime(s_month_buffer, sizeof(s_month_buffer), "%b", tick_time);

  text_layer_set_text(s_weekday_layer, s_weekday_buffer);
  text_layer_set_text(s_day_layer, s_day_buffer);
  text_layer_set_text(s_month_layer, s_month_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);

  // Keep the date block clear of the minute hand.
  bool right = (s_now.minutes >= 40 && s_now.minutes <= 55);
  if (right != s_info_right) {
    s_info_right = right;
    update_text_layout();
  }

  layer_mark_dirty(s_canvas_layer);
}

/************************** Event handlers **************************/

static void bt_handler(bool connected) {
  if (!connected && s_connected) {
    vibes_long_pulse();
  }
  s_connected = connected;
  layer_mark_dirty(s_canvas_layer);
}

static void batt_handler(BatteryChargeState state) {
  s_battery = state.charge_percent;
  layer_mark_dirty(s_canvas_layer);
}

static void hide_overlays(void *context) {
  s_overlay_timer = NULL;
  s_show_battery = false;
  layer_set_hidden(text_layer_get_layer(s_step_layer), true);
  layer_mark_dirty(s_canvas_layer);
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  s_step_count = (int)health_service_sum_today(HealthMetricStepCount);
  snprintf(s_step_buffer, sizeof(s_step_buffer), "%d", s_step_count);
  text_layer_set_text(s_step_layer, s_step_buffer);
  layer_set_hidden(text_layer_get_layer(s_step_layer), false);

  // A shake also reveals the battery ring momentarily.
  s_show_battery = true;
  layer_mark_dirty(s_canvas_layer);

  if (s_overlay_timer) {
    app_timer_cancel(s_overlay_timer);
  }
  s_overlay_timer = app_timer_register(3000, hide_overlays, NULL);
}

/************************** Intro animation **************************/

static int anim_percentage(AnimationProgress progress, int max) {
  return (int)((int32_t)max * progress / ANIMATION_NORMALIZED_MAX);
}

static void hands_update(Animation *anim, AnimationProgress progress) {
  s_anim.hours = anim_percentage(progress, (s_now.hours * 60) / 12);
  s_anim.minutes = anim_percentage(progress, s_now.minutes);
  layer_mark_dirty(s_canvas_layer);
}

static void animation_started(Animation *anim, void *context) {
  s_animating = true;
}

static void animation_stopped(Animation *anim, bool stopped, void *context) {
  s_animating = false;
  layer_mark_dirty(s_canvas_layer);
}

static void start_intro_animation(void) {
  static AnimationImplementation impl = {
    .update = hands_update,
  };

  Animation *anim = animation_create();
  if (!anim) {
    return;
  }
  animation_set_duration(anim, ANIM_DURATION);
  animation_set_delay(anim, ANIM_DELAY);
  animation_set_curve(anim, AnimationCurveEaseInOut);
  animation_set_implementation(anim, &impl);
  animation_set_handlers(anim, (AnimationHandlers) {
    .started = animation_started,
    .stopped = animation_stopped,
  }, NULL);
  animation_schedule(anim);
}

/************************** Window **************************/

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  update_geometry(bounds);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);

  // Weekday/month use a small font, the day-of-month a large one.
#if defined(PBL_PLATFORM_GABBRO)
  const char *small_font = FONT_KEY_GOTHIC_24_BOLD;
  const char *big_font = FONT_KEY_LECO_38_BOLD_NUMBERS;
#elif defined(PBL_PLATFORM_EMERY)
  const char *small_font = FONT_KEY_GOTHIC_18_BOLD;
  const char *big_font = FONT_KEY_GOTHIC_28_BOLD;
#else
  const char *small_font = FONT_KEY_GOTHIC_14_BOLD;
  const char *big_font = FONT_KEY_GOTHIC_24_BOLD;
#endif

  s_weekday_layer = text_layer_create(GRectZero);
  text_layer_set_text_alignment(s_weekday_layer, GTextAlignmentCenter);
  text_layer_set_font(s_weekday_layer, fonts_get_system_font(small_font));
  text_layer_set_text_color(s_weekday_layer, GColorWhite);
  text_layer_set_background_color(s_weekday_layer, GColorClear);

  s_day_layer = text_layer_create(GRectZero);
  text_layer_set_text_alignment(s_day_layer, GTextAlignmentCenter);
  text_layer_set_font(s_day_layer, fonts_get_system_font(big_font));
  text_layer_set_text_color(s_day_layer, GColorVividCerulean);
  text_layer_set_background_color(s_day_layer, GColorClear);

  s_month_layer = text_layer_create(GRectZero);
  text_layer_set_text_alignment(s_month_layer, GTextAlignmentCenter);
  text_layer_set_font(s_month_layer, fonts_get_system_font(small_font));
  text_layer_set_text_color(s_month_layer, GColorWhite);
  text_layer_set_background_color(s_month_layer, GColorClear);

#if defined(PBL_PLATFORM_BASALT)
  // Nudge the step count inward so it sits inside the battery ring.
  int step_y = bounds.size.h * 64 / 100;
#else
  int step_y = bounds.size.h * 68 / 100;
#endif
  s_step_layer = text_layer_create(GRect(0, step_y, bounds.size.w, 34));
  text_layer_set_text_alignment(s_step_layer, GTextAlignmentCenter);
  text_layer_set_font(s_step_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_color(s_step_layer, GColorWhite);
  text_layer_set_background_color(s_step_layer, GColorClear);
  layer_set_hidden(text_layer_get_layer(s_step_layer), true);

  // Seed the clock before the intro animation starts.
  time_t now = time(NULL);
  struct tm *local = localtime(&now);
  update_time(local);

  main_window_reload_config();
  start_intro_animation();
}

static void window_unload(Window *window) {
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  connection_service_unsubscribe();
  battery_state_service_unsubscribe();

  if (s_overlay_timer) {
    app_timer_cancel(s_overlay_timer);
    s_overlay_timer = NULL;
  }

  layer_destroy(s_canvas_layer);
  text_layer_destroy(s_weekday_layer);
  text_layer_destroy(s_day_layer);
  text_layer_destroy(s_month_layer);
  text_layer_destroy(s_step_layer);
  window_destroy(s_window);
}

/************************** API **************************/

void main_window_push(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

void main_window_reload_config(void) {
  if (!s_window) {
    return;
  }
  Layer *root = window_get_root_layer(s_window);

  connection_service_unsubscribe();
  if (data_get(DataKeyBT)) {
    connection_service_subscribe((ConnectionHandlers) {
      .pebble_app_connection_handler = bt_handler,
    });
    s_connected = connection_service_peek_pebble_app_connection();
  } else {
    s_connected = true;
  }

  battery_state_service_unsubscribe();
  if (data_get(DataKeyBattery)) {
    battery_state_service_subscribe(batt_handler);
    s_battery = battery_state_service_peek().charge_percent;
  }

  accel_tap_service_unsubscribe();
  accel_tap_service_subscribe(accel_tap_handler);

  tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(
      data_get(DataKeySecondHand) ? SECOND_UNIT : MINUTE_UNIT, tick_handler);

  // The canvas paints the coloured rim, so it must sit *beneath* the date text;
  // the step popup stays above everything.
  layer_remove_from_parent(s_canvas_layer);
  layer_add_child(root, s_canvas_layer);

  layer_remove_from_parent(text_layer_get_layer(s_weekday_layer));
  layer_remove_from_parent(text_layer_get_layer(s_day_layer));
  layer_remove_from_parent(text_layer_get_layer(s_month_layer));

  if (data_get(DataKeyDate)) {
    layer_add_child(root, text_layer_get_layer(s_weekday_layer));
    layer_add_child(root, text_layer_get_layer(s_month_layer));
  }
  if (data_get(DataKeyDay)) {
    layer_add_child(root, text_layer_get_layer(s_day_layer));
  }

  update_text_layout();

  layer_remove_from_parent(text_layer_get_layer(s_step_layer));
  layer_add_child(root, text_layer_get_layer(s_step_layer));

  layer_mark_dirty(s_canvas_layer);
}
