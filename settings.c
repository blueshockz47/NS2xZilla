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

  Modified for Switch 2: the settings are the theme, the confirm button and
  the system keyboard; 'Power' offers only leaving VitaShell.
*/

#include "main.h"
#include "config.h"
#include "init.h"
#include "theme.h"
#include "language.h"
#include "settings.h"
#include "message_dialog.h"
#include "ime_dialog.h"
#include "utils.h"

#ifdef __SWITCH2__
#include <switch2_platform.h>

static void exitShell();
#else
static void restartShell();
static void rebootDevice();
static void shutdownDevice();
static void suspendDevice();
#endif

static int changed = 0;
static int theme = 0;

static char spoofed_version[6];

static SettingsMenuEntry *settings_menu_entries = NULL;
static int n_settings_entries = 0;

#ifdef __SWITCH2__
// The Switch's A and B button symbols
static char *orientation_options[]={"0 Landscape","90 Portrait","180 Landscape","270 Portrait"};
static char *enter_button_options[] = { "\xEE\x83\xA0", "\xEE\x83\xA1" };
#else
static char *usbdevice_options[4];
static char *select_button_options[2];
#endif

static char **theme_options = NULL;
static int theme_count = 0;
static char *theme_name = NULL;

static ConfigEntry settings_entries[] = {
#ifdef __SWITCH2__
  { "ORIENTATION", CONFIG_TYPE_DECIMAL, &switch2_orientation },
  { "ENTER_BUTTON",       CONFIG_TYPE_DECIMAL, (int *)&vitashell_config.enter_button },
#else
  { "USBDEVICE",          CONFIG_TYPE_DECIMAL, (int *)&vitashell_config.usbdevice },
  { "SELECT_BUTTON",      CONFIG_TYPE_DECIMAL, (int *)&vitashell_config.select_button },
  { "DISABLE_AUTOUPDATE", CONFIG_TYPE_BOOLEAN, (int *)&vitashell_config.disable_autoupdate },
  { "DISABLE_WARNING",    CONFIG_TYPE_BOOLEAN, (int *)&vitashell_config.disable_warning },
#endif
};

static ConfigEntry theme_entries[] = {
  { "THEME_NAME", CONFIG_TYPE_STRING, (void *)&theme_name },
};

#ifdef __SWITCH2__
SettingsMenuOption main_settings[] = {
  { VITASHELL_SETTINGS_THEME, SETTINGS_OPTION_TYPE_OPTIONS, NULL,NULL,0,orientation_options,4,&switch2_orientation },
};

SettingsMenuOption power_settings[] = {
  { VITASHELL_SETTINGS_EXIT, SETTINGS_OPTION_TYPE_CALLBACK, (void *)exitShell, NULL, 0, NULL, 0, NULL },
};
#else
SettingsMenuOption main_settings[] = {
  // { VITASHELL_SETTINGS_LANGUAGE,     SETTINGS_OPTION_TYPE_BOOLEAN, NULL, NULL, 0, NULL, 0, &language },
  { VITASHELL_SETTINGS_THEME,           SETTINGS_OPTION_TYPE_OPTIONS, NULL, NULL, 0, NULL, 0, NULL },
  
  { VITASHELL_SETTINGS_USBDEVICE,       SETTINGS_OPTION_TYPE_OPTIONS, NULL, NULL, 0,
    usbdevice_options, sizeof(usbdevice_options) / sizeof(char **), &vitashell_config.usbdevice },
  { VITASHELL_SETTINGS_SELECT_BUTTON,   SETTINGS_OPTION_TYPE_OPTIONS, NULL, NULL, 0,
    select_button_options, sizeof(select_button_options) / sizeof(char **), &vitashell_config.select_button },
  { VITASHELL_SETTINGS_NO_AUTO_UPDATE,  SETTINGS_OPTION_TYPE_BOOLEAN, NULL, NULL, 0, NULL, 0, &vitashell_config.disable_autoupdate },
  { VITASHELL_SETTINGS_WARNING_MESSAGE, SETTINGS_OPTION_TYPE_BOOLEAN, NULL, NULL, 0, NULL, 0, &vitashell_config.disable_warning },

  { VITASHELL_SETTINGS_RESTART_SHELL,   SETTINGS_OPTION_TYPE_CALLBACK, (void *)restartShell, NULL, 0, NULL, 0, NULL },
};

