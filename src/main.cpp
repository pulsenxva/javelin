#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_util.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

struct Client {
  xcb_window_t window;
  int x, y, w, h;
};

void arrange(xcb_connection_t *connection, xcb_screen_t *screen, std::vector<Client> &clients) {
  if(clients.empty()) return;
  
  if((int)clients.size() == 1) {
    Client &c = clients.back();
    c.x = 0, c.y = 0;
    c.h = screen->height_in_pixels;
    c.w = screen->width_in_pixels;

    int vals[4] = {c.x, c.y, c.w, c.h};
    int mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    xcb_configure_window(connection, c.window, mask, vals);
  } else {
    Client& master = clients[0];
    master.x = 0;
    master.y = 0;
    master.w = screen->width_in_pixels/2;
    master.h = screen->height_in_pixels;
    int mvals[4] = {master.x, master.y, master.w, master.h};
    int mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    xcb_configure_window(connection, master.window, mask, mvals);

    std::vector<Client> ri;
    for(int i = 1; i < (int)clients.size(); i++) {
      ri.push_back(clients[i]);
    }
    int common_height = screen->height_in_pixels/(int)ri.size();
    int cur_y = 0;
    for(auto& c: ri) {
      c.x = screen->width_in_pixels/2+1;
      c.w = screen->width_in_pixels/2;
      c.y = cur_y;
      c.h = common_height;
      cur_y += common_height+1;
      int vals[4] = {c.x, c.y, c.w, c.h};
      int mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
      xcb_configure_window(connection, c.window, mask, vals);
    }
  }
}

void close_window(xcb_connection_t *connection, xcb_window_t window,
                   xcb_atom_t wm_protocols, xcb_atom_t wm_delete_window) {
  xcb_client_message_event_t event = {};
  event.response_type = XCB_CLIENT_MESSAGE;
  event.format = 32;
  event.window = window;
  event.type = wm_protocols;
  event.data.data32[0] = wm_delete_window;
  event.data.data32[1] = XCB_CURRENT_TIME;

  xcb_send_event(connection, 0, window, XCB_EVENT_MASK_NO_EVENT, (const char*)&event);
  xcb_flush(connection);
}

int main() {
  int screen_number;
  bool is_running = true;

  xcb_connection_t *connection = xcb_connect(NULL, &screen_number);
  assert(!xcb_connection_has_error(connection));
  xcb_screen_t *screen = xcb_aux_get_screen(connection, screen_number);

  xcb_atom_t wm_protocols;
  xcb_atom_t wm_delete_window;
  xcb_intern_atom_cookie_t c1 = xcb_intern_atom(connection, 0, 12, "WM_PROTOCOLS");
  xcb_intern_atom_reply_t *r1 = xcb_intern_atom_reply(connection, c1, NULL);
  wm_protocols = r1->atom;
  free(r1);
  xcb_intern_atom_cookie_t c2 = xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");
  xcb_intern_atom_reply_t *r2 = xcb_intern_atom_reply(connection, c2, NULL);
  wm_delete_window = r2->atom;
  free(r2);

  uint32_t mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_KEY_PRESS;
  xcb_void_cookie_t cookie = xcb_change_window_attributes_checked(
      connection, screen->root, XCB_CW_EVENT_MASK, &mask);
  xcb_generic_error_t *error = xcb_request_check(connection, cookie);
  if(error) {
    std::cerr << "Another WM is already running\n";
    free(error);
    return 1;
  }

  xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(connection);
  xcb_keycode_t *keycode = xcb_key_symbols_get_keycode(keysyms, XK_Q);

  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT,
      *keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(keycode);
  xcb_key_symbols_free(keysyms);
  xcb_flush(connection);

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
      case XCB_KEY_PRESS: {
        xcb_key_press_event_t *e = (xcb_key_press_event_t*) generic_event;
        if(!clients.empty()) {
          close_window(connection, clients.back().window, wm_protocols, wm_delete_window);
        }
        break;
      }
      case XCB_UNMAP_NOTIFY: {
        xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t*) generic_event;
        int delete_idx = -1;
        for(int i = 0; i < (int)clients.size(); i++) {
          if(clients[i].window == e->window) {
            delete_idx = i;
            break;
          }
        }
        if(delete_idx != -1) {
          clients.erase(clients.begin()+delete_idx);
          arrange(connection, screen, clients);
          xcb_flush(connection);
        }
        break;
      }
    }
    free(generic_event);
  }
  xcb_disconnect(connection);
  return 0;
}
