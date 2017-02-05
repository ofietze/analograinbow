#include "main_window.h"
#include <pebble.h>

#define MARGIN             5
#define THICKNESS          6
#define ANIMATION_DELAY    300
#define ANIMATION_DURATION 1000
#define HAND_LENGTH_MIN    70
#define HAND_LENGTH_HOUR   (HAND_LENGTH_MIN - 20)

typedef struct {
  int days;
  int hours;
  int minutes;
} SimpleTime;

static Window *s_main_window;
static TextLayer *s_weekday_layer, *s_day_in_month_layer, *s_month_layer, *s_step_layer;
static Layer *s_canvas_layer, *s_bg_layer;
static GBitmap *s_bitmap;
static BitmapLayer *s_bitmap_layer;

// One each of these to represent the current time and an animated pseudo-time
static SimpleTime s_current_time, s_anim_time;

static char s_weekday_buffer[8], s_month_buffer[8], s_day_buffer[3], s_step_buffer[16];
static bool s_animating, s_is_connected,s_info_drawn;
static int s_step_count = 0, s_window_loads = 0;

/*************** Pebble Health **********************/
static void get_step_count() {
  s_step_count = (int)health_service_sum_today(HealthMetricStepCount);
}

// Is step data available?
bool step_data_is_available() {
  return HealthServiceAccessibilityMaskAvailable &
    health_service_metric_accessible(HealthMetricStepCount,
      time_start_of_today(), time(NULL));
}

static void display_step_count(int steps) {
  //step setup
  Layer *window_layer = window_get_root_layer(s_main_window);
  GRect bounds = layer_get_bounds(window_layer);
  s_step_layer = text_layer_create(GRect(0, PBL_IF_ROUND_ELSE(122, 118), bounds.size.w, 38));
  text_layer_set_text_alignment(s_step_layer, GTextAlignmentCenter);
  text_layer_set_font(s_step_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_color(s_step_layer, GColorWhite);
  text_layer_set_background_color(s_step_layer, GColorClear);
  
  snprintf(s_step_buffer,sizeof(s_step_buffer), "%d", steps );
  text_layer_set_text(s_step_layer, s_step_buffer);
  layer_add_child(window_layer, text_layer_get_layer(s_step_layer));
}

void delete_step_layer(){
  layer_remove_from_parent(text_layer_get_layer(s_step_layer));
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  // A tap event occured
  get_step_count();
  display_step_count(s_step_count);
  //Remove step layer after 3000 miliseconds
  app_timer_register(3000, (AppTimerCallback) delete_step_layer, NULL);
  }

/******************************* Event Services *******************************/
static void tick_handler(struct tm *tick_time, TimeUnits changed) {
  s_current_time.days = tick_time->tm_mday;
  s_current_time.hours = tick_time->tm_hour;
  s_current_time.minutes = tick_time->tm_min;
  s_current_time.hours -= (s_current_time.hours > 12) ? 12 : 0;
  
  snprintf(s_day_buffer, sizeof(s_day_buffer), "%d", s_current_time.days);
  strftime(s_weekday_buffer, sizeof(s_weekday_buffer), "%a", tick_time);
  strftime(s_month_buffer, sizeof(s_month_buffer), "%b", tick_time);

  text_layer_set_text(s_weekday_layer, s_weekday_buffer);
  text_layer_set_text(s_day_in_month_layer, s_day_buffer);
  text_layer_set_text(s_month_layer, s_month_buffer);

  // Finally
  layer_mark_dirty(s_canvas_layer);
}

static void bt_handler(bool connected) {
  // Notify disconnection
  if(!connected && s_is_connected) {
    vibes_long_pulse();
  }
  s_is_connected = connected;
  layer_mark_dirty(s_canvas_layer); 
}

static void batt_handler(BatteryChargeState state) {
  layer_mark_dirty(s_canvas_layer);
}

/************************** AnimationImplementation ***************************/
static void animation_started(Animation *anim, void *context) {
  s_animating = true;
}

static void animation_stopped(Animation *anim, bool stopped, void *context) {
  s_animating = false;

  main_window_reload_config();
}

static void animate(int duration, int delay, AnimationImplementation *implementation, bool handlers) {
  Animation *anim = animation_create();
  if(anim) {
    animation_set_duration(anim, duration);
    animation_set_delay(anim, delay);
    animation_set_curve(anim, AnimationCurveEaseInOut);
    animation_set_implementation(anim, implementation);
    if(handlers) {
      animation_set_handlers(anim, (AnimationHandlers) {
        .started = animation_started,
        .stopped = animation_stopped
      }, NULL);
    }
    animation_schedule(anim);
  }
}

/****************************** Drawing Functions *****************************/
static void bg_image(BitmapLayer *s_bitmap_layer,GBitmap *s_bitmap){
  s_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BG);
  s_bitmap_layer = bitmap_layer_create(GRect(0, 0, 180, 180));
  bitmap_layer_set_compositing_mode(s_bitmap_layer, GCompOpSet);
  bitmap_layer_set_bitmap(s_bitmap_layer, s_bitmap);
  
  //Add background image
  layer_add_child(window_get_root_layer(s_main_window), bitmap_layer_get_layer(s_bitmap_layer));
}