SettingsMenuOption power_settings[] = {
  { VITASHELL_SETTINGS_REBOOT,    SETTINGS_OPTION_TYPE_CALLBACK, (void *)rebootDevice, NULL, 0, NULL, 0, NULL },
  { VITASHELL_SETTINGS_POWEROFF,  SETTINGS_OPTION_TYPE_CALLBACK, (void *)shutdownDevice, NULL, 0, NULL, 0, NULL },
  { VITASHELL_SETTINGS_STANDBY,   SETTINGS_OPTION_TYPE_CALLBACK, (void *)suspendDevice, NULL, 0, NULL, 0, NULL },
};
#endif

SettingsMenuEntry vitashell_settings_menu_entries[] = {
  { VITASHELL_SETTINGS_MAIN,  main_settings,  sizeof(main_settings) / sizeof(SettingsMenuOption) },
  { VITASHELL_SETTINGS_POWER, power_settings, sizeof(power_settings) / sizeof(SettingsMenuOption) },
};

static SettingsMenu settings_menu;

#ifdef __SWITCH2__
static void applyEnterButton() {
  enter_button = vitashell_config.enter_button ? SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS
                                               : SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;
}
#endif

void loadSettingsConfig() {
  // Load settings config file
  memset(&vitashell_config, 0, sizeof(VitaShellConfig));
  readConfig(VITASHELL_DATA_PATH "settings.txt", settings_entries, sizeof(settings_entries) / sizeof(ConfigEntry));

#ifdef __SWITCH2__
  if (vitashell_config.enter_button < 0 || vitashell_config.enter_button > 1)
    vitashell_config.enter_button = 0;
  if(switch2_orientation<0||switch2_orientation>3) switch2_orientation=0;
  vitashell_config.enter_button=0;
  applyEnterButton();
#endif
}

void saveSettingsConfig() {
  // Save settings config file
  writeConfig(VITASHELL_DATA_PATH "settings.txt", settings_entries, sizeof(settings_entries) / sizeof(ConfigEntry));

#ifdef __SWITCH2__
  vitashell_config.enter_button=0;
  applyEnterButton();
#else
  if (sceKernelGetModel() == SCE_KERNEL_MODEL_VITATV) {
    vitashell_config.select_button = SELECT_BUTTON_MODE_FTP;
  }
#endif
}

#ifdef __SWITCH2__
static void exitShell() {
  closeSettingsMenu();
  switch2RequestExit();
}
#else
static void restartShell() {
  closeSettingsMenu();
  sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
}

static void rebootDevice() {
  closeSettingsMenu();
  scePowerRequestColdReset();
}

static void shutdownDevice() {
  closeSettingsMenu();
  scePowerRequestStandby();
}

static void suspendDevice() {
  closeSettingsMenu();
  scePowerRequestSuspend();
}
#endif

void initSettingsMenu() {
  int i;

  memset(&settings_menu, 0, sizeof(SettingsMenu));
  settings_menu.status = SETTINGS_MENU_CLOSED;

  n_settings_entries = sizeof(vitashell_settings_menu_entries) / sizeof(SettingsMenuEntry);
  settings_menu_entries = vitashell_settings_menu_entries;

  for (i = 0; i < n_settings_entries; i++)
    settings_menu.n_options += settings_menu_entries[i].n_options;

#ifndef __SWITCH2__
  usbdevice_options[0] = language_container[VITASHELL_SETTINGS_USB_MEMORY_CARD];
  usbdevice_options[1] = language_container[VITASHELL_SETTINGS_USB_GAME_CARD];
  usbdevice_options[2] = language_container[VITASHELL_SETTINGS_USB_SD2VITA];
  usbdevice_options[3] = language_container[VITASHELL_SETTINGS_USB_PSVSD];

  select_button_options[0] = language_container[VITASHELL_SETTINGS_SELECT_BUTTON_USB];
  select_button_options[1] = language_container[VITASHELL_SETTINGS_SELECT_BUTTON_FTP];
#endif
  
  theme_options = malloc(MAX_THEMES * sizeof(char *));
  
  for (i = 0; i < MAX_THEMES; i++)
    theme_options[i] = malloc(MAX_THEME_LENGTH);
}

