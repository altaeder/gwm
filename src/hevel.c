#include "hevel.h"
#include "ipc.h"   // near the other includes
#include "control.h"
#include <unistd.h>
#include <stdlib.h>

struct compositor_state compositor = {0};
struct input_state input = {0};
struct chord_state chord = {0};
struct scroll_state scroll = {0};
struct zoom_state zoom = {0};
struct sel_state sel = {0};

// Border Colors
uint32_t outer_border_color_active = 0xffa0314b;
uint32_t inner_border_color_active = 0xff792539;
uint32_t outer_border_color_inactive = 0xee792539;
uint32_t inner_border_color_inactive = 0x90323232;

uint32_t select_box_color = 0xffa0314b;
uint32_t og_select_color = 0xffa0314b;

uint32_t og_border_act_out = 0xffa0314b;
uint32_t og_border_act_in = 0xff792539;
uint32_t og_border_inact_out = 0xee792539;
uint32_t og_border_inact_in = 0x90323232;

/* TODO: clear this up
 * it does this because we modify this value from config
 * so it needs to be copied over, too lazy to change the name now */
bool focus_center = center_focus;

static void
maybe_enable_nein_cursor_theme(void)
{
  const struct nein_cursor_meta *arrow =
      &nein_cursor_metadata[NEIN_CURSOR_WHITEARROW];
  const struct nein_cursor_meta *box =
      &nein_cursor_metadata[NEIN_CURSOR_BOXCURSOR];
  const struct nein_cursor_meta *cross =
      &nein_cursor_metadata[NEIN_CURSOR_CROSSCURSOR];
  const struct nein_cursor_meta *sight =
      &nein_cursor_metadata[NEIN_CURSOR_SIGHTCURSOR];
  const struct nein_cursor_meta *up = &nein_cursor_metadata[NEIN_CURSOR_T];
  const struct nein_cursor_meta *down = &nein_cursor_metadata[NEIN_CURSOR_B];

  if (!cursor_theme || strcmp(cursor_theme, "nein") != 0) return;

  swc_set_cursor_mode(SWC_CURSOR_MODE_COMPOSITOR);
  swc_set_cursor_image(SWC_CURSOR_DEFAULT, &nein_cursor_data[arrow->offset],
                       arrow->width, arrow->height, arrow->hotspot_x,
                       arrow->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_BOX, &nein_cursor_data[box->offset],
                       box->width, box->height, box->hotspot_x, box->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_CROSS, &nein_cursor_data[cross->offset],
                       cross->width, cross->height, cross->hotspot_x,
                       cross->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_SIGHT, &nein_cursor_data[sight->offset],
                       sight->width, sight->height, sight->hotspot_x,
                       sight->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_UP, &null_cursor_data[up->offset], up->width, // Ω NEIN set to NULL Ω
                       up->height, up->hotspot_x, up->hotspot_y);
  swc_set_cursor_image(SWC_CURSOR_DOWN, &null_cursor_data[down->offset],
                       down->width, down->height, down->hotspot_x,
                       down->hotspot_y);

  update_mode_cursor();
}

static void
newdevice(struct libinput_device *dev)
{
  (void)dev;
}

static const struct swc_manager manager = {
    .new_screen = newscreen,
    .new_window = newwindow,
    .new_device = newdevice,
};



// ----------------------------------- Hotkey functions
static void
quit(void *data, uint32_t time, uint32_t value, uint32_t state)
{
  (void)data;
  (void)time;
  (void)value;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
  {
    return;
  }
  //wl_display_terminate(compositor.display);
  swc_finalize();
}

static void
center(void *data, uint32_t time, uint32_t value, uint32_t state)
{
  (void)data;
  (void)time;
  (void)value;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
  {
    return;
  }

  if (compositor.focused) center_window(compositor.focused);
}



static void
switchWindow(void *data, uint32_t time, uint32_t value, uint32_t state)
{
  (void)data;
  (void)time;
  (void)value;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
  {
    return;
  }

  //if (compositor.focused)
  switch_window();
}


static void
kill_window(void *data, uint32_t time, uint32_t value, uint32_t state)
{
  (void)data;
  (void)time;
  (void)value;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
  {
    return;
  }
  /*int32_t x, y;
  if (cursor_position(&x, &y))
  {
    struct swc_window *target = swc_window_at(x, y);
    if (target) swc_window_close(target);
  }*/
  if (compositor.focused) swc_window_close(compositor.focused);
}

/////////////////////////////
////////// COMMANDS /////////
/////////////////////////////

// Drun-likes
static const char *appLaunch[] = {"qs", "ipc", "call", "launcher", "toggle", NULL};
static const char *runMenu[] = {"/home/tutter/GalleryShell/rofi", NULL};

// System
static const char *windowMap[] = {"qs", "ipc", "call", "system", "map", NULL};
static const char *powerMenu[] = {"qs", "ipc", "call", "system", "powermenu", NULL};
static const char *reloadQS[] = {"qs", "kill", "&", "wait", "&&", "qs", "-d", NULL};
static const char *brightUp[] = {"brightnessctl", "s", "5%+", NULL};
static const char *brightDown[] = {"brightnessctl", "s", "5%-", NULL};
static const char *soundUp[] = {"wpctl", "set-volume", "@DEFAULT_SINK@", "3.3%+", "--limit", "1.0", NULL};
static const char *soundUpSmall[] = {"wpctl", "set-volume", "@DEFAULT_SINK@", "1%+", "--limit", "1.0", NULL};
static const char *soundDown[] = {"wpctl", "set-volume", "@DEFAULT_SINK@", "3.3%-", NULL};
static const char *soundDownSmall[] = {"wpctl", "set-volume", "@DEFAULT_SINK@", "1%-", NULL};
static const char *soundMute[] = {"wpctl", "set-mute", "@DEFAULT_SINK@", "toggle", NULL};
static const char *playPause[] = {"playerctl", "play-pause", NULL};
static const char *screenshot[] = {"/home/tutter/GalleryShell/Tools/screenshot", NULL};
static const char *caps[] = {"/home/tutter/GalleryShell/Tools/caps", NULL};