static GPoint make_hand_point(int quantity, int intervals, int len, GPoint center) {
  return (GPoint) {
    .x = (int16_t)(sin_lookup(TRIG_MAX_ANGLE * quantity / intervals) * (int32_t)len / TRIG_MAX_RATIO) + center.x,
    .y = (int16_t)(-cos_lookup(TRIG_MAX_ANGLE * quantity / intervals) * (int32_t)len / TRIG_MAX_RATIO) + center.y,
  };
}

static int hours_to_minutes(int hours_out_of_12) {
  return (hours_out_of_12 * 60) / 12;
}

/****************** First window element ********************/

static void info_window_delete(){
  if(s_info_drawn == true){
    layer_remove_from_parent(text_layer_get_layer(s_day_in_month_layer));
    layer_remove_from_parent(text_layer_get_layer(s_weekday_layer));
    layer_remove_from_parent(text_layer_get_layer(s_month_layer));
    s_info_drawn = false;
  }
}

static void window_draw_check(){
  /* This has to be called every minute to check 
  if the hands are in front of the text fields
    */
  //draw left or right depending on time
  //info on the right
  
  Layer *window_layer = window_get_root_layer(s_main_window);
  GRect bounds = layer_get_bounds(window_layer);
  int x_offset = (bounds.size.w * 62) / 100;
  int x_offset_alt = (bounds.size.w * 11) /100;
  int min = s_current_time.minutes;
  
 if (min >= 40 && min <= 55){
    s_weekday_layer = text_layer_create(GRect(x_offset, 55, 44, 40));    
    s_day_in_month_layer = text_layer_create(GRect(x_offset, 68, 44, 40));
    s_month_layer = text_layer_create(GRect(x_offset, 95, 44, 40));
  }
 //info on the left 
  else{ 
    s_weekday_layer = text_layer_create(GRect(x_offset_alt, 55, 44, 40));
    s_day_in_month_layer = text_layer_create(GRect(x_offset_alt, 68, 44, 40));
    s_month_layer = text_layer_create(GRect(x_offset_alt, 95, 44, 40));
   }
  
    text_layer_set_text_alignment(s_weekday_layer, GTextAlignmentCenter);
    text_layer_set_font(s_weekday_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_color(s_weekday_layer, GColorWhite);
    text_layer_set_background_color(s_weekday_layer, GColorClear);
  
    text_layer_set_text_alignment(s_day_in_month_layer, GTextAlignmentCenter);
    text_layer_set_font(s_day_in_month_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_color(s_day_in_month_layer, PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite));
    text_layer_set_background_color(s_day_in_month_layer, GColorClear);
  
    text_layer_set_text_alignment(s_month_layer, GTextAlignmentCenter);
    text_layer_set_font(s_month_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
    text_layer_set_text_color(s_month_layer, GColorWhite);
    text_layer_set_background_color(s_month_layer, GColorClear);

    s_info_drawn = true;
    s_window_loads += 1;
}



static void draw_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GPoint center = grect_center_point(&bounds);  

  SimpleTime mode_time = (s_animating) ? s_anim_time : s_current_time;

  int len_min = HAND_LENGTH_MIN;
  int len_hour = HAND_LENGTH_HOUR;

  // Plot shorter overlaid hands
  len_min -= (MARGIN + 2);
  GPoint minute_hand_short = make_hand_point(mode_time.minutes, 60, len_min, center);

  float minute_angle = TRIG_MAX_ANGLE * mode_time.minutes / 60;
  float hour_angle;
  if(s_animating) {
    // Hours out of 60 for smoothness
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 60;
  } else {
    hour_angle = TRIG_MAX_ANGLE * mode_time.hours / 12;
  }
  hour_angle += (minute_angle / TRIG_MAX_ANGLE) * (TRIG_MAX_ANGLE / 12);

  // Shorter hour overlay
  len_hour -= (MARGIN + 2);
  GPoint hour_hand_short = (GPoint) {
    .x = (int16_t)(sin_lookup(hour_angle) * (int32_t)len_hour / TRIG_MAX_RATIO) + center.x,
    .y = (int16_t)(-cos_lookup(hour_angle) * (int32_t)len_hour / TRIG_MAX_RATIO) + center.y,
  };

  // Draw hands 
  int min = s_current_time.minutes;
  
  //Change Color of minute hand to the one on the outer rim of the bg
  if(min < 6) graphics_context_set_stroke_color(ctx, GColorJaegerGreen);
  else if(min < 11) graphics_context_set_stroke_color(ctx, GColorMalachite);
  else if(min < 16) graphics_context_set_stroke_color(ctx, GColorMediumAquamarine);
  else if(min < 21) graphics_context_set_stroke_color(ctx, GColorCyan);
  else if(min < 26) graphics_context_set_stroke_color(ctx, GColorVividCerulean);
  else if(min < 31) graphics_context_set_stroke_color(ctx, GColorBlueMoon);
  else if(min < 36) graphics_context_set_stroke_color(ctx, GColorIndigo);
  else if(min < 41) graphics_context_set_stroke_color(ctx, GColorJazzberryJam);
  else if(min < 46) graphics_context_set_stroke_color(ctx, GColorFolly);
  else if(min < 51) graphics_context_set_stroke_color(ctx, GColorOrange);
  else if(min < 56) graphics_context_set_stroke_color(ctx, GColorRajah);
  else if(min < 60) graphics_context_set_stroke_color(ctx, GColorIcterine);
  
  for(int y = 0; y < THICKNESS; y++) {
    for(int x = 0; x < THICKNESS; x++) {
      graphics_draw_line(ctx, GPoint(center.x + x, center.y + y), GPoint(minute_hand_short.x + x, minute_hand_short.y + y));
    }
  }

  //Change Color of hour hand to the one on the outer rim of the bg
  int h = s_current_time.hours;
  
    if(h == 12 || h == 0) graphics_context_set_stroke_color(ctx, GColorJaegerGreen);
    else if(h == 1) graphics_context_set_stroke_color(ctx, GColorMalachite);
    else if(h == 2) graphics_context_set_stroke_color(ctx, GColorMediumAquamarine);  
    else if(h == 3) graphics_context_set_stroke_color(ctx, GColorCyan);
    else if(h == 4) graphics_context_set_stroke_color(ctx, GColorVividCerulean);
    else if(h == 5) graphics_context_set_stroke_color(ctx, GColorBlueMoon);
    else if(h == 6) graphics_context_set_stroke_color(ctx, GColorIndigo);
    else if(h == 7) graphics_context_set_stroke_color(ctx, GColorJazzberryJam);
    else if(h == 8) graphics_context_set_stroke_color(ctx, GColorFolly);
    else if(h == 9) graphics_context_set_stroke_color(ctx, GColorOrange);
    else if(h == 10) graphics_context_set_stroke_color(ctx, GColorRajah);
    else if(h == 11) graphics_context_set_stroke_color(ctx, GColorIcterine);
  
   for(int y = 0; y < THICKNESS; y++) {
    for(int x = 0; x < THICKNESS; x++) {
      graphics_draw_line(ctx, GPoint(center.x + x, center.y + y), GPoint(hour_hand_short.x + x, hour_hand_short.y + y));
    }
  }

  // Center
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(center.x + 1, center.y + 1), 7);

  // Draw black if disconnected
  if(data_get(DataKeyBT) && !s_is_connected) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(center.x + 1, center.y + 1), 3);
  }
  //window_draw_check();
}

