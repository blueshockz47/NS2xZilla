/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW
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
 * Switch 2 replacement for VitaShell's ime_dialog.c, keeping its interface
 * (ime_dialog.h). The Vita's system IME dialog becomes the Switch software
 * keyboard library applet (swkbd). The applet runs while initImeDialog
 * waits; the next updateImeDialog reports its result. If the applet cannot
 * start, the dialog is canceled.
 */

#include "main.h"
#include "init.h"
#include "ime_dialog.h"
#include "utils.h"
#include "theme.h"

#include <switch2_platform.h>

#include <switch.h>

#define MAX_TITLE_BYTES 256

enum KeyboardResult {
  KEYBOARD_FINISHED,
  KEYBOARD_CANCELED,
};

static int ime_dialog_running = 0;
static int keyboard_result = KEYBOARD_CANCELED;

extern int switch2Portrait(void);

static int custom_ime,custom_result,custom_max,custom_page,custom_focus;
static char custom_title[128],custom_text[SCE_IME_DIALOG_MAX_TEXT_LENGTH+1];
static const char *key_pages[]={"1234567890qwertyuiopasdfghjkl-zxcvbnm_.,","1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM_.,","1234567890!@#$%^&*()[]{}+=~;:-_.,'?`<>|"};
static void customKey(int k){
  size_t n=strlen(custom_text);
  if(k<40){const char *p=key_pages[custom_page];if(k<(int)strlen(p)&&n<(size_t)custom_max){custom_text[n]=p[k];custom_text[n+1]=0;}}
  else if(k==40)custom_page=(custom_page+1)%3;
  else if(k==41&&n<(size_t)custom_max){custom_text[n]=' ';custom_text[n+1]=0;}
  else if(k==42&&n){do{n--;}while(n&&((unsigned char)custom_text[n]&0xc0)==0x80);custom_text[n]=0;}
  else if(k==43)custom_text[0]=0;
  else if(k==44)custom_result=IME_DIALOG_RESULT_CANCELED;
  else if(k==45)custom_result=IME_DIALOG_RESULT_FINISHED;
}
static float keyboardY(void){return SCREEN_HEIGHT-480;}
void switch2KeyboardInput(int gesture,float x,float y){
  if(!custom_ime||custom_result)return;
  float top=keyboardY(),w=(SCREEN_WIDTH-20)/10.0f;
  if(gesture==1&&x>=10&&x<SCREEN_WIDTH-10&&y>=top+110&&y<top+382){int k=(int)((y-top-95)/68)*10+(int)((x-10)/w);custom_focus=k;customKey(k);}
  else if(gesture==1&&x>=10&&x<SCREEN_WIDTH-10&&y>=top+287&&y<top+460){int k=40+(int)((x-10)/((SCREEN_WIDTH-20)/6.0f));custom_focus=k;customKey(k);}
  if(pressed_pad[PAD_LEFT])custom_focus=(custom_focus+45)%46;
  if(pressed_pad[PAD_RIGHT])custom_focus=(custom_focus+1)%46;
  if(pressed_pad[PAD_UP])custom_focus=custom_focus>=10?custom_focus-10:custom_focus;
  if(pressed_pad[PAD_DOWN])custom_focus=MIN(45,custom_focus+10);
  if(pressed_pad[PAD_ENTER])customKey(custom_focus);
  if(pressed_pad[PAD_CANCEL])custom_result=IME_DIALOG_RESULT_CANCELED;
}
int switch2KeyboardActive(void){return custom_ime;}
void switch2DrawKeyboard(void){
  if(!custom_ime)return;
  float top=keyboardY(),w=(SCREEN_WIDTH-20)/10.0f;
  vita2d_draw_rectangle(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,0x99000000);
  vita2d_draw_rectangle(0,top,SCREEN_WIDTH,480,BACKGROUND_COLOR);
  pgf_draw_text(12,top+8,FOLDER_COLOR,custom_title);
  vita2d_draw_rectangle(10,top+38,SCREEN_WIDTH-20,60,CONTROL_HINTS_BAR_COLOR);
  vita2d_enable_clipping();vita2d_set_clip_rectangle(14,top+38,SCREEN_WIDTH-14,top+98);
  pgf_draw_text(16,top+55,0xffffffff,custom_text);vita2d_disable_clipping();
  const char *p=key_pages[custom_page];
  for(int k=0;k<46;k++){
    float kw=k<40?w:(SCREEN_WIDTH-20)/6.0f;
    float x=10+(k<40?k%10:k-40)*kw,y=top+(k<40?110+k/10*68:382);
    char ch[2]={k<40&&k<(int)strlen(p)?p[k]:' ',0};
    const char *actions[]={"Mode","Space","Del","Clear","Cancel","OK"};
    const char *label=k<40?ch:actions[k-40];
    vita2d_draw_rectangle(x+2,y+2,kw-4,k<40?64:72,k==44?0xff4040dc:k==45?0xff45a622:k==custom_focus?FOLDER_COLOR:CONTROL_HINTS_BAR_COLOR);
    pgf_draw_text(x+(kw-pgf_text_width(label))/2,y+20,0xffffffff,label);
  }
}

static const char *ime_initial_text = NULL;

