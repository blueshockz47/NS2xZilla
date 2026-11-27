/*
  VitaShell for Switch 2
  Copyright (C) 2026, VitaShell Switch 2 port contributors

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * vita2d's drawing interface over SDL 2's renderer. On Switch 2, SDL's
 * renderer is OpenGL ES 2 through EGL and Mesa's Zink on NXVK.
 *
 * VitaShell draws in the Vita's 960x544 space; SDL_RenderSetLogicalSize
 * scales that to the window with a thin letterbox, keeping the upstream
 * layout unchanged. The window has the console's default resolution:
 * 1920x1080 handheld, the TV output setting docked (3840x2160 at 4K). It
 * follows docking and undocking, as libounce's default window does for
 * hbmenu. Images are decoded by SDL_image.
 */

#include "vita2d_internal.h"

#include <switch2_platform.h>

#include <switch.h>

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The window size if AM does not report the default resolution. */
#define FALLBACK_WINDOW_WIDTH 1280
#define FALLBACK_WINDOW_HEIGHT 720

#define FRAME_MICROSECONDS 16666

/* Upper bound on a PNG or BMP whose length must be found from its headers. */
#define MAX_IMAGE_BUFFER_SIZE (256 * 1024 * 1024)

struct vita2d_texture {
  SDL_Texture *texture;
  unsigned int width;
  unsigned int height;
  // CPU pixels of an empty texture that VitaShell writes; NULL otherwise.
  uint32_t *pixels;
  int pixels_dirty;
};

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static float output_scale = 1.0f;

// AM signals it when the default resolution changes and the new one is
// readable (after docking or undocking, or a TV setting change).
static Event resolution_event;
static int resolution_event_open = 0;

static unsigned int clear_color = 0xFF000000;
static int clipping_enabled = 0;
static SDL_Rect clip_rectangle = { 0, 0, VITA2D_LOGICAL_WIDTH, VITA2D_LOGICAL_HEIGHT };

static SceUInt64 last_frame_time = 0;

SDL_Renderer *vita2dRenderer(void) {
  return renderer;
}

float vita2dOutputScale(void) {
  return output_scale;
}

static void updateOutputScale(void) {
  int output_width = 0, output_height = 0;
  if (SDL_GetRendererOutputSize(renderer, &output_width, &output_height) != 0)
    return;

  float scale_x = (float)output_width / VITA2D_LOGICAL_WIDTH;
  float scale_y = (float)output_height / VITA2D_LOGICAL_HEIGHT;
  output_scale = scale_x < scale_y ? scale_x : scale_y;
  if (output_scale <= 0.0f)
    output_scale = 1.0f;
}

/* The console's default resolution, which the window takes. */
static void getWindowSize(int *width, int *height) {
  s32 default_width = 0, default_height = 0;
  if (R_SUCCEEDED(appletGetDefaultDisplayResolution(&default_width, &default_height)) &&
      default_width > 0 && default_height > 0) {
    *width = default_width;
    *height = default_height;
    return;
  }

  *width = FALLBACK_WINDOW_WIDTH;
  *height = FALLBACK_WINDOW_HEIGHT;
}

static SDL_Renderer *createRenderer(void) {
  SDL_Renderer *created = SDL_CreateRenderer(window, -1,
                                             SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (created)
    return created;

  fprintf(stderr, "vita2d: accelerated renderer failed: %s\n", SDL_GetError());
  created = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!created)
    fprintf(stderr, "vita2d: software renderer failed: %s\n", SDL_GetError());
  return created;
}

int vita2d_init(void) {
  if (renderer)
    return 0;

  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    fprintf(stderr, "vita2d: SDL video: %s\n", SDL_GetError());
    return -1;
  }

  // Open the change event before reading the resolution, so that a change
  // in between is not missed.
  if (R_SUCCEEDED(appletGetDefaultDisplayResolutionChangeEvent(&resolution_event))) {
    eventClear(&resolution_event);
    resolution_event_open = 1;
  }

  int window_width, window_height;
  getWindowSize(&window_width, &window_height);

  window = SDL_CreateWindow("OunceShell Pro", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                            window_width, window_height, 0);
  if (!window) {
    fprintf(stderr, "vita2d: window: %s\n", SDL_GetError());
    return -1;
  }

  renderer = createRenderer();
  if (!renderer) {
    SDL_DestroyWindow(window);
    window = NULL;
    return -1;
  }

  SDL_RenderSetLogicalSize(renderer, VITA2D_LOGICAL_WIDTH, VITA2D_LOGICAL_HEIGHT);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  updateOutputScale();

  if (TTF_Init() != 0)
    fprintf(stderr, "vita2d: SDL_ttf: %s\n", TTF_GetError());

  IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);

  return 0;
}

