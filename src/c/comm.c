#include "comm.h"

#include "data.h"
#include "main_window.h"

static void in_recv_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;

  if ((t = dict_find(iter, MESSAGE_KEY_DataKeyDate))) {
    data_set(DataKeyDate, t->value->int32);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DataKeyDay))) {
    data_set(DataKeyDay, t->value->int32);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DataKeyBT))) {
    data_set(DataKeyBT, t->value->int32);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DataKeyBattery))) {
    data_set(DataKeyBattery, t->value->int32);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DataKeySecondHand))) {
    data_set(DataKeySecondHand, t->value->int32);
  }

  main_window_reload_config();
}

void comm_init(void) {
  app_message_register_inbox_received(in_recv_handler);
  app_message_open(APP_MESSAGE_INBOX_SIZE_MINIMUM, APP_MESSAGE_OUTBOX_SIZE_MINIMUM);
}