static uint16_t ime_input_text_utf16[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static uint8_t ime_input_text_utf8[4 * SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

/* Decodes UTF-8 into code points; invalid bytes are taken as Latin-1.
   Returns the number of code points. */
static int decodeUtf8(const char *string, uint32_t *out, int max_count) {
  const unsigned char *bytes = (const unsigned char *)string;
  int count = 0;

  while (*bytes && count < max_count) {
    uint32_t value = *bytes;
    int length = 1;

    if (value >= 0xF0 && value < 0xF8)
      length = 4;
    else if (value >= 0xE0 && value < 0xF0)
      length = 3;
    else if (value >= 0xC0 && value < 0xE0)
      length = 2;

    if (length > 1) {
      uint32_t decoded = value & (0x7F >> length);
      int i;
      for (i = 1; i < length; i++) {
        if ((bytes[i] & 0xC0) != 0x80)
          break;
        decoded = (decoded << 6) | (bytes[i] & 0x3F);
      }

      if (i == length)
        value = decoded;
      else
        length = 1;
    }

    out[count++] = value;
    bytes += length;
  }

  return count;
}

/* Stores the keyboard's text as VitaShell reads it: UTF-8 and UTF-16. */
static void storeResult(const char *result) {
  snprintf((char *)ime_input_text_utf8, sizeof(ime_input_text_utf8), "%s", result);

  static uint32_t codepoints[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
  int count = decodeUtf8(result, codepoints, SCE_IME_DIALOG_MAX_TEXT_LENGTH);

  int i;
  int out = 0;
  for (i = 0; i < count && out < SCE_IME_DIALOG_MAX_TEXT_LENGTH; i++) {
    uint32_t codepoint = codepoints[i];
    if (codepoint >= 0x10000) {
      if (out + 2 > SCE_IME_DIALOG_MAX_TEXT_LENGTH)
        break;
      codepoint -= 0x10000;
      ime_input_text_utf16[out++] = (uint16_t)(0xD800 | (codepoint >> 10));
      ime_input_text_utf16[out++] = (uint16_t)(0xDC00 | (codepoint & 0x3FF));
    } else {
      ime_input_text_utf16[out++] = (uint16_t)codepoint;
    }
  }
  ime_input_text_utf16[out] = 0;
}

/* Shows the software keyboard and records its result. */
static void showKeyboard(const char *title, const char *initial_text, int max_length,
                         int type, int password) {
  keyboard_result = KEYBOARD_CANCELED;

  SwkbdConfig config;
  Result rc = swkbdCreate(&config, 0);
  if (R_FAILED(rc)) {
    fprintf(stderr, "ime: swkbdCreate: 0x%x\n", rc);
    return;
  }

  swkbdConfigMakePresetDefault(&config);
  swkbdConfigSetHeaderText(&config, title);
  swkbdConfigSetInitialText(&config, initial_text);
  swkbdConfigSetStringLenMax(&config, (u32)max_length);
  if (type == SCE_IME_TYPE_NUMBER || type == SCE_IME_TYPE_EXTENDED_NUMBER)
    swkbdConfigSetType(&config, SwkbdType_NumPad);
  if (password)
    swkbdConfigSetPasswordFlag(&config, 1);

  static char result[4 * SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
  memset(result, 0, sizeof(result));
  rc = swkbdShow(&config, result, sizeof(result));
  swkbdClose(&config);

  if (R_SUCCEEDED(rc)) {
    storeResult(result);
    keyboard_result = KEYBOARD_FINISHED;
    return;
  }

  // libnx reports a user cancel as a bad library applet exit.
  if (rc != MAKERESULT(Module_Libnx, LibnxError_LibAppletBadExit))
    fprintf(stderr, "ime: swkbdShow: 0x%x\n", rc);
}

/* Interface (ime_dialog.h) */

int initImeDialog(const char *title, const char *initial_text, int max_text_length, int type,
                  int option, int password) {
  (void)option;

  if (ime_dialog_running)
    return VITASHELL_ERROR_ALREADY_RUNNING;

  ime_initial_text = initial_text;
  ime_input_text_utf8[0] = '\0';
  ime_input_text_utf16[0] = 0;

  char header[MAX_TITLE_BYTES];
  snprintf(header, sizeof(header), "%s", title ? title : "");

  int max_length = max_text_length;
  if (max_length <= 0 || max_length > SCE_IME_DIALOG_MAX_TEXT_LENGTH)
    max_length = SCE_IME_DIALOG_MAX_TEXT_LENGTH;

  ime_dialog_running = 1;
  if(switch2Portrait()&&!password&&option!=SCE_IME_OPTION_MULTILINE&&type!=SCE_IME_TYPE_NUMBER&&type!=SCE_IME_TYPE_EXTENDED_NUMBER){custom_ime=1;custom_result=0;custom_page=0;custom_focus=0;custom_max=max_length;snprintf(custom_title,sizeof(custom_title),"%s",header);snprintf(custom_text,sizeof(custom_text),"%.*s",custom_max,initial_text?initial_text:"");return 0;}
  showKeyboard(header, initial_text ? initial_text : "", max_length, type, password);
  return 0;
}

int isImeDialogRunning() {
  return ime_dialog_running;
}

uint16_t *getImeDialogInputTextUTF16() {
  return ime_input_text_utf16;
}

uint8_t *getImeDialogInputTextUTF8() {
  return ime_input_text_utf8;
}

const char *getImeDialogInitialText() {
  return ime_initial_text;
}

int updateImeDialog() {
  if (!ime_dialog_running)
    return IME_DIALOG_RESULT_NONE;

  if(custom_ime){if(!custom_result)return IME_DIALOG_RESULT_RUNNING;int result=custom_result;if(result==IME_DIALOG_RESULT_FINISHED)storeResult(custom_text);custom_ime=0;ime_dialog_running=0;return result;}
  ime_dialog_running = 0;
  return keyboard_result == KEYBOARD_FINISHED ? IME_DIALOG_RESULT_FINISHED
                                              : IME_DIALOG_RESULT_CANCELED;
}