int vita2d_fini(void) {
  IMG_Quit();
  TTF_Quit();

  if (renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = NULL;
  }

  if (window) {
    SDL_DestroyWindow(window);
    window = NULL;
  }

  SDL_QuitSubSystem(SDL_INIT_VIDEO);

  if (resolution_event_open) {
    eventClose(&resolution_event);
    resolution_event_open = 0;
  }
  return 0;
}

/* Resizes the window to a new default resolution. SDL's Switch backend
   recreates the EGL surface at the new size; the fonts rasterize again at
   the new scale when they next draw (vita2d_font.c). */
static void followDisplayResolution(void) {
  if (!resolution_event_open || R_FAILED(eventWait(&resolution_event, 0)))
    return;

  int width, height;
  getWindowSize(&width, &height);

  int current_width, current_height;
  SDL_GetWindowSize(window, &current_width, &current_height);
  if (width == current_width && height == current_height)
    return;

  SDL_SetWindowSize(window, width, height);
  updateOutputScale();
}

void vita2d_start_drawing(void) {
  followDisplayResolution();
  updateOutputScale();

  clipping_enabled = 0;
  SDL_RenderSetClipRect(renderer, NULL);
}

void vita2d_end_drawing(void) {
}

void vita2d_swap_buffers(void) {
  SDL_RenderPresent(renderer);
}

int vita2d_common_dialog_update(void) {
  return 0;
}

void vita2d_wait_rendering_done(void) {
}

/* Presentation is vsynced; this only keeps the frame loop at 60 Hz if the
   swap interval was not honoured, since VitaShell animates per frame. */
int sceDisplayWaitVblankStart(void) {
  SceUInt64 now = sceKernelGetProcessTimeWide();
  if (last_frame_time != 0 && now - last_frame_time < FRAME_MICROSECONDS) {
    sceKernelDelayThread((SceUInt32)(FRAME_MICROSECONDS - (now - last_frame_time)));
    now = sceKernelGetProcessTimeWide();
  }

  last_frame_time = now;
  return 0;
}

void vita2d_set_clear_color(unsigned int color) {
  clear_color = color;
}

void vita2d_clear_screen(void) {
  Uint8 r, g, b, a;
  vita2dColorComponents(clear_color, &r, &g, &b, &a);
  SDL_SetRenderDrawColor(renderer, r, g, b, 0xFF);
  SDL_RenderClear(renderer);
}

static void applyClipping(void) {
  if (clipping_enabled)
    SDL_RenderSetClipRect(renderer, &clip_rectangle);
  else
    SDL_RenderSetClipRect(renderer, NULL);
}

void vita2d_enable_clipping(void) {
  clipping_enabled = 1;
  applyClipping();
}

void vita2d_disable_clipping(void) {
  clipping_enabled = 0;
  applyClipping();
}

/* VitaShell clips only text rows (file names, property and SFO values,
   audio tags), each to one 23-pixel row: enough for the PGF fonts, but
   Noto Sans and the shared fonts descend about 6 pixels below the
   baseline, 20 pixels down, so "g" and "y" lost their tails. The clip
   only has to stop long names horizontally; below, it allows the
   descent. */
#define CLIP_DESCENT_ALLOWANCE 6

void vita2d_set_clip_rectangle(int x_min, int y_min, int x_max, int y_max) {
  y_max += CLIP_DESCENT_ALLOWANCE;

  clip_rectangle.x = x_min;
  clip_rectangle.y = y_min;
  clip_rectangle.w = x_max > x_min ? x_max - x_min : 0;
  clip_rectangle.h = y_max > y_min ? y_max - y_min : 0;
  if (clipping_enabled) applyClipping();
}