void openSettingsMenu() {
  settings_menu.status = SETTINGS_MENU_OPENING;
  settings_menu.entry_sel = 0;
  settings_menu.option_sel = 0;

  // Get current theme
  if (theme_name)
    free(theme_name);

  readConfig(VITASHELL_DATA_PATH "theme/theme.txt", theme_entries, sizeof(theme_entries) / sizeof(ConfigEntry));

  // Get theme index in main tab
  int theme_index = -1;

  int i;
  for (i = 0; i < (sizeof(main_settings) / sizeof(SettingsMenuOption)); i++) {
    if (main_settings[i].name == VITASHELL_SETTINGS_THEME && main_settings[i].value != &switch2_orientation) {
      theme_index = i;
      break;
    }
  }

  // Find all themes
  if (theme_index >= 0) {
    SceUID dfd = sceIoDopen(VITASHELL_DATA_PATH "theme");
    if (dfd >= 0) {
      theme_count = 0;
      theme = 0;

      int res = 0;

      do {
        SceIoDirent dir;
        memset(&dir, 0, sizeof(SceIoDirent));

        res = sceIoDread(dfd, &dir);
        if (res > 0) {
          if (SCE_S_ISDIR(dir.d_stat.st_mode)) {
            if (theme_name && strcasecmp(dir.d_name, theme_name) == 0)
              theme = theme_count;
            
            strncpy(theme_options[theme_count], dir.d_name, MAX_THEME_LENGTH);
            theme_count++;
          }
        }
      } while (res > 0 && theme_count < MAX_THEMES);
      
      sceIoDclose(dfd);
      
      main_settings[theme_index].options = theme_options;
      main_settings[theme_index].n_options = theme_count;
      main_settings[theme_index].value = &theme;
    }
  }

  changed = 0;
}

void closeSettingsMenu() {
  settings_menu.status = SETTINGS_MENU_CLOSING;

  // Save settings
  if (changed) {
    saveSettingsConfig();
      
    // Save theme config file
    if(theme_count>0 && theme>=0 && theme<theme_count) {
    theme_entries[0].value = &theme_options[theme];
    writeConfig(VITASHELL_DATA_PATH "theme/theme.txt", theme_entries, sizeof(theme_entries) / sizeof(ConfigEntry));
    theme_entries[0].value = (void *)&theme_name;
    }
  }
}

int getSettingsMenuStatus() {
  return settings_menu.status;
}

