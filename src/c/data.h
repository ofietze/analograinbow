#pragma once

#include <pebble.h>

typedef enum {
  DataKeyDate = 0,
  DataKeyDay,
  DataKeyBT,
  DataKeyBattery,
  DataKeySecondHand,

  DataKeyCount
} DataKey;

void data_init(void);

void data_deinit(void);

void data_set(int key, bool value);

bool data_get(int key);