void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned int color) {
  Uint8 r, g, b, a;
  vita2dColorComponents(color, &r, &g, &b, &a);

  SDL_FRect rectangle = { x, y, w, h };
  SDL_SetRenderDrawColor(renderer, r, g, b, a);
  SDL_RenderFillRectF(renderer, &rectangle);
}

/* Textures */

static vita2d_texture *wrapTexture(SDL_Texture *sdl_texture, unsigned int width,
                                   unsigned int height) {
  vita2d_texture *texture = calloc(1, sizeof(vita2d_texture));
  if (!texture) {
    SDL_DestroyTexture(sdl_texture);
    return NULL;
  }

  SDL_SetTextureBlendMode(sdl_texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(sdl_texture, SDL_ScaleModeLinear);

  texture->texture = sdl_texture;
  texture->width = width;
  texture->height = height;
  return texture;
}

vita2d_texture *vita2d_create_empty_texture(unsigned int w, unsigned int h) {
  if (!renderer || w == 0 || h == 0)
    return NULL;

  SDL_Texture *sdl_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                                               SDL_TEXTUREACCESS_STATIC, (int)w, (int)h);
  if (!sdl_texture) {
    fprintf(stderr, "vita2d: texture %ux%u: %s\n", w, h, SDL_GetError());
    return NULL;
  }

  vita2d_texture *texture = wrapTexture(sdl_texture, w, h);
  if (!texture)
    return NULL;

  texture->pixels = calloc((size_t)w * h, sizeof(uint32_t));
  if (!texture->pixels) {
    vita2d_free_texture(texture);
    return NULL;
  }

  texture->pixels_dirty = 1;
  return texture;
}

void vita2d_free_texture(vita2d_texture *texture) {
  if (!texture)
    return;

  if (texture->texture)
    SDL_DestroyTexture(texture->texture);
  free(texture->pixels);
  free(texture);
}

unsigned int vita2d_texture_get_width(const vita2d_texture *texture) {
  return texture ? texture->width : 0;
}

unsigned int vita2d_texture_get_height(const vita2d_texture *texture) {
  return texture ? texture->height : 0;
}

unsigned int vita2d_texture_get_stride(const vita2d_texture *texture) {
  return texture ? texture->width * 4 : 0;
}

void *vita2d_texture_get_datap(const vita2d_texture *texture) {
  if (!texture)
    return NULL;

  // The caller writes the pixels after this call; upload them at the next draw.
  vita2d_texture *writable = (vita2d_texture *)texture;
  writable->pixels_dirty = 1;
  return writable->pixels;
}

void vita2d_texture_set_filters(vita2d_texture *texture, SceGxmTextureFilter min_filter,
                                SceGxmTextureFilter mag_filter) {
  (void)min_filter;
  if (!texture)
    return;

  SDL_SetTextureScaleMode(texture->texture, mag_filter == SCE_GXM_TEXTURE_FILTER_POINT
                                                ? SDL_ScaleModeNearest
                                                : SDL_ScaleModeLinear);
}

static SDL_Texture *uploadedTexture(const vita2d_texture *texture) {
  vita2d_texture *writable = (vita2d_texture *)texture;
  if (writable->pixels && writable->pixels_dirty) {
    SDL_UpdateTexture(writable->texture, NULL, writable->pixels, (int)(writable->width * 4));
    writable->pixels_dirty = 0;
  }
  return writable->texture;
}

void vita2d_draw_texture(const vita2d_texture *texture, float x, float y) {
  if (!texture)
    return;

  SDL_FRect destination = { x, y, (float)texture->width, (float)texture->height };
  SDL_RenderCopyF(renderer, uploadedTexture(texture), NULL, &destination);
}

void vita2d_draw_texture_part(const vita2d_texture *texture, float x, float y,
                              float tex_x, float tex_y, float tex_w, float tex_h) {
  if (!texture || tex_w <= 0.0f || tex_h <= 0.0f)
    return;

  SDL_Rect source = { (int)tex_x, (int)tex_y, (int)ceilf(tex_w), (int)ceilf(tex_h) };
  SDL_FRect destination = { x, y, tex_w, tex_h };
  SDL_RenderCopyF(renderer, uploadedTexture(texture), &source, &destination);
}

