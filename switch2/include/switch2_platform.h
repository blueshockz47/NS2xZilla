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
 * Switch 2 platform services that have no Vita SDK counterpart. VitaShell's
 * upstream files include this header only inside #ifdef __SWITCH2__ blocks.
 */

#ifndef VITASHELL_SWITCH2_PLATFORM_H
#define VITASHELL_SWITCH2_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VitaShell's data folder on the SD card, with the trailing slash. */
#define SWITCH2_VITASHELL_DIR "sdmc:/switch/ounceshell/"

/* An error for operations that exist on the Vita but not on Switch 2. */
#define SWITCH2_ERROR_NOT_SUPPORTED ((int)0xF0060000)

/* Event pump and exit (psp2_ctrl.c) */

/* Processes pending SDL events: controller hot-plug and the system's exit
   request. sceCtrlPeekBufferPositive calls this once per frame. */
void switch2PumpEvents(void);
/* Consume a gesture: 1 tap, 2 long press, 3 swipe up, 4 swipe down. */
int switch2TakeTap(float *x, float *y);

/* Nonzero once HOME's "close software" (SDL_QUIT) has been received or
   switch2RequestExit was called. While set, the controller reports a
   cancel press on every other frame so that VitaShell's viewers and dialogs
   unwind back to the file browser, which then returns from main. */
int switch2QuitRequested(void);
void switch2RequestExit(void);

/* Controls (psp2_ctrl.c) */

extern int switch2_orientation;
int switch2EffectiveOrientation(void);
int switch2Portrait(void);
int switch2ScreenWidth(void);
int switch2ScreenHeight(void);
int switch2MapTouch(float nx,float ny,float *x,float *y);
void switch2CtrlInit(void);
void switch2CtrlExit(void);

/* Rendering (vita2d_sdl.c, vita2d_font.c) */

/* Opens the system shared fonts (pl:u) and the bundled fallback font.
   Returns NULL only if no font at all could be opened. */
struct vita2d_pgf *switch2LoadSystemFonts(void);

/* Battery (psp2_misc.c). Returns 0 and fills the fields if the power state
   service answered; the result is cached for a few seconds. */
typedef struct {
  int percent;
  int charging;
} Switch2BatteryState;

int switch2GetBatteryState(Switch2BatteryState *state);
void switch2PowerInit(void);
void switch2PowerExit(void);

#ifdef __cplusplus
}
#endif

#endif