// Wallpapers
static const char *switchBgPaint[] = {"/home/tutter/GalleryShell/wawarandom", "-p", NULL};
static const char *switchBgAscii[] = {"/home/tutter/GalleryShell/wawarandom", "-a", NULL};
static const char *switchBgTile[] = {"/home/tutter/GalleryShell/wawarandom", "-t", NULL};
static const char *wallpaperGen[] = {"/home/tutter/GalleryShell/wallgen", NULL};

// Misc
static const char *sign[] = {"/home/tutter/GalleryShell/sign", NULL};
static const char *saveWallGen[] = {"/home/tutter/GalleryShell/wall", NULL};

// Autostarts
static const char *quickshell[] = {"qs", "-d", NULL};

static void
command(void *data, uint32_t time, uint32_t value, uint32_t state)
{
  char *const *run = data;
  (void)time;
  (void)value;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
    return;
  }

  if (fork() == 0)
  {
    execvp(run[0], run);
    exit(EXIT_FAILURE);
  }
}

// ------------------------------------

static void
sig(int s)
{
  (void)s;
  wl_display_terminate(compositor.display);
}

int
main(void)
{
  struct wl_event_loop *evloop;
  const char *sock;

  wl_list_init(&compositor.windows);
  wl_list_init(&compositor.screens);

  compositor.current_screen = NULL;
  compositor.display = wl_display_create();
  if (!compositor.display) {
    fprintf(stderr, "cannot create display\n");
    return 1;
  }

  evloop = wl_display_get_event_loop(compositor.display);
  compositor.evloop = evloop;

  if (!swc_initialize(compositor.display, evloop, &manager)) {
    fprintf(stderr, "cannot initialize swc\n");
    return 1;
  }

  ipc_init(evloop);   // right after swc_initialize succeeds, before the main loop
  control_init(evloop);

  maybe_enable_nein_cursor_theme();

  swc_set_gesture_handler(handle_gesture, NULL);

  // HOTKEYYYS!!!----------------------------------------------------------------
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO | SWC_MOD_SHIFT, XKB_KEY_q,
                  quit, NULL);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_Return,
                  center, NULL);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_w,
                  kill_window, NULL);
  //swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_Tab,
  //                switchWindow, NULL);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_Tab,
                  &command, windowMap);
  // Druns
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_XF86Favorites,
                  &command, appLaunch);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_XF86NotificationCenter,
                  &command, runMenu);

  // SYS-keys
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_Escape,
                  &command, powerMenu);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_r,
                  &command, reloadQS);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_XF86MonBrightnessUp,
                  &command, brightUp);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_XF86MonBrightnessDown,
                  &command, brightDown);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_SHIFT, XKB_KEY_XF86AudioRaiseVolume,
                  &command, soundUpSmall);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_XF86AudioRaiseVolume,
                  &command, soundUp);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_SHIFT, XKB_KEY_XF86AudioLowerVolume,
                  &command, soundDownSmall);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_XF86AudioLowerVolume,
                  &command, soundDown);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_XF86AudioMute,
                  &command, soundMute);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_XF86AudioMicMute,
                  &command, playPause);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_Print,
                  &command, screenshot);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_Caps_Lock,
                  &command, caps);

  // Wallpapers
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_SHIFT, XKB_KEY_XF86Display,
                  &command, switchBgPaint);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ALT, XKB_KEY_XF86Display,
                  &command, switchBgTile);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_XF86Display,
                  &command, wallpaperGen);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_ANY, XKB_KEY_XF86Display,
                  &command, switchBgAscii);
  // Misc
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO, XKB_KEY_n,
                  &command, sign);
  swc_add_binding(SWC_BINDING_KEY, SWC_MOD_LOGO | SWC_MOD_SHIFT, XKB_KEY_s,
                  &command, saveWallGen);

  // -------------------------------------------------------------------------------
  /* we can bind mouse buttons using SWC_MOD_ANY */
  swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_LEFT, button, NULL);
  swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_MIDDLE, button, NULL);
  swc_add_binding(SWC_BINDING_BUTTON, SWC_MOD_ANY, BTN_RIGHT, button, NULL);
  if (swc_add_axis_binding(SWC_MOD_ANY, 0, axis, NULL) < 0)
    fprintf(stderr, "cannot bind vertical scroll axis\n");
  if (swc_add_axis_binding(SWC_MOD_ANY, 1, axis, NULL) < 0)
    fprintf(stderr, "cannot bind horizontal scroll axis\n");

  sock = wl_display_add_socket_auto(compositor.display);
  if (!sock) {
    fprintf(stderr, "cannot add socket\n");
    return 1;
  }

  printf("%s\n", sock);
  setenv("WAYLAND_DISPLAY", sock, 1);

  signal(SIGTERM, sig);
  signal(SIGINT, sig);


  char* qsArgs[] = {"qs", "-d", NULL};

  if (fork() == 0)
  {
    execvp("qs", qsArgs);
  }

  if (fork() == 0)
  {
    execvp("/home/tutter/hevel-start", NULL);
  }

  wl_display_run(compositor.display);

  swc_finalize();
  wl_display_destroy(compositor.display);

  return 0;
}
