// i_video_sdl.c - SDL3 video and input for linuxdoom-1.10
//
// Replaces id's X11 i_video.c. 320x200 8-bit screens[0] -> ARGB texture,
// shown at 4:3 with nearest-neighbour scaling; on DOS (or with -swsurface)
// straight into an 8-bit window surface instead.
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 2026 psdoom-plus contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "doomstat.h"
#include "i_system.h"
#include "i_video.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"
#include "doomdef.h"

#include "i_platform.h"

static SDL_Window   *window;
static SDL_Renderer *renderer;
static SDL_Texture  *texture;
static Uint32        argb[256];
static boolean       grabmouse;
static int           mousebuttons;

// 8-bit window-surface path: the default on DOS (VGA mode 13h, so a frame
// is a 64000-byte copy and a palette load), -swsurface elsewhere.
static boolean       use_surface;
static SDL_Surface  *shadow8;        // screens[0] as an INDEX8 surface
static SDL_Color     colors[256];

static void InitSurfaceVideo(void)
{
	SDL_DisplayMode **modes;
	int i, n = 0;
	// -640: 640x480 8-bit (VESA, 60 Hz) instead of mode 13h's 320x200 at
	// 70 Hz, which some capture devices cannot lock to; the frame is
	// scaled 2x2.4 (Doom's pixels were never square anyway)
	int w = M_CheckParm("-640") ? 640 : SCREENWIDTH;
	int h = M_CheckParm("-640") ? 480 : SCREENHEIGHT;

	window = SDL_CreateWindow("psdoom-plus", w, h,
	                          M_CheckParm("-window") ? 0 : SDL_WINDOW_FULLSCREEN);
	if (window == NULL)
		I_Error("I_InitGraphics: %s", SDL_GetError());
	// exactly WxH: 8-bit first, then any depth (the blit converts). Never
	// SDL's closest match: on a Mach64 under UniVBE that picked 512x384,
	// which capture devices cannot lock to (dossage, 2026-09-03).
	modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &n);
	{
		const SDL_DisplayMode *pick = NULL;

		for (i = 0; modes && i < n && !pick; i++)
			if (modes[i]->w == w && modes[i]->h == h &&
			    modes[i]->format == SDL_PIXELFORMAT_INDEX8)
				pick = modes[i];
		for (i = 0; modes && i < n && !pick; i++)
			if (modes[i]->w == w && modes[i]->h == h)
				pick = modes[i];
		if (pick)
			SDL_SetWindowFullscreenMode(window, pick);
		else
			printf("I_InitGraphics: no %dx%d mode offered\n", w, h);
	}
	SDL_free(modes);
	SDL_SyncWindow(window);
	{
		// say what we got: a black capture must be told apart from a hang
		const SDL_DisplayMode *cur = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());

		if (cur)
			printf("I_InitGraphics: %dx%d %d bpp\n", cur->w, cur->h,
			       SDL_BITSPERPIXEL(cur->format));
	}
	shadow8 = SDL_CreateSurfaceFrom(SCREENWIDTH, SCREENHEIGHT, SDL_PIXELFORMAT_INDEX8,
	                                screens[0], SCREENWIDTH);
	if (shadow8 == NULL || SDL_CreateSurfacePalette(shadow8) == NULL ||
	    SDL_GetWindowSurface(window) == NULL)
		I_Error("I_InitGraphics: %s", SDL_GetError());
}

static void FinishSurfaceUpdate(void)
{
	SDL_Surface *ws = SDL_GetWindowSurface(window);
	int y;

	if (ws == NULL)
		return;
	if (ws->format == SDL_PIXELFORMAT_INDEX8 &&
	    SDL_GetSurfacePalette(ws) != SDL_GetSurfacePalette(shadow8))
		// SDL gives the window surface no palette; share ours, so that
		// I_SetPalette reaches the VGA DAC (SDL3-DOS programs it from here)
		SDL_SetSurfacePalette(ws, SDL_GetSurfacePalette(shadow8));
	if (ws->format == SDL_PIXELFORMAT_INDEX8 && ws->w == SCREENWIDTH &&
	    ws->h == SCREENHEIGHT) {
		// same size and depth: rows straight into the framebuffer
		if (SDL_MUSTLOCK(ws) && !SDL_LockSurface(ws))
			return;
		for (y = 0; y < SCREENHEIGHT; y++)
			memcpy((Uint8 *)ws->pixels + y * ws->pitch,
			       screens[0] + y * SCREENWIDTH, SCREENWIDTH);
		if (SDL_MUSTLOCK(ws))
			SDL_UnlockSurface(ws);
	} else {
		SDL_BlitSurfaceScaled(shadow8, NULL, ws, NULL, SDL_SCALEMODE_NEAREST);
	}
	SDL_UpdateWindowSurface(window);
}

