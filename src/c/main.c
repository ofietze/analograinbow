#include <pebble.h>

#include "comm.h"
#include "data.h"
#include "main_window.h"

static void init(void) {
  setlocale(LC_ALL, "");
  data_init();
  comm_init();

  main_window_push();
}

static void deinit(void) {
  data_deinit();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
