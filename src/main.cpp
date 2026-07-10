#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
#include <set>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <sys/wait.h>

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_util.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>

const int BORDER = 2;
int diff_percent = 0;

struct Client {
  xcb_window_t window;
  int x, y, w, h;
  int tag;
};

void arrange(xcb_connection_t *connection, xcb_screen_t *screen, std::vector<Client> &clients, int &current_workspace) {
  std::vector<Client> cur_clients;
  for(auto c: clients) if(c.tag == current_workspace) cur_clients.push_back(c);
  if(cur_clients.empty()) return;
  
  if((int)cur_clients.size() == 1) {
    if(cur_clients.empty()) return;
    Client &c = cur_clients.back();

    c.x = 0, c.y = 0;
    c.h = screen->height_in_pixels-2*BORDER;
    c.w = screen->width_in_pixels-2*BORDER;

    int vals[4] = {c.x, c.y, c.w, c.h};
    int mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    xcb_configure_window(connection, c.window, mask, vals);
  } else {
    Client& master = cur_clients[0];

    int dif = diff_percent*screen->width_in_pixels/200;

    master.x = 0;
    master.y = 0;
    master.w = screen->width_in_pixels/2 - BORDER + dif;
    master.h = screen->height_in_pixels - 2*BORDER;

    int mvals[4] = {master.x, master.y, master.w, master.h};
    int mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    xcb_configure_window(connection, master.window, mask, mvals);

    std::vector<Client> ri;
    for(int i = 1; i < (int)cur_clients.size(); i++) ri.push_back(cur_clients[i]);
    int common_height = (screen->height_in_pixels - 2*BORDER*(int)ri.size())/(int)ri.size();
    int cur_y = 0;
    for(auto& c: ri) {
      c.x = master.w + BORDER;
      c.w = screen->width_in_pixels - master.w - 2*BORDER;

      //c.x = screen->width_in_pixels/2 + BORDER;
      //c.w = master.w - 2*BORDER;
      c.y = cur_y;
      c.h = common_height;
      cur_y += common_height+2*BORDER;
      int vals[4] = {c.x, c.y, c.w, c.h};
      int mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
      xcb_configure_window(connection, c.window, mask, vals);
    }
  }
  xcb_flush(connection);
}

void spawn(const char* cmd) {
  if(fork() == 0) {
    setsid();
    execlp(cmd, cmd, NULL);
    _exit(1);
  }
}

void spawn_args(const char *file, char *const argv[]) {
  if(fork() == 0) {
    setsid();
    execvp(file, argv);
    _exit(1);
  }
}

bool rebuild(const char *project_dir) {
  pid_t pid = fork();
  if(pid == 0) {
    chdir(project_dir);
    execlp("make", "make", NULL);
    _exit(1);
  }

  int status;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
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

void focus_window(xcb_connection_t *connection, xcb_window_t &focused_window, xcb_window_t& window) {
  if(focused_window == window) return;
  if(focused_window != XCB_NONE) {
    int unfocused_col = 0x444444;
    xcb_change_window_attributes(connection, focused_window, XCB_CW_BORDER_PIXEL, &unfocused_col);
  }

  xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, window, XCB_CURRENT_TIME);

  int focused_col = 0x3370E8;
  xcb_change_window_attributes(connection, window, XCB_CW_BORDER_PIXEL, &focused_col);

  focused_window = window;
  xcb_flush(connection);
}

void switch_workspace(xcb_connection_t *connection, std::vector<Client>&clients, 
    int &current_workspace, int workspace, std::set<xcb_window_t> &unmaps) {
  if(current_workspace == workspace) return;
  for(auto c: clients) {
    if(c.tag == current_workspace) {
      unmaps.insert(c.window);
      xcb_unmap_window(connection, c.window);
    }
    else if(c.tag == workspace) xcb_map_window(connection, c.window);
  }
  current_workspace = workspace;
  xcb_flush(connection);
}

void move_to_workspace(xcb_connection_t *connection, Client &c, 
    int workspace, std::set<xcb_window_t>&unmaps) {
  if(workspace == c.tag) return;

  unmaps.insert(c.window);
  xcb_unmap_window(connection, c.window);
  c.tag = workspace;
}

