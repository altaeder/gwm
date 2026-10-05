#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const uint32_t background_color = 0x00000000;

// ---------------------------------------------------------------
// ------------------ Inactive borders
// ---------------------------------------------------------------

// Blue Damp
/*
static const uint32_t outer_border_color_inactive = 0xff202020;
static const uint32_t inner_border_color_inactive = 0xffb3382d7;
*/

// Yellow Damp
/*
//static const uint32_t outer_border_color_inactive = 0xff202020;
static const uint32_t outer_border_color_inactive = 0xffe8894a;
static const uint32_t inner_border_color_inactive = 0xffb8894a;
*/

// Dark red

//static const uint32_t outer_border_color_inactive = 0xee792539;
//static const uint32_t inner_border_color_inactive = 0x90323232;


// Dunno?
/*
static const uint32_t inner_border_color_inactive = 0xff313131;
static const uint32_t inner_border_color_inactive = 0xff2f4858;
*/

// ---------------------------------------------------------------
// ------------------ Active borders -----------------------------
// ---------------------------------------------------------------

// darker red (depth)
/*
static const uint32_t outer_border_color_active = 0xffa0314b;
static const uint32_t inner_border_color_active = 0xff621e2e; 
// static const uint32_t inner_border_color_active = 0xff3286A4; // cyan
*/

// Bright Red
//static const uint32_t outer_border_color_active = 0xffa0314b;
//static const uint32_t inner_border_color_active = 0xff792539;
//static const uint32_t inner_border_color_active = 0x90792539;
//static const uint32_t inner_border_color_active = 0xff39839d;


static const uint32_t outer_border_width = 4;
static const uint32_t inner_border_width = 4;

//static const uint32_t select_box_color = 0xffa0314b;
static const uint32_t select_box_border = 4;

/* cursor themes:
 * - "swc"  : use swc's built-in cursor, client cursors allowed, no per-chord
 * cursor
 * - "nein" : use the plan 9 cursor set, client cursors blocked, per chord
 * cursors
 */
static const char *const cursor_theme = "nein";

/* recommended st-wl/hst (stock st-wl has some issues) or havoc
 * but anything will work just fine */
static const char *const select_term_app_id = "kitty";
//static const char *const select_term_app_id = "com.mitchellh.ghostty";
//static const char *const term = "foot";
static const char *const term = "kitty";

/* a flag for your terminal emulator to setup a windowid
 * - for st-wl: -w
 * - for havoc: -i
 * - for foot: -a
 * - for kitty: --class
 * - for everything else: idk
 */
static const char *const term_flag = "--class";
//static const char *const term_flag = "+new-window";

/* gui programs take over the geometry of the terminal, broken for xwayland */
static const bool enable_terminal_spawning = false;

/* define a list of terminals that you use */
static const char *const terminal_app_ids[] = {"com.mitchellh.ghostty", "kitty", "foot", "alacritty", NULL};

static const int chord_click_timeout_ms = 145;

static const int32_t move_scroll_edge_threshold = 112;
static const int32_t move_scroll_speed = 24;
static const float move_ease_factor = 0.30f;

static const int timerms = 16;

static const int scrollpx = 80;
static const int scrollease = 4;
static const int scrollcap = 64;

/* scroll chord mode:
 * - true  : drag mouse to scroll in any direction
 * - false : use scroll wheel for vertical scrolling only
 */
static const bool scroll_drag_mode = true;

/* enable zoom feature:
 * - when enabled: scroll wheel controls zoom when in drag scroll mode
 * broken for multiple monitors
 */
static const bool enable_zoom = false;

/* whether or not to center the window.
 * in drag mode, it centers on both axis
 * otherwise on the vertical axis
 */
static const bool center_focus = false;

/* customizable 2-1 chord
 * avaliable options:
 * - sticky: make window not move when scroll
 * - fullscreen: make a window take entire screen
 * - jump: switch focus to the closest window
 */
static const char *const custom_chord = "fullscreen";

#endif
