#include <cassert>
#include <iostream>
#include <vector>
#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_util.h>
#include <xcb/xproto.h>

struct Client {
  xcb_window_t window;
  int x, y, w, h;
};

void arrange(xcb_connection_t *connection, xcb_screen_t *screen, std::vector<Client> &clients) {
  if(clients.empty()) return;
  Client &c = clients.back();
  
  // fullscreen
  c.x = 0, c.y = 0;
  c.h = screen->height_in_pixels;
  c.w = screen->width_in_pixels;

  int vals[4] = {c.x, c.y, c.w, c.h};
  int mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
  xcb_configure_window(connection, c.window, mask, vals);
}

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

  std::vector<Client> clients;

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
      case XCB_MAP_REQUEST: {
        xcb_map_request_event_t *e = (xcb_map_request_event_t*)  generic_event;
        
        Client c;
        c.window = e->window;

        clients.push_back(c);

        arrange(connection, screen, clients);
        xcb_map_window(connection, e->window);
        xcb_flush(connection);

        break;
      }
        
    }
    free(generic_event);
  }
  xcb_disconnect(connection);
  return 0;
}
