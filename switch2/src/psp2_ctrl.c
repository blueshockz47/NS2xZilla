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
 * The Vita controller (sceCtrlPeekBufferPositive) from SDL joysticks.
 *
 * SDL's Switch backend creates one joystick per player slot, and the
 * player's controller is not necessarily slot 1, so every joystick is
 * opened and their states are merged.
 *
 * The face buttons map by position: the Vita's bottom button (cross) is the
 * Switch's B, right (circle) is A, top (triangle) is X and left (square) is
 * Y. VitaShell's "enter button" setting then decides whether A or B
 * confirms. L/ZL are the Vita's L, R/ZR its R, + is START and - is SELECT.
 */

#include <vitasdk.h>
#include <switch2_platform.h>

#include <SDL.h>
#include "vita2d_internal.h"
#include <math.h>

#include <stdlib.h>
#include <string.h>

/* Button indices of SDL's Switch joystick (SDL_sysjoystick.c). */
enum SwitchJoystickButton {
  SWITCH_BUTTON_A = 0,
  SWITCH_BUTTON_B = 1,
  SWITCH_BUTTON_X = 2,
  SWITCH_BUTTON_Y = 3,
  SWITCH_BUTTON_LSTICK = 4,
  SWITCH_BUTTON_RSTICK = 5,
  SWITCH_BUTTON_L = 6,
  SWITCH_BUTTON_R = 7,
  SWITCH_BUTTON_ZL = 8,
  SWITCH_BUTTON_ZR = 9,
  SWITCH_BUTTON_PLUS = 10,
  SWITCH_BUTTON_MINUS = 11,
  SWITCH_BUTTON_DLEFT = 12,
  SWITCH_BUTTON_DUP = 13,
  SWITCH_BUTTON_DRIGHT = 14,
  SWITCH_BUTTON_DDOWN = 15,
};

typedef struct {
  int switch_button;
  unsigned int vita_button;
} ButtonMapping;

static const ButtonMapping button_mappings[] = {
  { SWITCH_BUTTON_A,      SCE_CTRL_CIRCLE },
  { SWITCH_BUTTON_B,      SCE_CTRL_CROSS },
  { SWITCH_BUTTON_X,      SCE_CTRL_TRIANGLE },
  { SWITCH_BUTTON_Y,      SCE_CTRL_SQUARE },
  { SWITCH_BUTTON_LSTICK, SCE_CTRL_L3 },
  { SWITCH_BUTTON_RSTICK, SCE_CTRL_R3 },
  { SWITCH_BUTTON_L,      SCE_CTRL_LTRIGGER },
  { SWITCH_BUTTON_R,      SCE_CTRL_RTRIGGER },
  { SWITCH_BUTTON_ZL,     SCE_CTRL_LTRIGGER },
  { SWITCH_BUTTON_ZR,     SCE_CTRL_RTRIGGER },
  { SWITCH_BUTTON_PLUS,   SCE_CTRL_START },
  { SWITCH_BUTTON_MINUS,  SCE_CTRL_SELECT },
  { SWITCH_BUTTON_DLEFT,  SCE_CTRL_LEFT },
  { SWITCH_BUTTON_DUP,    SCE_CTRL_UP },
  { SWITCH_BUTTON_DRIGHT, SCE_CTRL_RIGHT },
  { SWITCH_BUTTON_DDOWN,  SCE_CTRL_DOWN },
};

#define N_BUTTON_MAPPINGS (sizeof(button_mappings) / sizeof(ButtonMapping))

#define MAX_JOYSTICKS 16

static SDL_Joystick *joysticks[MAX_JOYSTICKS];
static int quit_requested = 0;
static int quit_cancel_phase = 0;

/* From init: which Vita button confirms (SCE_SYSTEM_PARAM_ENTER_BUTTON_*). */
extern int enter_button;

static int isOpen(SDL_JoystickID instance) {
  int i;
  for (i = 0; i < MAX_JOYSTICKS; i++) {
    if (joysticks[i] && SDL_JoystickInstanceID(joysticks[i]) == instance)
      return 1;
  }
  return 0;
}

static void openJoystick(int device_index) {
  SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(device_index);
  if (instance >= 0 && isOpen(instance))
    return;

  int i;
  for (i = 0; i < MAX_JOYSTICKS; i++) {
    if (!joysticks[i]) {
      joysticks[i] = SDL_JoystickOpen(device_index);
      return;
    }
  }
}

static void closeJoystick(SDL_JoystickID instance) {
  int i;
  for (i = 0; i < MAX_JOYSTICKS; i++) {
    if (joysticks[i] && SDL_JoystickInstanceID(joysticks[i]) == instance) {
      SDL_JoystickClose(joysticks[i]);
      joysticks[i] = NULL;
    }
  }
}

void switch2CtrlInit(void) {
  if (!SDL_WasInit(SDL_INIT_JOYSTICK))
    SDL_InitSubSystem(SDL_INIT_JOYSTICK);

  int count = SDL_NumJoysticks();
  int i;
  for (i = 0; i < count; i++)
    openJoystick(i);
}