void vita2d_draw_texture_scale(const vita2d_texture *texture, float x, float y,
                               float x_scale, float y_scale) {
  if (!texture)
    return;

  SDL_RendererFlip flip = SDL_FLIP_NONE;
  if (x_scale < 0.0f) {
    flip |= SDL_FLIP_HORIZONTAL;
    x_scale = -x_scale;
    x -= texture->width * x_scale;
  }
  if (y_scale < 0.0f) {
    flip |= SDL_FLIP_VERTICAL;
    y_scale = -y_scale;
    y -= texture->height * y_scale;
  }

  SDL_FRect destination = { x, y, texture->width * x_scale, texture->height * y_scale };
  SDL_RenderCopyExF(renderer, uploadedTexture(texture), NULL, &destination, 0.0, NULL, flip);
}

/* Draws the texture scaled by (x_scale, y_scale) and rotated by rad around
   its point (center_x, center_y), which lands at (x, y). */
void vita2d_draw_texture_scale_rotate_hotspot(const vita2d_texture *texture, float x, float y,
                                              float x_scale, float y_scale, float rad,
                                              float center_x, float center_y) {
  if (!texture || x_scale == 0.0f || y_scale == 0.0f)
    return;

  SDL_RendererFlip flip = SDL_FLIP_NONE;
  if (x_scale < 0.0f) {
    flip |= SDL_FLIP_HORIZONTAL;
    x_scale = -x_scale;
    center_x = texture->width - center_x;
  }
  if (y_scale < 0.0f) {
    flip |= SDL_FLIP_VERTICAL;
    y_scale = -y_scale;
    center_y = texture->height - center_y;
  }

  SDL_FPoint hotspot = { center_x * x_scale, center_y * y_scale };
  SDL_FRect destination = { x - hotspot.x, y - hotspot.y, texture->width * x_scale,
                            texture->height * y_scale };
  double degrees = (double)rad * 180.0 / M_PI;
  SDL_RenderCopyExF(renderer, uploadedTexture(texture), NULL, &destination, degrees, &hotspot,
                    flip);
}

/* Image loading */

static vita2d_texture *textureFromSurface(SDL_Surface *surface) {
  if (!surface)
    return NULL;

  if (!renderer) {
    SDL_FreeSurface(surface);
    return NULL;
  }

  SDL_Texture *sdl_texture = SDL_CreateTextureFromSurface(renderer, surface);
  unsigned int width = (unsigned int)surface->w;
  unsigned int height = (unsigned int)surface->h;
  SDL_FreeSurface(surface);

  if (!sdl_texture) {
    fprintf(stderr, "vita2d: image texture %ux%u: %s\n", width, height, SDL_GetError());
    return NULL;
  }

  return wrapTexture(sdl_texture, width, height);
}

/* An SDL stream over sceIo, so that image paths are resolved like every
   other VitaShell path. */
static Sint64 SDLCALL sceIoStreamSize(SDL_RWops *stream) {
  SceUID fd = (SceUID)(intptr_t)stream->hidden.unknown.data1;
  SceOff position = sceIoLseek(fd, 0, SCE_SEEK_CUR);
  SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
  sceIoLseek(fd, position, SCE_SEEK_SET);
  return size < 0 ? -1 : (Sint64)size;
}

static Sint64 SDLCALL sceIoStreamSeek(SDL_RWops *stream, Sint64 offset, int whence) {
  SceUID fd = (SceUID)(intptr_t)stream->hidden.unknown.data1;
  int sce_whence = whence == RW_SEEK_CUR ? SCE_SEEK_CUR
                 : whence == RW_SEEK_END ? SCE_SEEK_END : SCE_SEEK_SET;
  SceOff result = sceIoLseek(fd, (SceOff)offset, sce_whence);
  return result < 0 ? -1 : (Sint64)result;
}

static size_t SDLCALL sceIoStreamRead(SDL_RWops *stream, void *buffer, size_t size, size_t count) {
  SceUID fd = (SceUID)(intptr_t)stream->hidden.unknown.data1;
  if (size == 0 || count == 0)
    return 0;

  size_t total = size * count;
  size_t done = 0;
  while (done < total) {
    int chunk = sceIoRead(fd, (char *)buffer + done, (SceSize)(total - done));
    if (chunk <= 0)
      break;
    done += (size_t)chunk;
  }
  return done / size;
}

