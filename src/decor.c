#include "gallery.h"
#include "decor.h"

#include "stdint.h"
#include "swc.h"

// DEFS
const uint32_t plaque_border_color = 0xffa0314bu;

/*
 * Build a square frame from eight tiny pixel parts.
 *
 * Corners are drawn once.
 * Edges are tiled along their respective sides.
 */

#define PLAQUE_HEIGHT 32

static uint32_t plaque_bottom_pixels[PLAQUE_HEIGHT];
static uint32_t plaque_corner_pixels[4 * PLAQUE_HEIGHT];


static const struct swc_decor_part plaque_bottom =
{
  .width = 1,
  .height = PLAQUE_HEIGHT,
  .stride = sizeof(uint32_t),
  .data = plaque_bottom_pixels,
};

static const struct swc_decor_part plaque_corner =
{
  .width = 4,
  .height = PLAQUE_HEIGHT,
  .stride = 4 * sizeof(uint32_t),
  .data = plaque_corner_pixels,
};

#define PLAQUE_PIXEL_PART                 \
{                                         \
  .width = 1,                             \
  .height = 1,                            \
  .stride = sizeof(uint32_t),             \
  .data = &outer_border_color_inactive,   \
}

static const struct swc_decor_parts plaque_parts =
{
  .top_left     = PLAQUE_PIXEL_PART,
  .top          = PLAQUE_PIXEL_PART, //plaque_top,
  .top_right    = PLAQUE_PIXEL_PART,

  .left         = PLAQUE_PIXEL_PART,
  .right        = PLAQUE_PIXEL_PART,

  .bottom_left  = plaque_corner,
  .bottom       = plaque_bottom,
  .bottom_right = plaque_corner,
};

#undef PLAQUE_PIXEL_PART

struct config {
	struct swc_decor decor;
	uint32_t       gaps;
};

static const struct config cfg = {
	.decor = {
		.color = 0xff202020,
		.top = 0,
		.right = -196,
		.bottom = 32,
		.left = -196,
		.parts = &plaque_parts,
		.title =
		{
			.enabled = true,
			.edge = SWC_DECOR_EDGE_BOTTOM,
			.align = SWC_DECOR_ALIGN_CENTER,
			.color = 0xffffffff,
			.padding = 4,
			.font = "sun12x22:size=18", /*you should probably change this to a font you have*/
		},
	},
	.gaps = 0,
};


static const char* decorstring(struct swc_window *window)
{
	if (!window)
		return NULL;

	if (window->title && window->title[0])
		return window->title;

	if (window->app_id && window->app_id[0])
		return window->app_id;

	return NULL;
}

static void
set_plaque_border_color(uint32_t color)
{
  for (size_t i = 0; i < PLAQUE_HEIGHT; ++i)
    plaque_bottom_pixels[i] =
   (i < 4 || i  >= PLAQUE_HEIGHT - 4) ? color : 0;

 for (size_t i = 0; i < 4 * PLAQUE_HEIGHT; ++i)
  plaque_corner_pixels[i] = color;
}

void apply_decor(struct swc_window *window, bool active)
{
	const char* title;
	struct swc_decor decor;

	if (!window)
		return;

	/*if (window->fullscreen) {
		swc_window_set_decor(window, NULL);
		return;
	}*/

	decor = cfg.decor;
	//decor.parts = &plaque_parts;
	decor.title.color = 0xff202020; //active ? 0xffffffff : 0xffc0c0c0;
	decor.color = inner_border_color_inactive;

  set_plaque_border_color(outer_border_color_inactive);

  struct window *plaques;
  wl_list_for_each(plaques, &compositor.windows, link)
  {
    if (!plaques->swc)
      continue;

  	title = decorstring(plaques->swc);
  	if (title)
	  	decor.title.string = title;

  	swc_window_set_decor(plaques->swc, &decor);
  }
	swc_window_set_decor(window, NULL);
}