void switch2CtrlExit(void) {
  int i;
  for (i = 0; i < MAX_JOYSTICKS; i++) {
    if (joysticks[i]) {
      SDL_JoystickClose(joysticks[i]);
      joysticks[i] = NULL;
    }
  }
}

static int finger_active, tap_pending, finger_moved, gesture_kind;
static SDL_FingerID finger_id;
static float finger_x, finger_y, tap_x, tap_y;
static Uint32 finger_time;
int switch2TakeTap(float *x, float *y) {
  if (!tap_pending) return 0;
  tap_pending = 0;
  *x = tap_x; *y = tap_y; return gesture_kind;
}
void switch2PumpEvents(void) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_FINGERDOWN:
        if (!finger_active) { finger_active = 1; finger_moved = 0; finger_id = event.tfinger.fingerId; finger_x = event.tfinger.x; finger_y = event.tfinger.y; finger_time = SDL_GetTicks(); }
        else finger_moved = 1;
        break;
      case SDL_FINGERMOTION:
        if (finger_active && finger_id == event.tfinger.fingerId && (fabsf(event.tfinger.x-finger_x) > 0.015f || fabsf(event.tfinger.y-finger_y) > 0.025f)) finger_moved = 1;
        break;
      case SDL_FINGERUP:
        if (finger_active && finger_id == event.tfinger.fingerId) {
          finger_active = 0;
          if (finger_moved || (fabsf(event.tfinger.x-finger_x) <= 0.015f && fabsf(event.tfinger.y-finger_y) <= 0.025f)) {
            SDL_Renderer *r = vita2dRenderer();
            int w=0,h=0;
            if (r && SDL_GetRendererOutputSize(r,&w,&h)==0 && w>0 && h>0) {
              float scale=fminf((float)w/VITA2D_LOGICAL_WIDTH,(float)h/VITA2D_LOGICAL_HEIGHT);
              tap_x=(event.tfinger.x*w-(w-VITA2D_LOGICAL_WIDTH*scale)/2)/scale;
              tap_y=(event.tfinger.y*h-(h-VITA2D_LOGICAL_HEIGHT*scale)/2)/scale;
              gesture_kind=0;
              if(!finger_moved) gesture_kind=SDL_GetTicks()-finger_time>=500?2:1;
              else if(fabsf(event.tfinger.y-finger_y)>0.06f && fabsf(event.tfinger.y-finger_y)>fabsf(event.tfinger.x-finger_x)) gesture_kind=event.tfinger.y<finger_y?3:4;
              tap_pending=gesture_kind!=0;
            }
          }
        }
        break;
      case SDL_QUIT:
        quit_requested = 1;
        break;

      case SDL_JOYDEVICEADDED:
        openJoystick(event.jdevice.which);
        break;

      case SDL_JOYDEVICEREMOVED:
        closeJoystick(event.jdevice.which);
        break;
    }
  }
}

int switch2QuitRequested(void) {
  return quit_requested;
}

void switch2RequestExit(void) {
  quit_requested = 1;
}

/* SDL axis (-32768..32767, down and right positive) to the Vita's 0..255. */
static unsigned char axisToVita(Sint16 value) {
  return (unsigned char)(((int)value + 32768) >> 8);
}

/* The axis value furthest from the centre among all controllers. */
static Sint16 strongestAxis(int axis) {
  Sint16 strongest = 0;
  int i;
  for (i = 0; i < MAX_JOYSTICKS; i++) {
    if (!joysticks[i] || axis >= SDL_JoystickNumAxes(joysticks[i]))
      continue;

    Sint16 value = SDL_JoystickGetAxis(joysticks[i], axis);
    if (abs(value) > abs(strongest))
      strongest = value;
  }
  return strongest;
}

int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad_data, int count) {
  (void)port;

  if (!pad_data || count < 1)
    return -1;

  switch2PumpEvents();

  memset(pad_data, 0, sizeof(SceCtrlData));
  pad_data->timeStamp = sceKernelGetProcessTimeWide();

  if (quit_requested) {
    // Unwind VitaShell's nested viewers and dialogs: press cancel on every
    // other frame and centre the sticks.
    quit_cancel_phase = !quit_cancel_phase;
    if (quit_cancel_phase) {
      pad_data->buttons = enter_button == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE ? SCE_CTRL_CROSS
                                                                               : SCE_CTRL_CIRCLE;
    }
    pad_data->lx = pad_data->ly = pad_data->rx = pad_data->ry = 128;
    return 1;
  }

  int i;
  for (i = 0; i < MAX_JOYSTICKS; i++) {
    if (!joysticks[i])
      continue;

    int buttons = SDL_JoystickNumButtons(joysticks[i]);
    size_t m;
    for (m = 0; m < N_BUTTON_MAPPINGS; m++) {
      if (button_mappings[m].switch_button < buttons &&
          SDL_JoystickGetButton(joysticks[i], button_mappings[m].switch_button))
        pad_data->buttons |= button_mappings[m].vita_button;
    }
  }

  pad_data->lx = axisToVita(strongestAxis(0));
  pad_data->ly = axisToVita(strongestAxis(1));
  pad_data->rx = axisToVita(strongestAxis(2));
  pad_data->ry = axisToVita(strongestAxis(3));

  return 1;
}