static size_t SDLCALL sceIoStreamWrite(SDL_RWops *stream, const void *buffer, size_t size,
                                       size_t count) {
  (void)stream;
  (void)buffer;
  (void)size;
  (void)count;
  return 0;
}

static int SDLCALL sceIoStreamClose(SDL_RWops *stream) {
  SceUID fd = (SceUID)(intptr_t)stream->hidden.unknown.data1;
  sceIoClose(fd);
  SDL_FreeRW(stream);
  return 0;
}

static SDL_RWops *openSceIoStream(const char *filename) {
  SceUID fd = sceIoOpen(filename, SCE_O_RDONLY, 0);
  if (fd < 0)
    return NULL;

  SDL_RWops *stream = SDL_AllocRW();
  if (!stream) {
    sceIoClose(fd);
    return NULL;
  }

  stream->size = sceIoStreamSize;
  stream->seek = sceIoStreamSeek;
  stream->read = sceIoStreamRead;
  stream->write = sceIoStreamWrite;
  stream->close = sceIoStreamClose;
  stream->type = SDL_RWOPS_UNKNOWN;
  stream->hidden.unknown.data1 = (void *)(intptr_t)fd;
  return stream;
}

static vita2d_texture *loadImageFile(const char *filename) {
  SDL_RWops *stream = openSceIoStream(filename);
  if (!stream)
    return NULL;
  return textureFromSurface(IMG_Load_RW(stream, 1));
}

static vita2d_texture *loadImageBuffer(const void *buffer, size_t size, const char *type) {
  if (!buffer || size == 0)
    return NULL;

  SDL_RWops *stream = SDL_RWFromConstMem(buffer, (int)size);
  if (!stream)
    return NULL;
  return textureFromSurface(IMG_LoadTyped_RW(stream, 1, type));
}

static uint32_t readBigEndian32(const uint8_t *bytes) {
  return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) |
         (uint32_t)bytes[3];
}

/* vita2d's PNG buffer loader takes no size: walk the chunks to IEND. */
static size_t pngBufferSize(const void *buffer) {
  static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
  const uint8_t *bytes = buffer;

  if (memcmp(bytes, signature, sizeof(signature)) != 0)
    return 0;

  size_t offset = sizeof(signature);
  while (offset + 12 <= MAX_IMAGE_BUFFER_SIZE) {
    uint32_t chunk_length = readBigEndian32(bytes + offset);
    if (chunk_length > MAX_IMAGE_BUFFER_SIZE)
      return 0;

    int is_end = memcmp(bytes + offset + 4, "IEND", 4) == 0;
    offset += 12 + (size_t)chunk_length;
    if (is_end)
      return offset;
  }

  return 0;
}

/* A BMP's file header records its size. */
static size_t bmpBufferSize(const void *buffer) {
  const uint8_t *bytes = buffer;
  if (bytes[0] != 'B' || bytes[1] != 'M')
    return 0;

  uint32_t size = (uint32_t)bytes[2] | ((uint32_t)bytes[3] << 8) | ((uint32_t)bytes[4] << 16) |
                  ((uint32_t)bytes[5] << 24);
  if (size < 26 || size > MAX_IMAGE_BUFFER_SIZE)
    return 0;
  return size;
}

vita2d_texture *vita2d_load_PNG_file(const char *filename) {
  return loadImageFile(filename);
}

vita2d_texture *vita2d_load_PNG_buffer(const void *buffer) {
  if (!buffer)
    return NULL;
  return loadImageBuffer(buffer, pngBufferSize(buffer), "PNG");
}

vita2d_texture *vita2d_load_JPEG_file(const char *filename) {
  return loadImageFile(filename);
}

vita2d_texture *vita2d_load_JPEG_buffer(const void *buffer, unsigned long buffer_size) {
  return loadImageBuffer(buffer, buffer_size, "JPG");
}

vita2d_texture *vita2d_load_BMP_file(const char *filename) {
  return loadImageFile(filename);
}

vita2d_texture *vita2d_load_BMP_buffer(const void *buffer) {
  if (!buffer)
    return NULL;
  return loadImageBuffer(buffer, bmpBufferSize(buffer), "BMP");
}
