#include <pebble.h>
#pragma once


typedef enum {
  DataKeyDate = 0,
  DataKeyDay,
  DataKeyBT,
  DataKeyBattery,
  DataKeySecondHand,

  DataKeyCount
} DataKey;

void data_init();

void data_deinit();

void data_set(int key, bool value);

bool data_get(int key);

