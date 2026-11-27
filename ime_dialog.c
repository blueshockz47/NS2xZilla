/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW

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

#include "main.h"
#include "ime_dialog.h"
#include "utils.h"
#ifdef __SWITCH2__
#include "theme.h"
#include "pgf.h"
#endif

static int ime_dialog_running = 0;
static int ime_dialog_option = 0;

#ifdef __SWITCH2__
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
static float keyboardY(void){return SCREEN_HEIGHT-370;}
void switch2KeyboardInput(int gesture,float x,float y){
  if(!custom_ime||custom_result)return;
  float top=keyboardY(),w=(SCREEN_WIDTH-20)/10.0f;
  if(gesture==1&&x>=10&&x<SCREEN_WIDTH-10&&y>=top+95&&y<top+287){int k=(int)((y-top-95)/48)*10+(int)((x-10)/w);custom_focus=k;customKey(k);}
  else if(gesture==1&&x>=10&&x<SCREEN_WIDTH-10&&y>=top+287&&y<top+345){int k=40+(int)((x-10)/((SCREEN_WIDTH-20)/6.0f));custom_focus=k;customKey(k);}
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
  vita2d_draw_rectangle(0,top,SCREEN_WIDTH,370,BACKGROUND_COLOR);
  pgf_draw_text(12,top+8,FOLDER_COLOR,custom_title);
  vita2d_draw_rectangle(10,top+38,SCREEN_WIDTH-20,44,CONTROL_HINTS_BAR_COLOR);
  vita2d_enable_clipping();vita2d_set_clip_rectangle(14,top+38,SCREEN_WIDTH-14,top+82);
  pgf_draw_text(16,top+48,0xffffffff,custom_text);vita2d_disable_clipping();
  const char *p=key_pages[custom_page];
  for(int k=0;k<46;k++){
    float kw=k<40?w:(SCREEN_WIDTH-20)/6.0f;
    float x=10+(k<40?k%10:k-40)*kw,y=top+(k<40?95+k/10*48:287);
    char ch[2]={k<40&&k<(int)strlen(p)?p[k]:' ',0};
    const char *actions[]={"Mode","Space","Del","Clear","Cancel","OK"};
    const char *label=k<40?ch:actions[k-40];
    vita2d_draw_rectangle(x+2,y+2,kw-4,44,k==custom_focus?FOLDER_COLOR:CONTROL_HINTS_BAR_COLOR);
    pgf_draw_text(x+(kw-pgf_text_width(label))/2,y+11,0xffffffff,label);
  }
}
#endif
static const char *ime_initial_text = NULL;
static uint16_t ime_title_utf16[SCE_IME_DIALOG_MAX_TITLE_LENGTH];
static uint16_t ime_initial_text_utf16[SCE_IME_DIALOG_MAX_TEXT_LENGTH];
static uint16_t ime_input_text_utf16[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
static uint8_t ime_input_text_utf8[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];

static void utf16_to_utf8(const uint16_t *src, uint8_t *dst) {
  int i;
  for (i = 0; src[i]; i++) {
    if ((src[i] & 0xFF80) == 0) {
      *(dst++) = src[i] & 0xFF;
    } else if((src[i] & 0xF800) == 0) {
      *(dst++) = ((src[i] >> 6) & 0xFF) | 0xC0;
      *(dst++) = (src[i] & 0x3F) | 0x80;
    } else if((src[i] & 0xFC00) == 0xD800 && (src[i + 1] & 0xFC00) == 0xDC00) {
      *(dst++) = (((src[i] + 64) >> 8) & 0x3) | 0xF0;
      *(dst++) = (((src[i] >> 2) + 16) & 0x3F) | 0x80;
      *(dst++) = ((src[i] >> 4) & 0x30) | 0x80 | ((src[i + 1] << 2) & 0xF);
      *(dst++) = (src[i + 1] & 0x3F) | 0x80;
      i += 1;
    } else {
      *(dst++) = ((src[i] >> 12) & 0xF) | 0xE0;
      *(dst++) = ((src[i] >> 6) & 0x3F) | 0x80;
      *(dst++) = (src[i] & 0x3F) | 0x80;
    }
  }

  *dst = '\0';
}

static void utf8_to_utf16(const uint8_t *src, uint16_t *dst) {
  int i;
  for (i = 0; src[i];) {
    if ((src[i] & 0xE0) == 0xE0) {
      *(dst++) = ((src[i] & 0x0F) << 12) | ((src[i + 1] & 0x3F) << 6) | (src[i + 2] & 0x3F);
      i += 3;
    } else if ((src[i] & 0xC0) == 0xC0) {
      *(dst++) = ((src[i] & 0x1F) << 6) | (src[i + 1] & 0x3F);
      i += 2;
    } else {
      *(dst++) = src[i];
      i += 1;
    }
  }

  *dst = '\0';
}

int initImeDialog(const char *title, const char *initial_text, int max_text_length, int type, int option, int password) {
  if (ime_dialog_running)
    return VITASHELL_ERROR_ALREADY_RUNNING;

  ime_initial_text = initial_text;
#ifdef __SWITCH2__
  if(switch2Portrait()&&!password&&option!=SCE_IME_OPTION_MULTILINE){custom_ime=1;custom_result=0;custom_page=0;custom_focus=0;custom_max=MIN(max_text_length,(int)sizeof(custom_text)-1);snprintf(custom_title,sizeof(custom_title),"%s",title);snprintf(custom_text,sizeof(custom_text),"%.*s",custom_max,initial_text);ime_dialog_running=1;return 0;}
#endif

  // Convert UTF8 to UTF16
  memset(ime_title_utf16, 0, sizeof(ime_title_utf16));
  memset(ime_initial_text_utf16, 0, sizeof(ime_initial_text_utf16));
  utf8_to_utf16((uint8_t *)title, ime_title_utf16);
  utf8_to_utf16((uint8_t *)initial_text, ime_initial_text_utf16);

  SceImeDialogParam param;
  sceImeDialogParamInit(&param);

  param.supportedLanguages = 0x0001FFFF;
  param.languagesForced = SCE_TRUE;
  param.type = type;
  param.option = option;
  if (option == SCE_IME_OPTION_MULTILINE)
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
  param.textBoxMode = password ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD : SCE_IME_DIALOG_TEXTBOX_MODE_DEFAULT;
  param.title = ime_title_utf16;
  param.maxTextLength = max_text_length;
  param.initialText = ime_initial_text_utf16;
  param.inputTextBuffer = ime_input_text_utf16;

  int res = sceImeDialogInit(&param);
  if (res >= 0) {
    ime_dialog_running = 1;
    ime_dialog_option = option;
  }

  return res;
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

#ifdef __SWITCH2__
  if(custom_ime){if(!custom_result)return IME_DIALOG_RESULT_RUNNING;int result=custom_result;if(result==IME_DIALOG_RESULT_FINISHED){snprintf((char*)ime_input_text_utf8,sizeof(ime_input_text_utf8),"%s",custom_text);utf8_to_utf16(ime_input_text_utf8,ime_input_text_utf16);}custom_ime=0;ime_dialog_running=0;return result;}
#endif
  SceCommonDialogStatus status = sceImeDialogGetStatus();
  if (status == IME_DIALOG_RESULT_FINISHED) {
    SceImeDialogResult result;
    memset(&result, 0, sizeof(SceImeDialogResult));
    sceImeDialogGetResult(&result);

    if ((ime_dialog_option == SCE_IME_OPTION_MULTILINE && result.button == SCE_IME_DIALOG_BUTTON_CLOSE) ||
        (ime_dialog_option != SCE_IME_OPTION_MULTILINE && result.button == SCE_IME_DIALOG_BUTTON_ENTER)) {
      // Convert UTF16 to UTF8
      utf16_to_utf8(ime_input_text_utf16, ime_input_text_utf8);
    } else {
      status = IME_DIALOG_RESULT_CANCELED;
    }

    sceImeDialogTerm();

    ime_dialog_running = 0;
  }

  return status;
}