void I_InitGraphics(void)
{
	static boolean done;
	int scale = 2;
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;

	if (done)
		return;
	done = true;

	if (M_CheckParm("-1")) scale = 1;
	if (M_CheckParm("-2")) scale = 2;
	if (M_CheckParm("-3")) scale = 3;
	if (M_CheckParm("-4")) scale = 4;
	if (M_CheckParm("-fullscreen"))
		flags |= SDL_WINDOW_FULLSCREEN;
	grabmouse = M_CheckParm("-grabmouse") != 0;
#ifdef __DJGPP__
	use_surface = true;
	grabmouse = !M_CheckParm("-nomouse");   // DOS: fullscreen, mouse is ours
#else
	use_surface = M_CheckParm("-swsurface") != 0;
#endif

	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
		I_Error("I_InitGraphics: %s", SDL_GetError());

	if (use_surface) {
		InitSurfaceVideo();
		if (grabmouse)
			SDL_SetWindowRelativeMouseMode(window, true);
		return;
	}

	window = SDL_CreateWindow("psdoom-plus", SCREENWIDTH * scale,
	                          SCREENHEIGHT * scale * 6 / 5, flags);
	if (window == NULL)
		I_Error("I_InitGraphics: %s", SDL_GetError());
	renderer = SDL_CreateRenderer(window, NULL);
	if (renderer == NULL)
		I_Error("I_InitGraphics: %s", SDL_GetError());
	SDL_SetRenderVSync(renderer, 1);
	// Doom's 320x200 was shown on 4:3 screens: present it as 320x240
	SDL_SetRenderLogicalPresentation(renderer, SCREENWIDTH, SCREENHEIGHT * 6 / 5,
	                                 SDL_LOGICAL_PRESENTATION_LETTERBOX);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
	                            SDL_TEXTUREACCESS_STREAMING, SCREENWIDTH, SCREENHEIGHT);
	if (texture == NULL)
		I_Error("I_InitGraphics: %s", SDL_GetError());
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

	if (grabmouse)
		SDL_SetWindowRelativeMouseMode(window, true);
}