int main(int argc, char *argv[]) {
  int screen_number;
  bool is_running = true;
  char *wm_path = argv[0];

  xcb_connection_t *connection = xcb_connect(NULL, &screen_number);
  assert(!xcb_connection_has_error(connection));
  xcb_screen_t *screen = xcb_aux_get_screen(connection, screen_number);

  signal(SIGCHLD, SIG_IGN);

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

  xcb_keycode_t *keycode_q = xcb_key_symbols_get_keycode(keysyms, XK_Q);
  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT,
      *keycode_q, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(keycode_q);

  xcb_keycode_t *keycode_return = xcb_key_symbols_get_keycode(keysyms, XK_Return);
  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_4,
      *keycode_return, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(keycode_return);

  xcb_keycode_t *keycode_y = xcb_key_symbols_get_keycode(keysyms, XK_Y);
  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_4,
      *keycode_y, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(keycode_y);

  xcb_keycode_t *keycode_ysh = xcb_key_symbols_get_keycode(keysyms, XK_Y);
  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT,
      *keycode_ysh, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(keycode_ysh);

  xcb_keycode_t *keycode_d = xcb_key_symbols_get_keycode(keysyms, XK_D);
  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_4,
      *keycode_d, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(keycode_d);

  xcb_keycode_t *keycode_h = xcb_key_symbols_get_keycode(keysyms, XK_H);
  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_4,
      *keycode_h, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(keycode_h);

  xcb_keycode_t *keycode_l = xcb_key_symbols_get_keycode(keysyms, XK_L);
  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_4,
      *keycode_l, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(keycode_l);

  for(int i = 1; i <= 9; i++) {
    xcb_keycode_t *kc = xcb_key_symbols_get_keycode(keysyms, XK_1 + i - 1);
    xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_4, *kc,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT, *kc,
        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    free(kc);
  }

  xcb_keycode_t *kc_vol_up = xcb_key_symbols_get_keycode(keysyms, XF86XK_AudioRaiseVolume);
  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_ANY, *kc_vol_up,
      XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(kc_vol_up);

  xcb_keycode_t *kc_vol_down = xcb_key_symbols_get_keycode(keysyms, XF86XK_AudioLowerVolume);
  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_ANY, *kc_vol_down,
      XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(kc_vol_down);

  xcb_keycode_t *kc_mute = xcb_key_symbols_get_keycode(keysyms, XF86XK_AudioMute);
  xcb_grab_key(connection, 1, screen->root, XCB_MOD_MASK_ANY, *kc_mute,
      XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
  free(kc_mute);

  xcb_key_symbols_free(keysyms);
  xcb_flush(connection);

  std::vector<Client> clients;
  xcb_window_t focused_window = XCB_NONE;
  int current_workspace = 1;
  std::set<xcb_window_t> unmaps;

  xcb_query_tree_cookie_t qt_cookie = xcb_query_tree(connection, screen->root);
  xcb_query_tree_reply_t *qt_reply = xcb_query_tree_reply(connection, qt_cookie, NULL);

  if(qt_reply) {
    xcb_window_t *existing = xcb_query_tree_children(qt_reply);
    int n = xcb_query_tree_children_length(qt_reply);

    for(int i = 0; i < n; i++) {
      xcb_get_window_attributes_cookie_t attr_cookie = xcb_get_window_attributes(connection, existing[i]);
      xcb_get_window_attributes_reply_t *attr = xcb_get_window_attributes_reply(connection, attr_cookie, NULL);

      if(attr && attr->map_state == XCB_MAP_STATE_VIEWABLE && !attr->override_redirect) {
        uint32_t enter_mask = XCB_EVENT_MASK_ENTER_WINDOW;
        xcb_change_window_attributes(connection, existing[i], XCB_CW_EVENT_MASK, &enter_mask);

        Client c;
        c.window = existing[i];
        c.tag = current_workspace;
        clients.push_back(c);

        xcb_configure_window(connection, existing[i], XCB_CONFIG_WINDOW_BORDER_WIDTH, &BORDER);
      }
      free(attr);
    }
    free(qt_reply);
  }

  arrange(connection, screen, clients, current_workspace);
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
      
      case XCB_MAP_REQUEST: {
        xcb_map_request_event_t *e = (xcb_map_request_event_t*)  generic_event;

        uint32_t enter_mask = XCB_EVENT_MASK_ENTER_WINDOW;
        xcb_change_window_attributes(connection, e->window, XCB_CW_EVENT_MASK, &enter_mask);
        
        Client c;
        c.window = e->window;
        c.tag = current_workspace;

        clients.push_back(c);

        xcb_configure_window(connection, e->window, XCB_CONFIG_WINDOW_BORDER_WIDTH, &BORDER);

        arrange(connection, screen, clients, current_workspace);
        xcb_map_window(connection, e->window);
        xcb_flush(connection);

        focus_window(connection, focused_window, e->window);

        break;
      }

      case XCB_KEY_PRESS: {
        xcb_key_press_event_t *e = (xcb_key_press_event_t*) generic_event;
        
        xcb_key_symbols_t *ks = xcb_key_symbols_alloc(connection);
        xcb_keysym_t sym = xcb_key_symbols_get_keysym(ks, e->detail, 0);
        xcb_key_symbols_free(ks);
        
        if(sym == XK_q && (e->state & XCB_MOD_MASK_SHIFT) && focused_window !=  XCB_NONE) {
          close_window(connection, focused_window, wm_protocols, wm_delete_window);
        }
        else if(sym == XK_Return) {
          spawn("alacritty");
        }
        else if(sym == XK_y && !(e->state &XCB_MOD_MASK_SHIFT)) {
          is_running = false;
        }
        else if(sym == XK_y && (e->state &XCB_MOD_MASK_SHIFT)) {
          if(rebuild("/home/jonathan/fun/javelin")) {
            xcb_disconnect(connection);
            execlp(wm_path, wm_path, NULL);
            perror("execlp failed to restart wm");
            exit(1);
          } else std::cerr << "Build failed, keeping current WM running\n";
          break;
        }
        else if(sym == XK_d) {
          spawn("dmenu_run");
        }
        else if(sym  >= XK_1 && sym <= XK_9 && !(e->state &XCB_MOD_MASK_SHIFT)) {
          switch_workspace(connection, clients, current_workspace, (int)sym-XK_1+1, unmaps);
          arrange(connection, screen, clients, current_workspace);
        } 
        else if(sym >= XK_1 && sym <= XK_9 && (e->state & XCB_MOD_MASK_SHIFT)) {
          for(auto &c: clients) if(c.window == focused_window) {
            move_to_workspace(connection, c, sym-XK_1+1, unmaps);
            switch_workspace(connection, clients, current_workspace, (int)sym-XK_1+1, unmaps);
            break;
          }
          arrange(connection, screen, clients, current_workspace);
        }
        else if(sym == XK_h) {
          if(diff_percent <= -40) break;
          
          int cnt_windows = 0;
          for(auto c: clients) if(c.tag == current_workspace) cnt_windows++;
          if(cnt_windows <= 1) break;

          diff_percent -= 5;
          std::cout << diff_percent << '\n';
          arrange(connection, screen, clients, current_workspace);
        } 
        else if(sym == XK_l) {
          if(diff_percent >= 40) break;
          
          int cnt_windows = 0;
          for(auto c: clients) if(c.tag == current_workspace) cnt_windows++;
          if(cnt_windows <= 1) break;

          diff_percent += 5;
          arrange(connection, screen, clients, current_workspace);
        }
        else if(sym == XF86XK_AudioRaiseVolume) {
          char *argv[] = {(char*)"pactl", (char*)"set-sink-volume", (char*)"@DEFAULT_SINK@", (char*)"+5%", NULL};
          spawn_args("pactl", argv);
        }
        else if(sym == XF86XK_AudioLowerVolume) {
          char *argv[] = {(char*)"pactl", (char*)"set-sink-volume", (char*)"@DEFAULT_SINK@", (char*)"-5%", NULL};
          spawn_args("pactl", argv);
        }
        else if(sym == XF86XK_AudioMute) {
          char *argv[] = {(char*)"pactl", (char*)"set-sink-mute", (char*)"@DEFAULT_SINK@", (char*)"toggle", NULL};
          spawn_args("pactl", argv);
        }

        break;
      }

      // delete a window
      case XCB_UNMAP_NOTIFY: {
        xcb_unmap_notify_event_t *e = (xcb_unmap_notify_event_t*) generic_event;

        if(unmaps.find(e->window) != unmaps.end()) {
          unmaps.erase(unmaps.find(e->window));
          break;
        }

        int delete_idx = -1;
        for(int i = 0; i < (int)clients.size(); i++) {
          if(clients[i].window == e->window) {
            delete_idx = i;
            break;
          }
        }
        if(delete_idx != -1) {
          clients.erase(clients.begin()+delete_idx);

          if(focused_window == e->window) {
            focused_window = XCB_NONE;
            xcb_window_t foc_w = XCB_NONE;
            for(auto& c: clients) if(c.tag == current_workspace) {
              foc_w = c.window;
              break;
            }
            focus_window(connection, focused_window, foc_w);
          }

          arrange(connection, screen, clients, current_workspace);
          xcb_flush(connection);
        }
        break;
      }

      // focus window
      case XCB_ENTER_NOTIFY: {
        xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t*) generic_event;
        focus_window(connection, focused_window, e->event);
        break;
      }

      case XCB_CONFIGURE_REQUEST: {
        xcb_configure_request_event_t *e = (xcb_configure_request_event_t*) generic_event;

        int values[7];
        int i = 0;
        int mask = 0;

        if(e->value_mask & XCB_CONFIG_WINDOW_X) { mask |= XCB_CONFIG_WINDOW_X; values[i++] = e->x; }
        if(e->value_mask & XCB_CONFIG_WINDOW_Y) { mask |= XCB_CONFIG_WINDOW_Y; values[i++] = e->y; }
        if(e->value_mask & XCB_CONFIG_WINDOW_WIDTH) { mask |= XCB_CONFIG_WINDOW_WIDTH; values[i++] = e->width; }
        if(e->value_mask & XCB_CONFIG_WINDOW_HEIGHT) { mask |= XCB_CONFIG_WINDOW_HEIGHT; values[i++] = e->height; }
        if(e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) { mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH; values[i++] = e->border_width; }
        if(e->value_mask & XCB_CONFIG_WINDOW_SIBLING) { mask |= XCB_CONFIG_WINDOW_SIBLING; values[i++] = e->sibling; }
        if(e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) { mask |= XCB_CONFIG_WINDOW_STACK_MODE; values[i++] = e->stack_mode; }

        xcb_configure_window(connection, e->window, mask, values);
        xcb_flush(connection);
        break;
      }
    }

  }

  xcb_disconnect(connection);
  return 0;
}