/*********************************** Window ***********************************/
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  bg_image(s_bitmap_layer,s_bitmap);
  s_bg_layer = layer_create(bounds);
  layer_add_child(window_layer, s_bg_layer);
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, draw_proc);
  window_draw_check();
 // s_info_drawn = false;
}

static void window_unload(Window *window) {
    
  // Unsubscribe from tap events
  accel_tap_service_unsubscribe();
  
  layer_destroy(s_canvas_layer);
  layer_destroy(s_bg_layer);

  text_layer_destroy(s_weekday_layer);
  text_layer_destroy(s_day_in_month_layer);
  text_layer_destroy(s_month_layer);

  window_destroy(s_main_window);
}

static int anim_percentage(AnimationProgress dist_normalized, int max) {
  return (max * dist_normalized) / ANIMATION_NORMALIZED_MAX;
}

static void hands_update(Animation *anim, AnimationProgress dist_normalized) {
  s_current_time.hours -= (s_current_time.hours > 12) ? 12 : 0;

  s_anim_time.hours = anim_percentage(dist_normalized, hours_to_minutes(s_current_time.hours));
  s_anim_time.minutes = anim_percentage(dist_normalized, s_current_time.minutes);
  
  layer_mark_dirty(s_canvas_layer);
}

/************************************ API *************************************/