void I_ShutdownGraphics(void)
{
	if (shadow8)  SDL_DestroySurface(shadow8);
	shadow8 = NULL;
	if (texture)  SDL_DestroyTexture(texture);
	if (renderer) SDL_DestroyRenderer(renderer);
	if (window)   SDL_DestroyWindow(window);
	texture = NULL;
	renderer = NULL;
	window = NULL;
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void I_SetPalette(byte *palette)
{
	int i;

	for (i = 0; i < 256; i++) {
		Uint32 r = gammatable[usegamma][*palette++];
		Uint32 g = gammatable[usegamma][*palette++];
		Uint32 b = gammatable[usegamma][*palette++];
		argb[i] = 0xff000000u | (r << 16) | (g << 8) | b;
		colors[i].r = (Uint8)r;
		colors[i].g = (Uint8)g;
		colors[i].b = (Uint8)b;
		colors[i].a = 255;
	}
	if (use_surface && shadow8)
		SDL_SetPaletteColors(SDL_GetSurfacePalette(shadow8), colors, 0, 256);
}

void I_UpdateNoBlit(void)
{
}

void I_FinishUpdate(void)
{
	void *pixels;
	int pitch, x, y;
	const byte *src = screens[0];

	if (use_surface) {
		FinishSurfaceUpdate();
		return;
	}
	if (texture == NULL)
		return;
	if (!SDL_LockTexture(texture, NULL, &pixels, &pitch))
		return;
	for (y = 0; y < SCREENHEIGHT; y++) {
		Uint32 *dst = (Uint32 *)((Uint8 *)pixels + y * pitch);
		for (x = 0; x < SCREENWIDTH; x++)
			dst[x] = argb[*src++];
	}
	SDL_UnlockTexture(texture);
	SDL_RenderClear(renderer);
	SDL_RenderTexture(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

void I_ReadScreen(byte *scr)
{
	memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

void I_StartFrame(void)
{
}

static int translate_key(SDL_Keycode k)
{
	switch (k) {
	case SDLK_LEFT:      return KEY_LEFTARROW;
	case SDLK_RIGHT:     return KEY_RIGHTARROW;
	case SDLK_UP:        return KEY_UPARROW;
	case SDLK_DOWN:      return KEY_DOWNARROW;
	case SDLK_ESCAPE:    return KEY_ESCAPE;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:  return KEY_ENTER;
	case SDLK_TAB:       return KEY_TAB;
	case SDLK_F1:        return KEY_F1;
	case SDLK_F2:        return KEY_F2;
	case SDLK_F3:        return KEY_F3;
	case SDLK_F4:        return KEY_F4;
	case SDLK_F5:        return KEY_F5;
	case SDLK_F6:        return KEY_F6;
	case SDLK_F7:        return KEY_F7;
	case SDLK_F8:        return KEY_F8;
	case SDLK_F9:        return KEY_F9;
	case SDLK_F10:       return KEY_F10;
	case SDLK_F11:       return KEY_F11;
	case SDLK_F12:       return KEY_F12;
	case SDLK_BACKSPACE:
	case SDLK_DELETE:    return KEY_BACKSPACE;
	case SDLK_PAUSE:     return KEY_PAUSE;
	case SDLK_EQUALS:
	case SDLK_KP_PLUS:   return KEY_EQUALS;
	case SDLK_MINUS:
	case SDLK_KP_MINUS:  return KEY_MINUS;
	case SDLK_LSHIFT:
	case SDLK_RSHIFT:    return KEY_RSHIFT;
	case SDLK_LCTRL:
	case SDLK_RCTRL:     return KEY_RCTRL;
	case SDLK_LALT:
	case SDLK_RALT:
	case SDLK_LGUI:
	case SDLK_RGUI:      return KEY_RALT;
	default:
		if (k >= 32 && k < 127)
			return (int)k;   // SDL keycodes for printable keys are lowercase ASCII
		return 0;
	}
}

static int buttons_of(SDL_MouseButtonFlags b)
{
	return ((b & SDL_BUTTON_LMASK) ? 1 : 0) |
	       ((b & SDL_BUTTON_RMASK) ? 2 : 0) |
	       ((b & SDL_BUTTON_MMASK) ? 4 : 0);
}

void I_StartTic(void)
{
	SDL_Event e;
	event_t ev;

	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_EVENT_QUIT:
			I_Quit();
			break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			if (e.key.repeat)
				break;
			ev.type = e.type == SDL_EVENT_KEY_DOWN ? ev_keydown : ev_keyup;
			ev.data1 = translate_key(e.key.key);
			if (ev.data1 != 0)
				D_PostEvent(&ev);
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			mousebuttons = buttons_of(SDL_GetMouseState(NULL, NULL));
			ev.type = ev_mouse;
			ev.data1 = mousebuttons;
			ev.data2 = ev.data3 = 0;
			D_PostEvent(&ev);
			break;
		case SDL_EVENT_MOUSE_MOTION:
			if (!grabmouse)
				break;
			ev.type = ev_mouse;
			ev.data1 = mousebuttons;
			ev.data2 = (int)(e.motion.xrel * 4);
			ev.data3 = (int)(-e.motion.yrel * 4);
			if (ev.data2 || ev.data3)
				D_PostEvent(&ev);
			break;
		default:
			break;
		}
	}
}

// Binary PPM of the current frame: no image library needed.
int I_WriteScreenshotPPM(const char *path)
{
	FILE *f = fopen(path, "wb");
	const byte *src = screens[0];
	int i;

	if (f == NULL)
		return -1;
	fprintf(f, "P6\n%d %d\n255\n", SCREENWIDTH, SCREENHEIGHT);
	for (i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++) {
		Uint32 c = argb[src[i]];
		fputc((int)((c >> 16) & 0xff), f);
		fputc((int)((c >> 8) & 0xff), f);
		fputc((int)(c & 0xff), f);
	}
	return fclose(f) == 0 ? 0 : -1;
}
