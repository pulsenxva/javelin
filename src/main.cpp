#include <xcb/xcb.h>
#include <cassert>
#include <xcb/xcb_util.h>
#include <xcb/xproto.h>

int main() {
  int screen_number;
  xcb_connection_t *connection = xcb_connect(NULL, &screen_number);
  assert(xcb_connection_has_error(connection));

  xcb_screen_t *screen = xcb_aux_get_screen(connection, screen_number);

  xcb_window_t window = xcb_generate_id(connection);
  xcb_create_window(connection, screen->root_depth, window, screen->root, 0, 0, 720, 400, 0, 
      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_EVENT_MASK_NO_EVENT, NULL);
  
  xcb_map_window(connection, window);
  assert(xcb_flush(connection));

  //const char *title = "QWE";
  //xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, 8, title);

  while(1);

  xcb_disconnect(connection);
  return 0;
}