#ifdef __SWITCH2__
extern const char *switch2ThemeName(int);
extern unsigned int switch2ThemeColor(int);
extern int switch2ThemeIndex(void);
extern void switch2SetTheme(int);
static int theme_page,theme_focus,settings_focus;
static float settingsPanelX(void){return 24.0f;}
static float settingsPanelY(void){return (SCREEN_HEIGHT-(theme_page?340.0f:220.0f))/2;}
void settingsMenuTap(float tx,float ty){
  if(settings_menu.status!=SETTINGS_MENU_OPENED && settings_menu.status!=SETTINGS_MENU_OPENING) return;
  float x=settingsPanelX(),y=settingsPanelY();
  if(tx<x||tx>SCREEN_WIDTH-x||ty<y||ty>y+(theme_page?340:220)){closeSettingsMenu();return;}
  if(theme_page){if(ty>=y+45&&ty<y+285){int col=tx<SCREEN_WIDTH/2?0:1;int row=(int)((ty-y-45)/60);theme_focus=row*2+col;switch2SetTheme(theme_focus);}else if(ty>=y+290)theme_page=0;}
  else if(ty>=y+45&&ty<y+105){theme_page=1;theme_focus=switch2ThemeIndex();}
  else if(ty>=y+110&&ty<y+165)exitShell();
}
#endif
void drawSettingsMenu() {
  if (settings_menu.status == SETTINGS_MENU_CLOSED)
    return;

  // Closing settings menu
  if (settings_menu.status == SETTINGS_MENU_CLOSING) {
    if (settings_menu.cur_pos > 0.0f) {
      settings_menu.cur_pos -= easeOut(0.0f, settings_menu.cur_pos, 0.25f, 0.01f);
    } else {
      settings_menu.status = SETTINGS_MENU_CLOSED;
    }
  }

  // Opening settings menu
  if (settings_menu.status == SETTINGS_MENU_OPENING) {
    if (settings_menu.cur_pos < SCREEN_HEIGHT) {
      settings_menu.cur_pos += easeOut(settings_menu.cur_pos, SCREEN_HEIGHT, 0.25f, 0.01f);
    } else {
      settings_menu.status = SETTINGS_MENU_OPENED;
    }
  }

  // Draw settings menu
#ifdef __SWITCH2__
  vita2d_draw_rectangle(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,0x99000000);
  float px=settingsPanelX(),py=settingsPanelY();
  vita2d_draw_rectangle(px,py,SCREEN_WIDTH-2*px,theme_page?340:220,SETTINGS_MENU_COLOR);
  pgf_draw_text(px+12,py+12,SETTINGS_MENU_TITLE_COLOR,theme_page?"NS2xZilla Themes":"Settings");
  if(theme_page){float w=(SCREEN_WIDTH-2*px-24)/2;for(int i=0;i<8;i++){float x=px+8+(i%2)*(w+8),y=py+45+(i/2)*60;vita2d_draw_rectangle(x,y,w,54,i==theme_focus?switch2ThemeColor(i):SETTINGS_MENU_FOCUS_COLOR);vita2d_draw_rectangle(x+3,y+3,w-6,48,SETTINGS_MENU_COLOR);vita2d_draw_rectangle(x+10,y+12,24,24,switch2ThemeColor(i));pgf_draw_text(x+42,y+14,0xffffffff,switch2ThemeName(i));if(i==switch2ThemeIndex())pgf_draw_text(x+w-24,y+14,0xffffffff,"*");}}
  else {for(int i=0;i<2;i++){float y=py+45+i*65;vita2d_draw_rectangle(px+8,y,SCREEN_WIDTH-2*px-16,55,i==settings_focus?SETTINGS_MENU_FOCUS_COLOR:SETTINGS_MENU_COLOR);pgf_draw_text(px+20,y+6,0xffffffff,i?"Exit NS2xZilla":"NS2xZilla Themes");if(!i)pgf_draw_text(px+20,y+29,SETTINGS_MENU_OPTION_COLOR,switch2ThemeName(switch2ThemeIndex()));}}
  float fy=py+(theme_page?300:180);vita2d_draw_rectangle(px+12,fy,25,25,0xff45a622);pgf_draw_text(px+18,fy,0xffffffff,"A");pgf_draw_text(px+45,fy,0xffffffff,"Open / Select");vita2d_draw_rectangle(SCREEN_WIDTH/2+12,fy,25,25,0xff4040dc);pgf_draw_text(SCREEN_WIDTH/2+18,fy,0xffffffff,"B");pgf_draw_text(SCREEN_WIDTH/2+45,fy,0xffffffff,"Back");
  return;
#else
  vita2d_draw_texture(settings_image, 0.0f, SCREEN_HEIGHT - settings_menu.cur_pos);
#endif

  float y = SCREEN_HEIGHT - settings_menu.cur_pos + 100;

  int i;
  for (i = 0; i < n_settings_entries; i++) {
    // Title
    float x = pgf_text_width(language_container[settings_menu_entries[i].name]);
    pgf_draw_text(ALIGN_CENTER(SCREEN_WIDTH, x), y, SETTINGS_MENU_TITLE_COLOR, language_container[settings_menu_entries[i].name]);

    y += FONT_Y_SPACE;

    SettingsMenuOption *options = settings_menu_entries[i].options;

    int j;
    for (j = 0; j < settings_menu_entries[i].n_options; j++) {
      // Focus
      if (settings_menu.entry_sel == i && settings_menu.option_sel == j)
        vita2d_draw_rectangle(SHELL_MARGIN_X, y + 3.0f, MARK_WIDTH, FONT_Y_SPACE, SETTINGS_MENU_FOCUS_COLOR);

      if (options[j].type == SETTINGS_OPTION_TYPE_CALLBACK) {
        // Item
        float x = pgf_text_width((options[j].value==&switch2_orientation?"Screen rotation":options[j].name==VITASHELL_SETTINGS_EXIT?"Exit NS2xZilla":language_container[options[j].name]));
        pgf_draw_text(ALIGN_CENTER(SCREEN_WIDTH, x), y, SETTINGS_MENU_ITEM_COLOR, (options[j].value==&switch2_orientation?"Screen rotation":options[j].name==VITASHELL_SETTINGS_EXIT?"Exit NS2xZilla":language_container[options[j].name]));
      } else {
        // Item
        float x = pgf_text_width((options[j].value==&switch2_orientation?"Screen rotation":options[j].name==VITASHELL_SETTINGS_EXIT?"Exit NS2xZilla":language_container[options[j].name]));
        pgf_draw_text(ALIGN_RIGHT(SCREEN_HALF_WIDTH - 10.0f, x), y, SETTINGS_MENU_ITEM_COLOR, (options[j].value==&switch2_orientation?"Screen rotation":options[j].name==VITASHELL_SETTINGS_EXIT?"Exit NS2xZilla":language_container[options[j].name]));

        // Option
        switch (options[j].type) {
          case SETTINGS_OPTION_TYPE_BOOLEAN:
            pgf_draw_text(SCREEN_HALF_WIDTH + 10.0f, y, SETTINGS_MENU_OPTION_COLOR,
                          (options[j].value && *(options[j].value)) ? language_container[ON] : language_container[OFF]);
            break;

          case SETTINGS_OPTION_TYPE_STRING:
            pgf_draw_text(SCREEN_HALF_WIDTH + 10.0f, y, SETTINGS_MENU_OPTION_COLOR, options[j].string);
            break;

          case SETTINGS_OPTION_TYPE_OPTIONS:
          {
            int value = 0;
            if (options[j].value)
              value = *(options[j].value);
            pgf_draw_text(SCREEN_HALF_WIDTH + 10.0f, y, SETTINGS_MENU_OPTION_COLOR, options[j].options ? options[j].options[value] : "");
            break;
          }
        }
      }

      y += FONT_Y_SPACE;
    }

    y += FONT_Y_SPACE;
  }
}

