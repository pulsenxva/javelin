#include <cassert>
#include <iostream>
#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_util.h>

int main() {
  int screen_number;
  bool is_running = true;

  xcb_connection_t *connection = xcb_connect(NULL, &screen_number);
  assert(!xcb_connection_has_error(connection));
  xcb_screen_t *screen = xcb_aux_get_screen(connection, screen_number);

  uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
  xcb_void_cookie_t cookie = xcb_change_window_attributes_checked(
      connection, screen->root, XCB_CW_EVENT_MASK, &mask);
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if(error) {
    std::cerr << "Another WM is already running\n";
    free(error);
    return 1;
  }

  xcb_flush(connection);

  xcb_generic_event_t *generic_event;
  while(is_running) {
    generic_event = xcb_wait_for_event(connection);
    if(!generic_event) {
      if(xcb_connection_has_error(connection)) {
        std::cout << "Connection to X server lost\n";
      }
      break;
    }
    switch(XCB_EVENT_RESPONSE_TYPE(generic_event)) {
    }
    free(generic_event);
  }
  xcb_disconnect(connection);
  return 0;
}