void main_window_push() {
  
  //load time earlier to use it for the window layout
  time_t t = time(NULL);
  struct tm *tm_now = localtime(&t);
  s_current_time.hours = tm_now->tm_hour;
  s_current_time.minutes = tm_now->tm_min;  

  tick_timer_service_unsubscribe();
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  connection_service_unsubscribe();

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_main_window, true);

  // Begin smooth animation
  static AnimationImplementation hands_impl = {
    .update = hands_update
  };
  animate(ANIMATION_DURATION, ANIMATION_DELAY, &hands_impl, true);

  main_window_reload_config();
}

void main_window_reload_config() {

  if(data_get(DataKeyBT)) {
    connection_service_subscribe((ConnectionHandlers) {
      .pebble_app_connection_handler = bt_handler
    });
    bt_handler(connection_service_peek_pebble_app_connection());
  }

  battery_state_service_unsubscribe();
  if(data_get(DataKeyBattery)) {
    battery_state_service_subscribe(batt_handler);
     
  // Subscribe to tap events
 accel_tap_service_subscribe(accel_tap_handler);
  }
  
  Layer *window_layer = window_get_root_layer(s_main_window);
  layer_remove_from_parent(text_layer_get_layer(s_day_in_month_layer));
  layer_remove_from_parent(text_layer_get_layer(s_weekday_layer));
  layer_remove_from_parent(text_layer_get_layer(s_month_layer));
    
  if (s_info_drawn == true && s_window_loads > 1){
     info_window_delete();
  }else{
    window_draw_check();
  }
  
  if(data_get(DataKeyDay)) {
    layer_add_child(window_layer, text_layer_get_layer(s_day_in_month_layer));
  }
  if(data_get(DataKeyDate)) {
    layer_add_child(window_layer, text_layer_get_layer(s_weekday_layer));
    layer_add_child(window_layer, text_layer_get_layer(s_month_layer));
  }
  
  layer_add_child(window_layer, s_canvas_layer); 
}