static int agreement = SETTINGS_AGREEMENT_NONE;

void settingsAgree() {
  agreement = SETTINGS_AGREEMENT_AGREE;
}

void settingsDisagree() {
  agreement = SETTINGS_AGREEMENT_DISAGREE;
}

void settingsMenuCtrl() {
#ifdef __SWITCH2__
  if(pressed_pad[PAD_START]||pressed_pad[PAD_CANCEL]){if(theme_page)theme_page=0;else closeSettingsMenu();}
  else if(theme_page){if(pressed_pad[PAD_LEFT])theme_focus=MAX(0,theme_focus-1);if(pressed_pad[PAD_RIGHT])theme_focus=MIN(7,theme_focus+1);if(pressed_pad[PAD_UP])theme_focus=MAX(0,theme_focus-2);if(pressed_pad[PAD_DOWN])theme_focus=MIN(7,theme_focus+2);if(pressed_pad[PAD_ENTER])switch2SetTheme(theme_focus);}
  else {if(pressed_pad[PAD_UP]||pressed_pad[PAD_DOWN])settings_focus=1-settings_focus;if(pressed_pad[PAD_ENTER]){if(settings_focus)exitShell();else{theme_page=1;theme_focus=switch2ThemeIndex();}}}
  return;
#endif
  SettingsMenuOption *option = &settings_menu_entries[settings_menu.entry_sel].options[settings_menu.option_sel];

  // Agreement
  if (agreement != SETTINGS_AGREEMENT_NONE) {
    agreement = SETTINGS_AGREEMENT_NONE;
  }

  // Change options
  if (pressed_pad[PAD_ENTER] || pressed_pad[PAD_LEFT] || pressed_pad[PAD_RIGHT]) {
    changed = 1;

    switch (option->type) {
      case SETTINGS_OPTION_TYPE_BOOLEAN:
        if (option->value)
          *(option->value) = !*(option->value);
        break;
      
      case SETTINGS_OPTION_TYPE_STRING:
        initImeDialog(language_container[option->name], option->string, option->size_string, SCE_IME_TYPE_EXTENDED_NUMBER, 0, 0);
        setDialogStep(DIALOG_STEP_SETTINGS_STRING);
        break;
        
      case SETTINGS_OPTION_TYPE_CALLBACK:
        if (option->callback)
          option->callback(&option);
        break;
        
      case SETTINGS_OPTION_TYPE_OPTIONS:
      {
        if (option->value) {
          if (pressed_pad[PAD_LEFT]) {
            if (*(option->value) > 0)
              (*(option->value))--;
            else
              *(option->value) = option->n_options - 1;
          } else if (pressed_pad[PAD_ENTER] || pressed_pad[PAD_RIGHT]) {
            if (*(option->value) < option->n_options - 1)
              (*(option->value))++;
            else
              *(option->value) = 0;
          }
        }
        
        break;
      }
    }
  }

  // Move
  if (hold_pad[PAD_UP] || hold2_pad[PAD_LEFT_ANALOG_UP]) {
    if (settings_menu.option_sel > 0) {
      settings_menu.option_sel--;
    } else if (settings_menu.entry_sel > 0) {
      settings_menu.entry_sel--;
      settings_menu.option_sel = settings_menu_entries[settings_menu.entry_sel].n_options - 1;
    }
  } else if (hold_pad[PAD_DOWN] || hold2_pad[PAD_LEFT_ANALOG_DOWN]) {
    if (settings_menu.option_sel < settings_menu_entries[settings_menu.entry_sel].n_options - 1) {
      settings_menu.option_sel++;
    } else if (settings_menu.entry_sel < n_settings_entries - 1) {
      settings_menu.entry_sel++;
      settings_menu.option_sel = 0;
    }
  }

  // Close
  if (pressed_pad[PAD_START] || pressed_pad[PAD_CANCEL]) {
    closeSettingsMenu();
  }
}
