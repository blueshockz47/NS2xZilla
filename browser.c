#include <math.h>
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

  Modified for Switch 2: device roots are "sdmc:/", VPK packages open as
  archives, the Vita-only USB/FTP, QR, ad hoc and core dump features are
  left out, the browser returns when the system asks the program to exit,
  and an unreadable last-directory file no longer leaves garbage in the path.
*/

#include "main.h"
#include "main_context.h"
#include "browser.h"
#include "init.h"
#include "io_process.h"
#include "refresh.h"
#include "makezip.h"
#include "package_installer.h"
#include "context_menu.h"
#include "archive.h"
#include "photo.h"
#include "audioplayer.h"
#include "file.h"
#include "text.h"
#include "hex.h"
#include "settings.h"
#include "adhoc_dialog.h"
#include "property_dialog.h"
#include "message_dialog.h"
#include "netcheck_dialog.h"
#include "ime_dialog.h"
#include "theme.h"
#include "language.h"
#include "utils.h"
#include "sfo.h"
#include "coredump.h"
#include "usb.h"
#include "qr.h"
#include "pfs.h"

#ifdef __SWITCH2__
#include <switch2_platform.h>
#endif

// File lists
FileList file_list, mark_list, copy_list, install_list;

// Paths
char cur_file[MAX_PATH_LENGTH];
char archive_copy_path[MAX_PATH_LENGTH];
char archive_path[MAX_PATH_LENGTH];

static char focus_name[MAX_NAME_LENGTH];

// Position
int base_pos = 0, rel_pos = 0;
static int base_pos_list[MAX_DIR_LEVELS];
static int rel_pos_list[MAX_DIR_LEVELS];
static int dir_level = 0;
#ifdef __SWITCH2__
static int grid_view, touch_select, search_pending;
static void changeView(int grid);
static uint64_t last_tap_time;
static int last_tap_pos=-1;
static char search_query[128];
static char last_tap_path[MAX_PATH_LENGTH];
static int search_found;
static int palette;
static const unsigned int accents[]={0xFFFFB849,0xFFFF7AB7,0xFFA2E635,0xFF489AFF};
static const unsigned int backgrounds[]={0xFF251910,0xFF231319,0xFF1C200E,0xFF131825};
static const unsigned int panels[]={0xFF3D2A1B,0xFF3F2130,0xFF2F3819,0xFF1D2740};
#define THEME_PREF VITASHELL_DATA_PATH "internal/palette.txt"
static void applyPalette(void) { BACKGROUND_COLOR=backgrounds[palette]; CONTROL_HINTS_BAR_COLOR=panels[palette]; FOLDER_COLOR=accents[palette]; SCROLL_BAR_COLOR=accents[palette]; CONTEXT_MENU_FOCUS_COLOR=accents[palette]; SETTINGS_MENU_OPTION_COLOR=accents[palette]; MARKED_COLOR=(accents[palette]&0xffffff)|0x50000000; }
static void nextPalette(void) { palette=(palette+1)%4; applyPalette(); char v='0'+palette; WriteFile(THEME_PREF,&v,1); }
#define VIEW_PREF VITASHELL_DATA_PATH "internal/view.txt"
static void saveView(void) { char v=grid_view?'1':'0'; WriteFile(VIEW_PREF,&v,1); }
#define GRID_COLS 5
#define GRID_COUNT 15
#define GRID_W 184.0f
#define GRID_H 132.0f
static void gridFocus(int pos) {
  if (file_list.length <= 0) { base_pos=rel_pos=0; return; }
  pos=MAX(0,MIN(pos,file_list.length-1));
  base_pos=(base_pos/GRID_COLS)*GRID_COLS;
  if(pos<base_pos) base_pos=(pos/GRID_COLS)*GRID_COLS;
  if(pos>=base_pos+GRID_COUNT) base_pos=(pos/GRID_COLS-2)*GRID_COLS;
  rel_pos=pos-base_pos;
}
static void changeView(int grid) { int pos=base_pos+rel_pos; if(grid_view==grid) return; grid_view=grid; if(grid) gridFocus(pos); else { base_pos=MAX(0,MIN(pos,MAX(0,file_list.length-MAX_POSITION))); rel_pos=MAX(0,MIN(pos-base_pos,MAX_POSITION-1)); } saveView(); last_tap_pos=-1; }
#endif

// Modes
int sort_mode = SORT_BY_NAME;
int last_set_sort_mode = SORT_BY_NAME;
int copy_mode = COPY_MODE_NORMAL;
int file_type = FILE_TYPE_UNKNOWN;

// Archive
static int is_in_archive = 0;
static char dir_level_archive = -1;

// Scrolling filename
static int scroll_count = 0;
static float scroll_x = FILE_X;

SceInt64 time_last_recent_files, time_last_bookmarks;

static void fileBrowserHandleSymlink(FileListEntry* file_entry);
static void fileBrowserHandleFolder(FileListEntry* file_entry);
static void fileBrowserHandleFile(FileListEntry* file_entry);

static void create_recent_symlink(FileListEntry *file_entry);

// escape from dir hierarchy with a symlink
typedef struct SymlinkDirectoryPath {
  struct SymlinkDirectoryPath* previous;
  // contains / at the end, is directory where jumped from
  char last_path[MAX_PATH_LENGTH];
  // contains / at the end, is directory where jumped to
  char last_hook[MAX_PATH_LENGTH];
} SymlinkDirectoryPath;

static SymlinkDirectoryPath* symlink_directory_path = NULL;
static SymlinkDirectoryPath* symlink_directory_path_head = NULL;

static void storeSymlinkPath(SymlinkDirectoryPath * path) {
  if (!symlink_directory_path) {
    symlink_directory_path = path;
    symlink_directory_path_head = path;
    symlink_directory_path->previous = 0;
  } else {
    SymlinkDirectoryPath *prev = symlink_directory_path;
    symlink_directory_path = path;
    symlink_directory_path->previous = prev;
  }
}

void dirLevelUp() {
  if (dir_level < MAX_DIR_LEVELS - 1) {
    base_pos_list[dir_level] = base_pos;
    rel_pos_list[dir_level] = rel_pos;
    dir_level++;
    base_pos_list[dir_level] = 0;
    rel_pos_list[dir_level] = 0;
  }

  base_pos = 0;
  rel_pos = 0;
}

void setDirArchiveLevel() {
  dir_level_archive = dir_level;
}

void setInArchive() {
  is_in_archive = 1;
}

int isInArchive() {
  return is_in_archive;
}

void dirUpCloseArchive() {
  if (isInArchive() && dir_level_archive >= dir_level) {
    is_in_archive = 0;
    archiveClose();
    dir_level_archive = -1;
  }
}

int change_to_directory(char *lastdir) {
  if (!checkFolderExist(lastdir)) {
    return VITASHELL_ERROR_NAVIGATION;
  } else {
    if (isInArchive()) {
      dirUpCloseArchive();
    }
    int i;
    for (i = 0; i < strlen(lastdir) + 1; i++) {
#ifdef __SWITCH2__
      // A Switch device root is "sdmc:/": its colon and slash are one level.
      if (lastdir[i] == ':' && lastdir[i + 1] == '/')
        continue;
#endif
      if (lastdir[i] == ':' || lastdir[i] == '/') {
        char ch = lastdir[i + 1];
        lastdir[i + 1] = '\0';

        char ch2 = lastdir[i];
        lastdir[i] = '\0';

        char *p = strrchr(lastdir, '/');
        if (!p)
          p = strrchr(lastdir, ':');
        if (!p)
          p = lastdir - 1;
#ifdef __SWITCH2__
        // Focus the whole device entry "sdmc:/" at the root level.
        if (*p == ':' && p + 1 == lastdir + i)
          p = lastdir - 1;
#endif

        lastdir[i] = ch2;

        refreshFileList();
        setFocusOnFilename(p + 1);

        strcpy(file_list.path, lastdir);

        lastdir[i + 1] = ch;

        dirLevelUp();
      }
    }
  }
  refreshFileList();
  return 0;
}

static void dirUp() {
  if (pfs_mounted_path[0] &&
      strcmp(file_list.path, pfs_mounted_path) == 0 && // we're about to leave the pfs path
      !strstr(copy_list.path, pfs_mounted_path)) { // nothing has been copied from pfs path
    // Then umount
    pfsUmount();
  }

  // skip all symlink hierarchies when pressing O in bookmarks/ recent files
  if (symlink_directory_path_head &&
      ((strncmp(file_list.path, VITASHELL_BOOKMARKS_PATH, MAX_PATH_LENGTH) == 0)
       || strncmp(file_list.path, VITASHELL_RECENT_PATH, MAX_PATH_LENGTH) == 0)) {
    strcpy(file_list.path, symlink_directory_path_head->last_path);
    SymlinkDirectoryPath *e = symlink_directory_path;
    while(e != NULL) {
      SymlinkDirectoryPath *prev = e->previous;
      free(e);
      dir_level--;
      e = prev;
    }
    symlink_directory_path_head = 0;
    symlink_directory_path = 0;
    goto DIR_UP_RETURN;
  }

  if (symlink_directory_path
      && strncmp(file_list.path, symlink_directory_path->last_hook, MAX_PATH_LENGTH) == 0) {
    strcpy(file_list.path, symlink_directory_path->last_path);
    SymlinkDirectoryPath* prev = symlink_directory_path->previous;
    free(symlink_directory_path);
    symlink_directory_path = prev;
    dir_level--;
    goto DIR_UP_RETURN;
  }

  removeEndSlash(file_list.path);

  char *p;
  p = strrchr(file_list.path, '/');
  if (p) {
    p[1] = '\0';
    dir_level--;
    goto DIR_UP_RETURN;
  }

  p = strrchr(file_list.path, ':');
  if (p) {
    if (strlen(file_list.path) - ((p + 1) - file_list.path) > 0) {
      p[1] = '\0';
      dir_level--;
      goto DIR_UP_RETURN;
    }
  }

  strcpy(file_list.path, HOME_PATH);
  dir_level = 0;

DIR_UP_RETURN:
  if (dir_level < 0)
    dir_level = 0;

  base_pos = (int)base_pos_list[dir_level];
  rel_pos = (int)rel_pos_list[dir_level];
  dirUpCloseArchive();
}

void setFocusName(const char *name) {
  strncpy(focus_name, name, MAX_NAME_LENGTH-1);
  focus_name[MAX_NAME_LENGTH-1] = '\0';
}

void setFocusOnFilename(const char *name) {
  int name_pos = fileListGetNumberByName(&file_list, name);
  if (name_pos < 0 || name_pos >= file_list.length)
    return;
  
  if (name_pos >= base_pos && name_pos < (base_pos + MAX_POSITION)) {
    rel_pos = name_pos - base_pos;
  } else if (name_pos < base_pos) {
    base_pos = name_pos;
    rel_pos = 0;
  } else if (name_pos >= (base_pos + MAX_POSITION)) {
    rel_pos = MAX_POSITION - 1;
    base_pos = name_pos - rel_pos;
  }
}

int refreshFileList() {
  int ret = 0, res = 0;

  // always sort recent files by date
  char *contains = strstr(file_list.path, VITASHELL_RECENT_PATH);
  if (contains) {
    sort_mode = SORT_BY_DATE;
  } else {
    sort_mode = last_set_sort_mode;
  }

  do {
    fileListEmpty(&file_list);

    res = fileListGetEntries(&file_list, file_list.path, sort_mode);

    if (res < 0) {
      ret = res;
      dirUp();
    }
  } while (res < 0);
    
  // Position correction
  if (file_list.length >= MAX_POSITION) {
    if ((base_pos + rel_pos) >= file_list.length) {
      rel_pos = MAX_POSITION - 1;
    }
    
    if ((base_pos + MAX_POSITION - 1) >= file_list.length) {
      base_pos = file_list.length - MAX_POSITION;
    }
  } else {
    if ((base_pos + rel_pos) >= file_list.length) {
      rel_pos = file_list.length - 1;
    }
    
    base_pos = 0;
  }
  
  return ret;
}

static void refreshMarkList() {
  if (isInArchive())
    return;
  
  FileListEntry *entry = mark_list.head;

  int length = mark_list.length;

  int i;
  for (i = 0; i < length; i++) {
    // Get next entry already now to prevent crash after entry is removed
    FileListEntry *next = entry->next;

    char path[MAX_PATH_LENGTH];
    snprintf(path, MAX_PATH_LENGTH, "%s%s", file_list.path, entry->name);

    // Check if the entry still exits. If not, remove it from list
    SceIoStat stat;
    memset(&stat, 0, sizeof(SceIoStat));
    if (sceIoGetstat(path, &stat) < 0)
      fileListRemoveEntry(&mark_list, entry);

    // Next
    entry = next;
  }
}

static void refreshCopyList() {
  if (copy_list.is_in_archive)
    return;
  
  FileListEntry *entry = copy_list.head;

  int length = copy_list.length;

  int i;
  for (i = 0; i < length; i++) {
    // Get next entry already now to prevent crash after entry is removed
    FileListEntry *next = entry->next;

    char path[MAX_PATH_LENGTH];
    snprintf(path, MAX_PATH_LENGTH, "%s%s", copy_list.path, entry->name);

    // Check if the entry still exits. If not, remove it from list
    SceIoStat stat;
    memset(&stat, 0, sizeof(SceIoStat));
    if (sceIoGetstat(path, &stat) < 0)
      fileListRemoveEntry(&copy_list, entry);

    // Next
    entry = next;
  }
}

static int handleFile(const char *file, FileListEntry *entry) {
  int res = 0;

	// try to fix GPU freeze
	vita2d_wait_rendering_done();

  int type = getFileType(file);

#ifdef __SWITCH2__
  // A VPK is a zip archive; Vita core dumps open in the text/hex viewer.
  if (type == FILE_TYPE_VPK)
    type = FILE_TYPE_ARCHIVE;
  if (type == FILE_TYPE_PSP2DMP)
    type = FILE_TYPE_UNKNOWN;
#endif

  switch (type) {
    case FILE_TYPE_PSP2DMP:
    case FILE_TYPE_MP3:
    case FILE_TYPE_OGG:
    case FILE_TYPE_VPK:
    case FILE_TYPE_ARCHIVE:
      if (isInArchive())
        type = FILE_TYPE_UNKNOWN;

      break;
  }

  switch (type) {
#ifndef __SWITCH2__
    case FILE_TYPE_PSP2DMP:
      res = coredumpViewer(file);
      break;
#endif
      
    case FILE_TYPE_INI:
    case FILE_TYPE_TXT:
    case FILE_TYPE_XML:
    case FILE_TYPE_UNKNOWN:
      res = textViewer(file);
      break;
      
    case FILE_TYPE_BMP:
    case FILE_TYPE_PNG:
    case FILE_TYPE_JPEG:
      res = photoViewer(file, type, &file_list, entry, &base_pos, &rel_pos);
      break;

    case FILE_TYPE_MP3:
    case FILE_TYPE_OGG:
      res = audioPlayer(file, type, &file_list, entry, &base_pos, &rel_pos);
      break;
      
    case FILE_TYPE_SFO:
      res = SFOReader(file);
      break;
      
#ifndef __SWITCH2__
    case FILE_TYPE_VPK:
      initMessageDialog(SCE_MSG_DIALOG_BUTTON_TYPE_YESNO, language_container[INSTALL_QUESTION]);
      setDialogStep(DIALOG_STEP_INSTALL_QUESTION);
      break;
#endif
      
    case FILE_TYPE_ARCHIVE:
      archiveClearPassword();
      res = archiveOpen(file);
      if (res >= 0 && archiveNeedPassword()) {
        initImeDialog(language_container[ENTER_PASSWORD], "", 128, SCE_IME_TYPE_BASIC_LATIN, 0, 1);
        setDialogStep(DIALOG_STEP_ENTER_PASSWORD);
      }
      break;
      
    default:
      res = textViewer(file);
      break;
  }

  if (res < 0) {
    errorDialog(res);
    return res;
  }

  return type;
}

int shortCuts() {
  // bookmarks shortcut
  if (current_pad[PAD_SQUARE]) {
    SceInt64 now = sceKernelGetSystemTimeWide();

    // switching too quickly back and forth between recent and bookmarks
    // causes VS to crash
    if (now - time_last_bookmarks > THRESHOLD_LAST_PAD_BOOKMARKS_WAIT) {
      if (strncmp(file_list.path, VITASHELL_BOOKMARKS_PATH, MAX_PATH_LENGTH) != 0) {
        char path[MAX_PATH_LENGTH] = VITASHELL_BOOKMARKS_PATH;
        sort_mode = last_set_sort_mode;
        jump_to_directory_track_current_path(path);
        time_last_bookmarks = now;
        return 0;
      }
    }
  }

  // recent files shortcut
  if (current_pad[PAD_TRIANGLE]) {
    SceInt64 now = sceKernelGetSystemTimeWide();
    if (now - time_last_recent_files > THRESHOLD_LAST_PAD_RECENT_FILES_WAIT) {
      if (strncmp(file_list.path, VITASHELL_RECENT_PATH, MAX_PATH_LENGTH) != 0) {
        char path[MAX_PATH_LENGTH] = VITASHELL_RECENT_PATH;
        sort_mode = SORT_BY_DATE;
        jump_to_directory_track_current_path(path);
        time_last_recent_files = now;
        return 0;
      }
    }
  }

#ifndef __SWITCH2__
  // QR
  if (current_pad[PAD_CIRCLE] && enabledQR()) {
    startQR();
    initMessageDialog(MESSAGE_DIALOG_QR_CODE, language_container[QR_SCANNING]);
    setDialogStep(DIALOG_STEP_QR);
  }
#endif

  return 0;
}

static int fileBrowserMenuCtrl() {
  int refresh = 0;

  // Short cuts combo
#ifdef __SWITCH2__
  if(pressed_pad[PAD_LTRIGGER] || pressed_pad[PAD_RTRIGGER]) { changeView(pressed_pad[PAD_RTRIGGER]!=0); }
  if (pressed_pad[PAD_RIGHT_ANALOG_UP]) { changeView(!grid_view); }
  if (grid_view) {
    int pos=base_pos+rel_pos;
    if(hold_pad[PAD_UP] || hold2_pad[PAD_LEFT_ANALOG_UP]) gridFocus(pos-GRID_COLS);
    else if(hold_pad[PAD_DOWN] || hold2_pad[PAD_LEFT_ANALOG_DOWN]) gridFocus(pos+GRID_COLS);
    else if(hold_pad[PAD_LEFT] || hold2_pad[PAD_LEFT_ANALOG_LEFT]) gridFocus(pos-1);
    else if(hold_pad[PAD_RIGHT] || hold2_pad[PAD_LEFT_ANALOG_RIGHT]) gridFocus(pos+1);

  }
  if (!grid_view && current_pad[PAD_LEFT])
#else
  if (current_pad[PAD_LEFT])
#endif
    return shortCuts();

  // Settings menu
  if (pressed_pad[PAD_START]) {
    openSettingsMenu();
  }

  // SELECT button
  if (pressed_pad[PAD_SELECT]) {
#ifndef __SWITCH2__
    if (vitashell_config.select_button == SELECT_BUTTON_MODE_USB &&
        sceKernelGetModel() == SCE_KERNEL_MODEL_VITA) {
      if (isSafeMode()) {
        infoDialog(language_container[EXTENDED_PERMISSIONS_REQUIRED]);
      } else {
        SceUdcdDeviceState state;
        sceUdcdGetDeviceState(&state);

        if (state.cable & SCE_UDCD_STATUS_CABLE_CONNECTED) {
          initUsb();
        } else {
          initMessageDialog(SCE_MSG_DIALOG_BUTTON_TYPE_CANCEL,
                            language_container[USB_NOT_CONNECTED]);
          setDialogStep(DIALOG_STEP_USB_WAIT);
        }
      }
    } else if (vitashell_config.select_button == SELECT_BUTTON_MODE_FTP ||
               sceKernelGetModel() == SCE_KERNEL_MODEL_VITATV) {
#else
    // Modified for Switch 2: SELECT (the - button) starts the FTP server.
    // There is no USB mode.
    {
#endif
      // Init FTP
      if (!ftpvita_is_initialized()) {
        int res = ftpvita_init(vita_ip, &vita_port);
        if (res < 0) {
          initMessageDialog(SCE_MSG_DIALOG_BUTTON_TYPE_CANCEL, language_container[PLEASE_WAIT]);
          setDialogStep(DIALOG_STEP_FTP_WAIT);
        } else {
          initFtp();
        }

        // Lock power timers
        powerLock();
      }

      // Dialog
      if (ftpvita_is_initialized()) {
        initMessageDialog(SCE_MSG_DIALOG_BUTTON_TYPE_OK_CANCEL, language_container[FTP_SERVER],
                          vita_ip, vita_port);
        setDialogStep(DIALOG_STEP_FTP);
      }
    }
  }

  // Move
#ifdef __SWITCH2__
  if (!grid_view) {
#endif
  if (hold_pad[PAD_UP] || hold2_pad[PAD_LEFT_ANALOG_UP]) {
    int old_pos = base_pos + rel_pos;

    if (rel_pos > 0) {
      rel_pos--;
    } else if (base_pos > 0) {
      base_pos--;
    }

    if (old_pos != base_pos + rel_pos) {
      scroll_count = 0;
    }
  } else if (hold_pad[PAD_DOWN] || hold2_pad[PAD_LEFT_ANALOG_DOWN]) {
    int old_pos = base_pos + rel_pos;

    if ((old_pos + 1) < file_list.length) {
      if ((rel_pos + 1) < MAX_POSITION) {
        rel_pos++;
      } else if ((base_pos + rel_pos + 1) < file_list.length) {
        base_pos++;
      }
    }

    if (old_pos != base_pos + rel_pos) {
      scroll_count = 0;
    }
  }

  // Page skip
  if (
#ifdef __SWITCH2__
      0
#else
      hold_pad[PAD_LTRIGGER] || hold_pad[PAD_RTRIGGER]
#endif
  ) {
    int old_pos = base_pos + rel_pos;

    if (hold_pad[PAD_LTRIGGER]) { // Skip page up
      base_pos = base_pos - MAX_ENTRIES;
      if (base_pos < 0) {
        base_pos = 0;
        rel_pos = 0;
      }
    } else { // Skip page down
      base_pos = base_pos + MAX_ENTRIES;
      if (base_pos >= file_list.length - MAX_POSITION) {
        base_pos = MAX(file_list.length - MAX_POSITION, 0);
        rel_pos = MIN(MAX_POSITION - 1, file_list.length - 1);
      }
    }

    if (old_pos != base_pos + rel_pos) {
      scroll_count = 0;
    }
  }

#ifdef __SWITCH2__
  }
#endif
  // Context menu trigger
  if (pressed_pad[PAD_TRIANGLE]) {
    if (getContextMenuMode() == CONTEXT_MENU_CLOSED) {
      if (dir_level > 0) {
        setContextMenu(&context_menu_main);
        setContextMenuMainVisibilities();
        setContextMenuMode(CONTEXT_MENU_OPENING);
      } else {
        // Switch 2: every 'home' menu entry is a Vita mount or refresh.
#ifndef __SWITCH2__
        setContextMenu(&context_menu_home);
        setContextMenuHomeVisibilities();
        setContextMenuMode(CONTEXT_MENU_OPENING);
#endif
      }
    }
  }

  // Not at 'home'
  if (dir_level > 0) {
    // Mark entry    
    if (pressed_pad[PAD_SQUARE]) {
      FileListEntry *file_entry = fileListGetNthEntry(&file_list, base_pos + rel_pos);
      if (file_entry && strcmp(file_entry->name, DIR_UP) != 0) {
        if (!fileListFindEntry(&mark_list, file_entry->name)) {
          fileListAddEntry(&mark_list, fileListCopyEntry(file_entry), SORT_NONE);
        } else {
          fileListRemoveEntryByName(&mark_list, file_entry->name);
        }
      }
    }

    // Back
    if (pressed_pad[PAD_CANCEL]) {
      scroll_count = 0;
      fileListEmpty(&mark_list);
      dirUp();
      WriteFile(VITASHELL_LASTDIR, file_list.path, strlen(file_list.path) + 1);
      refreshFileList();
    }
  }

  // Handle
  if (pressed_pad[PAD_ENTER]) {
    scroll_count = 0;

    fileListEmpty(&mark_list);

    // Handle file, symlink or folder
    FileListEntry *file_entry = fileListGetNthEntry(&file_list, base_pos + rel_pos);
    if (file_entry) {
      if (file_entry->is_symlink) {
        fileBrowserHandleSymlink(file_entry);
      } else if (file_entry->is_folder) {
        fileBrowserHandleFolder(file_entry);
      } else {
        fileBrowserHandleFile(file_entry);
        create_recent_symlink(file_entry);
      }
    }
  }
  return refresh;
}

static void create_recent_symlink(FileListEntry *file_entry) {
  if (isInArchive()) return;

  char target[MAX_PATH_LENGTH];
  snprintf(target, MAX_PATH_LENGTH, "%s%s."SYMLINK_EXT, VITASHELL_RECENT_PATH,
               file_entry->name);
  snprintf(cur_file, MAX_PATH_LENGTH, "%s%s", file_list.path, file_entry->name);

  // create file recent symlink
  createSymLink(target, cur_file);
}

static void fileBrowserHandleFile(FileListEntry *file_entry) {
  snprintf(cur_file, MAX_PATH_LENGTH, "%s%s", file_list.path, file_entry->name);
  int type = handleFile(cur_file, file_entry);

  // Archive mode
  if (type == FILE_TYPE_ARCHIVE && getDialogStep() != DIALOG_STEP_ENTER_PASSWORD) {
    is_in_archive = 1;
    dir_level_archive = dir_level;

    snprintf(archive_path, MAX_PATH_LENGTH, "%s%s", file_list.path, file_entry->name);

    strcat(file_list.path, file_entry->name);
    addEndSlash(file_list.path);

    dirLevelUp();
    refreshFileList();
  }
}

static void fileBrowserHandleFolder(FileListEntry *file_entry) {
  if (strcmp(file_entry->name, DIR_UP) == 0) {
    dirUp();
  } else {
    if (dir_level == 0) {
      strcpy(file_list.path, file_entry->name);
    } else {
      if (dir_level > 1)
        addEndSlash(file_list.path);
      strcat(file_list.path, file_entry->name);
    }
    dirLevelUp();
  }

  // Save last dir
  WriteFile(VITASHELL_LASTDIR, file_list.path, strlen(file_list.path) + 1);

  // Open folder
  int res = refreshFileList();
  if (res < 0)
    errorDialog(res);
}

// escape from dir level structure so that parent directory is browsed
// where this jump came from and not the hierarchically higher folder
int jump_to_directory_track_current_path(char *path) {
  SymlinkDirectoryPath *symlink_path = malloc(sizeof(SymlinkDirectoryPath));

  int archive = 0;
  while(isInArchive()) {
    archive = 1;
    dirUp();
  }
  if (archive) {
    dirUpCloseArchive();
  }

  if (symlink_path) {
    strncpy(symlink_path->last_path, file_list.path, MAX_PATH_LENGTH);
    strncpy(symlink_path->last_hook, path, MAX_PATH_LENGTH);
    dirLevelUp();
    int _dir_level = dir_level; // we escape from hierarchical dir level structure
    if (change_to_directory(path) < 0) {
      free(symlink_path);
      return VITASHELL_ERROR_NAVIGATION;
    }
    WriteFile(VITASHELL_LASTDIR, file_list.path, strlen(file_list.path) + 1);
    storeSymlinkPath(symlink_path);
    dir_level = _dir_level;
    refreshFileList();
  }
  return 0;
}

static void fileBrowserHandleSymlink(FileListEntry *file_entry) {
  if ((file_entry->symlink->to_file == 1 && !checkFileExist(file_entry->symlink->target_path))
      || (file_entry->symlink->to_file == 0 && !checkFolderExist(file_entry->symlink->target_path))) {
    // TODO: What if in archive?
    snprintf(cur_file, MAX_PATH_LENGTH, "%s%s", file_list.path, file_entry->name);
    textViewer(cur_file);
    return;
  }
  if (file_entry->symlink->to_file == 0) {
    if (jump_to_directory_track_current_path(file_entry->symlink->target_path) < 0) {
      errorDialog(VITASHELL_ERROR_SYMLINK_INVALID_PATH);
    }
  } else {
    char *target_base_directory = getBaseDirectory(file_entry->symlink->target_path);
    if (!target_base_directory) {
      errorDialog(VITASHELL_ERROR_SYMLINK_CANT_RESOLVE_BASEDIR);
      return;
    }
    char *target_file_name = getFilename(file_entry->symlink->target_path);
    if (!target_file_name) {
      errorDialog(VITASHELL_ERROR_SYMLINK_CANT_RESOLVE_FILENAME);
      return;
    }
    if (jump_to_directory_track_current_path(target_base_directory) < 0) {
      errorDialog(VITASHELL_ERROR_SYMLINK_INVALID_PATH);
      return;
    }
    FileListEntry *resolved_file_entry = fileListFindEntry(&file_list, target_file_name);
    if (!resolved_file_entry) {
      errorDialog(VITASHELL_ERROR_SYMLINK_INVALID_PATH);
      return;
    }
    fileBrowserHandleFile(resolved_file_entry);
    dirUp();

    free(target_file_name);
    free(target_base_directory);
  }
  int res = refreshFileList();
  if (res < 0)
    errorDialog(res);
}

#ifdef __SWITCH2__
// Added for Switch 2: a row of button hints under the file list, so that
// actions such as - (SELECT) starting the FTP server can be found. The
// symbols are the Nintendo extension font's controller buttons, as in the
// dialogs (uncommon_dialog.h).
#define HINT_BUTTON_A "\xEE\x83\xA0"
#define HINT_BUTTON_B "\xEE\x83\xA1"
#define HINT_BUTTON_X "\xEE\x83\xA2"
#define HINT_BUTTON_Y "\xEE\x83\xA3"
#define HINT_BUTTON_L "\xEE\x83\xA4"
#define HINT_BUTTON_R "\xEE\x83\xA5"
#define HINT_BUTTON_PLUS "\xEE\x83\xAF"
#define HINT_BUTTON_MINUS "\xEE\x83\xB0"

// Space between a button and its action, and between two hints.
#define HINT_BUTTON_SPACE 6.0f
#define HINT_SPACE 26.0f

// The bar behind the hints, from below the file list (which ends at 478)
// to the bottom of the screen.
#define HINT_BAR_TOP 484.0f

// A theme with a browser background but no CONTROL_HINTS_BAR_COLOR most
// likely paints the Vita's button legend there; this bar covers it.
#define HINT_BAR_FALLBACK_COLOR 0xD0000000

typedef struct {
  const char *button;
  const char *action;
} ControlHint;

static int browserHasFocus(void) {
  return getDialogStep() == DIALOG_STEP_NONE &&
         getPropertyDialogStatus() == PROPERTY_DIALOG_CLOSED &&
         getSettingsMenuStatus() == SETTINGS_MENU_CLOSED &&
         getContextMenuMode() == CONTEXT_MENU_CLOSED;
}

/* Rounded silhouettes using non-overlapping scanline bands. */
static void roundedBox(float x,float y,float w,float h,int radius,unsigned int color) {
  for(int n=0;n<radius;n++) { float dy=radius-n-.5f; float dx=radius-sqrtf(radius*radius-dy*dy); vita2d_draw_rectangle(x+dx,y+n,w-2*dx,1,color); vita2d_draw_rectangle(x+dx,y+h-n-1,w-2*dx,1,color); }
  vita2d_draw_rectangle(x,y+radius,w,h-2*radius,color);
}
static unsigned int shade(unsigned int c,int percent) {
  return 0xff000000|(((c&255)*percent/100)&255)|((((c>>8&255)*percent/100)&255)<<8)|((((c>>16&255)*percent/100)&255)<<16);
}
static void drawModernIcon(FileListEntry *e, float x, float y, float s) {
  if(e->is_folder) {
    int rad=s>=50?6:2;
    roundedBox(x+s*.06f,y+s*.12f,s*.42f,s*.30f,rad,shade(accents[palette],65));
    roundedBox(x+s*.06f,y+s*.25f,s*.88f,s*.61f,rad,shade(accents[palette],78));
    roundedBox(x+s*.06f,y+s*.34f,s*.88f,s*.52f,rad,accents[palette]);
    vita2d_draw_rectangle(x+s*.16f,y+s*.37f,s*.68f,s*.035f,0x80FFFFFF);
    if(strcmp(e->name,DIR_UP)==0) pgf_draw_text(x+s*.32f,y+s*.39f,0xFFFFFFFF,"<");
  } else {
    vita2d_draw_rectangle(x+s*.19f,y+s*.07f,s*.62f,s*.82f,0xFFF0EAE4);
    vita2d_draw_rectangle(x+s*.59f,y+s*.07f,s*.22f,s*.22f,0xFFC8BFB5);
    unsigned int accent=0xFFCB9A68;
    if(e->type==FILE_TYPE_ARCHIVE || e->type==FILE_TYPE_VPK) accent=0xFF52A5E8;
    else if(e->type==FILE_TYPE_PNG || e->type==FILE_TYPE_JPEG || e->type==FILE_TYPE_BMP) accent=0xFF8EBB55;
    else if(e->type==FILE_TYPE_MP3 || e->type==FILE_TYPE_OGG) accent=0xFFBC79BC;
    vita2d_draw_rectangle(x+s*.19f,y+s*.57f,s*.62f,s*.19f,accent);
  }
}
static void drawGrid(void) {
  FileListEntry *e=fileListGetNthEntry(&file_list,base_pos);
  for(int i=0;e && i<GRID_COUNT;i++,e=e->next) {
    float x=SHELL_MARGIN_X+(i%GRID_COLS)*GRID_W, y=START_Y+(i/GRID_COLS)*GRID_H;
    if(i==rel_pos) roundedBox(x+4,y+2,GRID_W-8,GRID_H-6,10,((accents[palette]&0xffffff)|0x50000000));
    if(strcmp(e->name,DIR_UP)!=0 && (touch_select || fileListFindEntry(&mark_list,e->name))) { vita2d_draw_rectangle(x+8,y+6,16,16,0xFF7C7167); vita2d_draw_rectangle(x+10,y+8,12,12,fileListFindEntry(&mark_list,e->name)?accents[palette]:panels[palette]); }
    drawModernIcon(e,x+(GRID_W-72)/2,y+7,72);
    char label[MAX_NAME_LENGTH]; snprintf(label,sizeof(label),"%s",strcmp(e->name,DIR_UP)==0?"Back":e->name); if(pgf_text_width(label)>GRID_W-16) { size_t n=strlen(label); while(n>0) { n--; while(n>0 && ((unsigned char)label[n]&0xc0)==0x80) n--; label[n]=0; if(pgf_text_width(label)+pgf_text_width("...")<=GRID_W-16) break; } if(strlen(label)+3<sizeof(label)) strcat(label,"..."); } float w=pgf_text_width(label);
    vita2d_enable_clipping(); vita2d_set_clip_rectangle(x+8,y+84,x+GRID_W-8,y+110);
    pgf_draw_text(x+MAX(8,(GRID_W-w)/2),y+86,i==rel_pos?FOCUS_COLOR:FILE_COLOR,label);
    vita2d_disable_clipping();
  }
}
static void drawControlHints(void) {
  // The "Confirm button" setting swaps A and B.
  int a_confirms = enter_button == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;

  const char *normal[]={a_confirms?"A Open":"B Open",a_confirms?"B Back":"A Back","X Menu","Y Select","+ Settings","- FTP"};
  const char *select[]={"Copy","Move","Rename","Delete","All","Done"};
  const char *paste[]={"Open","Back","Paste here","Cancel copy","Settings","FTP"};
  int pasting=!touch_select && copy_list.length>0;
  vita2d_draw_rectangle(0,HINT_BAR_TOP,SCREEN_WIDTH,SCREEN_HEIGHT-HINT_BAR_TOP,panels[palette]);
  char info[128]; if(ftpvita_is_initialized()) snprintf(info,sizeof(info),"FTP %s %s:%u",ftpvita_is_recovering()?"RECOVERING":"ACTIVE",vita_ip,(unsigned)vita_port); else snprintf(info,sizeof(info),"FTP Offline");
  pgf_draw_text(SHELL_MARGIN_X,HINT_BAR_TOP,ftpvita_is_initialized()?0xFF83D49A:DATE_TIME_COLOR,info);
  char count[64]; snprintf(count,sizeof(count),"%d selected",mark_list.length);
  if(touch_select) pgf_draw_text(SCREEN_WIDTH-SHELL_MARGIN_X-pgf_text_width(count),HINT_BAR_TOP,accents[palette],count);
  else if(ftpvita_is_initialized()) { unsigned clients; uint64_t up,down; ftpvita_metrics_snapshot(&clients,&up,&down); snprintf(count,sizeof(count),"%u clients | UP %.1f | DOWN %.1f MiB",clients,(double)up/1048576.0,(double)down/1048576.0); if(pgf_text_width(info)+pgf_text_width(count)<SCREEN_WIDTH-60) pgf_draw_text(SCREEN_WIDTH-SHELL_MARGIN_X-pgf_text_width(count),HINT_BAR_TOP,DATE_TIME_COLOR,count); }
  for(int n=0;n<6;n++) { float w=SCREEN_WIDTH/6.0f; const char *label=touch_select?select[n]:pasting?paste[n]:normal[n]; unsigned int color=touch_select&&n==3?0xFF6868FF:FOCUS_COLOR; if(touch_select&&n<4&&(!mark_list.length||(n==2&&mark_list.length!=1))) color=DATE_TIME_COLOR; roundedBox(n*w+5,HINT_BAR_TOP+27,w-10,29,5,backgrounds[palette]); pgf_draw_text(n*w+(w-pgf_text_width(label))/2,HINT_BAR_TOP+29,color,label); }

}
#endif

int browserMain() {
  // Position
  memset(base_pos_list, 0, sizeof(base_pos_list));
  memset(rel_pos_list, 0, sizeof(rel_pos_list));

  // Paths
  memset(cur_file, 0, sizeof(cur_file));
  memset(archive_path, 0, sizeof(archive_path));

  // File lists
  memset(&file_list, 0, sizeof(FileList));
  memset(&mark_list, 0, sizeof(FileList));
  memset(&copy_list, 0, sizeof(FileList));
  memset(&install_list, 0, sizeof(FileList));

  // Current path is 'home'
  strcpy(file_list.path, HOME_PATH);

  if (use_custom_config) {
    // Last dir
    char lastdir[MAX_PATH_LENGTH];
    memset(lastdir, 0, sizeof(lastdir));
    ReadFile(VITASHELL_LASTDIR, lastdir, sizeof(lastdir) - 1);
    change_to_directory(lastdir);
  }

  // Refresh file list
  refreshFileList();

  // Init settings menu
  initSettingsMenu();
#ifdef __SWITCH2__
  char theme_pref='0'; ReadFile(THEME_PREF,&theme_pref,1); palette=theme_pref>='0'&&theme_pref<='3'?theme_pref-'0':0; applyPalette();
  char view_pref='0'; ReadFile(VIEW_PREF,&view_pref,1); grid_view=view_pref=='1';
  if(grid_view) gridFocus(base_pos+rel_pos);
#endif

  while (1) {
    readPad();

#ifdef __SWITCH2__
    // HOME's "close software": nested viewers have already returned.
    if (switch2QuitRequested())
      break;
#endif

    int refresh = REFRESH_MODE_NONE;
#ifdef __SWITCH2__
    float tx, ty;
    int gesture=switch2TakeTap(&tx,&ty);
    if(strcmp(last_tap_path,file_list.path)!=0) { last_tap_pos=-1; touch_select=0; search_query[0]=0; snprintf(last_tap_path,sizeof(last_tap_path),"%s",file_list.path); }
    for(int k=0;k<PAD_N_BUTTONS;k++) if(pressed_pad[k]) { last_tap_pos=-1; break; }
    if (!browserHasFocus() || isImeDialogRunning()) { if(gesture==1 && getContextMenuMode()!=CONTEXT_MENU_CLOSED && !isImeDialogRunning() && getDialogStep()==DIALOG_STEP_NONE) contextMenuTap(tx,ty); gesture=0; last_tap_pos=-1; }
    if(search_pending) {
      int result=updateImeDialog();
      if(result==IME_DIALOG_RESULT_FINISHED) {
        snprintf(search_query,sizeof(search_query),"%s",(char*)getImeDialogInputTextUTF8());
        search_found=0; if(search_query[0]) { FileListEntry *e=fileListGetNthEntry(&file_list,0); for(;e;e=e->next) if(strcasestr(e->name,search_query)) { setFocusOnFilename(e->name); search_found=1; break; } }
      }
      if(result!=IME_DIALOG_RESULT_RUNNING) search_pending=0;
      gesture=0;
    }
    if (gesture && browserHasFocus()) {
      if(gesture==1 && ty>=513 && tx>=0 && tx<SCREEN_WIDTH) { int slot=(int)(tx/(SCREEN_WIDTH/6.0f)); if(touch_select) { if(slot==5) { touch_select=0; fileListEmpty(&mark_list); } else if(slot==4) { fileListEmpty(&mark_list); for(FileListEntry *e=file_list.head;e;e=e->next) if(strcmp(e->name,DIR_UP)!=0 && dir_level>0) fileListAddEntry(&mark_list,fileListCopyEntry(e),SORT_NONE); } else if(mark_list.length>0 && dir_level>0) { setFocusOnFilename(mark_list.head->name); setContextMenuMainVisibilities(); int names[]={COPY,MOVE,RENAME,DELETE}; for(int j=0;j<context_menu_main.n_entries;j++) if(context_menu_main.entries[j].name==names[slot] && context_menu_main.entries[j].visibility==CTX_VISIBLE && (slot!=2 || mark_list.length==1)) { context_menu_main.callback(j,context_menu_main.context); if(slot<2) { touch_select=0; fileListEmpty(&mark_list); } break; } } } else if(copy_list.length>0 && slot==2) { if(dir_level>0 && !isInArchive() && file_list.length>0) { setContextMenuMainVisibilities(); for(int j=0;j<context_menu_main.n_entries;j++) if(context_menu_main.entries[j].name==PASTE && context_menu_main.entries[j].visibility==CTX_VISIBLE) { context_menu_main.callback(j,context_menu_main.context); break; } } } else if(copy_list.length>0 && slot==3) { fileListEmpty(&copy_list); } else { int keys[]={PAD_ENTER,PAD_CANCEL,PAD_TRIANGLE,PAD_SQUARE,PAD_START,PAD_SELECT}; if(slot==3) touch_select=1; else pressed_pad[keys[slot]]=1; } last_tap_pos=-1; }
      else if(gesture==1 && ty>=START_Y-FONT_Y_SPACE && ty<START_Y && tx>=SHELL_MARGIN_X && tx<180) { nextPalette(); }
      else if(gesture>=3) { int pos=base_pos+rel_pos+(gesture==3?1:-1)*(grid_view?GRID_COLS:4); if(grid_view) gridFocus(pos); else if(file_list.length>0) { pos=MAX(0,MIN(pos,file_list.length-1)); base_pos=MAX(0,MIN(pos,MAX(0,file_list.length-MAX_ENTRIES))); rel_pos=pos-base_pos; } last_tap_pos=-1; }
      else if(gesture==1 && ty>=START_Y-FONT_Y_SPACE && ty<START_Y && tx>=SCREEN_WIDTH-340 && tx<SCREEN_WIDTH-220) { search_pending=initImeDialog("Find file", "", 30, 0, 0, 0)>=0; last_tap_pos=-1; }
      else if(gesture==1 && tx>=SCREEN_WIDTH-220 && tx<SCREEN_WIDTH-SHELL_MARGIN_X && ty>=START_Y-FONT_Y_SPACE && ty<START_Y) { changeView(tx>=SCREEN_WIDTH-120); }
      else if(tx>=SHELL_MARGIN_X && tx<SCREEN_WIDTH-SHELL_MARGIN_X && ty>=START_Y && ty<START_Y+(grid_view?3*GRID_H:MAX_ENTRIES*FONT_Y_SPACE)) {
        int row=grid_view ? (int)((ty-START_Y)/GRID_H)*GRID_COLS+(int)((tx-SHELL_MARGIN_X)/GRID_W) : (int)((ty-START_Y)/FONT_Y_SPACE);
        if((!grid_view || (int)((tx-SHELL_MARGIN_X)/GRID_W)<GRID_COLS) && ty<HINT_BAR_TOP && row>=0 && row<(grid_view?GRID_COUNT:MAX_POSITION) && base_pos+row<file_list.length) { rel_pos=row; scroll_count=0; scroll_x=FILE_X; int pos=base_pos+row; uint64_t now=sceKernelGetProcessTimeWide(); if(gesture==2) { pressed_pad[PAD_TRIANGLE]=1; last_tap_pos=-1; } else if(touch_select) { FileListEntry *selected=fileListGetNthEntry(&file_list,pos); if(selected && strcmp(selected->name,DIR_UP)!=0) pressed_pad[PAD_SQUARE]=1; last_tap_pos=-1; } else if(last_tap_pos==pos && now-last_tap_time<350000) { pressed_pad[PAD_ENTER]=1; last_tap_pos=-1; } else { last_tap_pos=pos; last_tap_time=now; } }
      }
    }
#endif

    // Control
    if (getDialogStep() != DIALOG_STEP_NONE) {
      refresh = dialogSteps();
      // scroll_count = 0;
#ifndef __SWITCH2__
    } else if (getAdhocDialogStatus() != ADHOC_DIALOG_CLOSED) {
      adhocDialogCtrl();
#endif
    } else if (getPropertyDialogStatus() != PROPERTY_DIALOG_CLOSED) {
      propertyDialogCtrl();
      scroll_count = 0;
    } else if (getSettingsMenuStatus() != SETTINGS_MENU_CLOSED) {
      settingsMenuCtrl();
    } else if (getContextMenuMode() != CONTEXT_MENU_CLOSED) {
      contextMenuCtrl();
    } else {
#ifdef __SWITCH2__
      if(!search_pending)
#endif
      refresh = fileBrowserMenuCtrl();
    }

#ifndef __SWITCH2__
    // Receive system event
    SceAppMgrSystemEvent event;
    sceAppMgrReceiveSystemEvent(&event);

    // Refresh on app resume
    if (event.systemEvent == SCE_APPMGR_SYSTEMEVENT_ON_RESUME) {
      sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_USB_CONNECTION);
      pfsUmount(); // umount game data at resume
      refresh = REFRESH_MODE_NORMAL;
    }
#endif
    if (refresh != REFRESH_MODE_NONE) {
      // Refresh lists
      refreshFileList();
      refreshMarkList();
      refreshCopyList();

      // Focus
      if (refresh == REFRESH_MODE_SETFOCUS)
        setFocusOnFilename(focus_name);
    }

 #ifdef __SWITCH2__
    if(grid_view) gridFocus(base_pos+rel_pos);
#endif
    // Start drawing
    startDrawing(bg_browser_image);
#ifdef __SWITCH2__
    vita2d_draw_rectangle(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,backgrounds[palette]);
#endif

    // Draw
    drawShellInfo(file_list.path);
#ifdef __SWITCH2__
    float hy=START_Y-FONT_Y_SPACE;
    vita2d_draw_rectangle(0,hy,SCREEN_WIDTH,FONT_Y_SPACE,panels[palette]);
    const char *theme_names[]={"Ocean Blue","Purple","Emerald","Sunset"};
    pgf_draw_text(SHELL_MARGIN_X+3,hy,accents[palette],theme_names[palette]);
    vita2d_enable_clipping(); vita2d_set_clip_rectangle(185,hy,SCREEN_WIDTH-345,START_Y);
    if(search_query[0]) { char result[180]; snprintf(result,sizeof(result),"%s: %s",search_found?"Found":"No match",search_query); pgf_draw_text(190,hy,DATE_TIME_COLOR,result); }
    else { char count[64]; snprintf(count,sizeof(count),"%d items",MAX(0,file_list.length-(dir_level>0?1:0))); pgf_draw_text(190,hy,DATE_TIME_COLOR,count); }
    vita2d_disable_clipping();
    roundedBox(SCREEN_WIDTH-340,hy,115,FONT_Y_SPACE,4,backgrounds[palette]);
    roundedBox(SCREEN_WIDTH-220,hy,95,FONT_Y_SPACE,4,grid_view?backgrounds[palette]:shade(accents[palette],55));
    roundedBox(SCREEN_WIDTH-120,hy,100,FONT_Y_SPACE,4,grid_view?shade(accents[palette],55):backgrounds[palette]);
    pgf_draw_text(SCREEN_WIDTH-310,hy,FOCUS_COLOR,"Find");
    pgf_draw_text(SCREEN_WIDTH-210,hy,FOCUS_COLOR,"L List");
    pgf_draw_text(SCREEN_WIDTH-110,hy,FOCUS_COLOR,"R Grid");
#endif
    drawScrollBar(base_pos, file_list.length);
#ifdef __SWITCH2__
    if (browserHasFocus())
      drawControlHints();
#endif

    // Draw
#ifdef __SWITCH2__
    if(grid_view) drawGrid(); else
#endif
    {
    FileListEntry *file_entry = fileListGetNthEntry(&file_list, base_pos);
    if (file_entry) {
      int i;
      for (i = 0; i < MAX_ENTRIES && (base_pos + i) < file_list.length; i++) {
        uint32_t color = FILE_COLOR;
        float y = START_Y + (i * FONT_Y_SPACE);

        vita2d_texture *icon = NULL;
        if (file_entry->is_symlink) {
          if (file_entry->symlink->to_file) {
            color = FILE_SYMLINK_COLOR;
            icon = file_symlink_icon;
          } else {
            color = FOLDER_SYMLINK_COLOR;
            icon = folder_symlink_icon;
          }
        }
        // Folder
        else if (file_entry->is_folder) {
          color = FOLDER_COLOR;
          icon = folder_icon;
        } else {
          switch (file_entry->type) {
            case FILE_TYPE_BMP:
            case FILE_TYPE_PNG:
            case FILE_TYPE_JPEG:
              color = IMAGE_COLOR;
              icon = image_icon;
              break;
              
            case FILE_TYPE_VPK:
            case FILE_TYPE_ARCHIVE:
              color = ARCHIVE_COLOR;
              icon = archive_icon;
              break;
              
            case FILE_TYPE_MP3:
            case FILE_TYPE_OGG:
              color = IMAGE_COLOR;
              icon = audio_icon;
              break;
              
            case FILE_TYPE_SFO:
              color = SFO_COLOR;
              icon = sfo_icon;
              break;
            
            case FILE_TYPE_INI:
            case FILE_TYPE_TXT:
            case FILE_TYPE_XML:
              color = TXT_COLOR;
              icon = text_icon;
              break;
              
            default:
              color = FILE_COLOR;
              icon = file_icon;
              break;
          }
        }

#ifdef __SWITCH2__
        // A subtle full-row focus background, independent of file type.
        if (i == rel_pos) {
          vita2d_draw_rectangle(SHELL_MARGIN_X - 4.0f, y, MARK_WIDTH + 8.0f, FONT_Y_SPACE, ((accents[palette]&0xffffff)|0x50000000));
          vita2d_draw_rectangle(SHELL_MARGIN_X - 4.0f, y, 3.0f, FONT_Y_SPACE, accents[palette]);
        }
#endif
        // Draw icon
#ifdef __SWITCH2__
        drawModernIcon(file_entry,SHELL_MARGIN_X,y+2,22);
#else
        if (icon) vita2d_draw_texture(icon,SHELL_MARGIN_X,y+3.0f);
#endif

        // Current position
        if (i == rel_pos)
          color = FOCUS_COLOR;

        // Marked
        if (fileListFindEntry(&mark_list, file_entry->name))
          vita2d_draw_rectangle(SHELL_MARGIN_X, y + 3.0f, MARK_WIDTH, FONT_Y_SPACE, MARKED_COLOR);

 #ifdef __SWITCH2__
        if(touch_select && strcmp(file_entry->name,DIR_UP)!=0) { vita2d_draw_rectangle(SHELL_MARGIN_X,y+5,16,16,0xFF7C7167); vita2d_draw_rectangle(SHELL_MARGIN_X+2,y+7,12,12,fileListFindEntry(&mark_list,file_entry->name)?accents[palette]:panels[palette]); }
#endif
        // Draw file name
        vita2d_enable_clipping();
        vita2d_set_clip_rectangle(FILE_X + 1.0f, y, FILE_X + 1.0f + MAX_NAME_WIDTH, y + FONT_Y_SPACE);

        float x = FILE_X;

        char file_name[MAX_PATH_LENGTH];
        memset(file_name, 0, sizeof(file_name));

        if (file_entry->is_symlink) {
          snprintf(file_name, MAX_PATH_LENGTH, "%s  → %s",
                   file_entry->name, file_entry->symlink->target_path);
        } else {
          snprintf(file_name,sizeof(file_name),"%s",file_entry->name);
        }
        if (i == rel_pos) {
          int width = (int)pgf_text_width(file_name);
          if (width >= MAX_NAME_WIDTH) {
            if (scroll_count < 60) {
              scroll_x = x;
            } else if (scroll_count < width + 90) {
              scroll_x--;
            } else if (scroll_count < width + 120) {
              color = (color & 0x00FFFFFF) | ((((color >> 24) * (scroll_count - width - 90)) / 30) << 24); // fade-in in 0.5s
              scroll_x = x;
            } else {
              scroll_count = 0;
            }
            
            scroll_count++;
            
            x = scroll_x;
          }
        }

        pgf_draw_text(x, y, color, file_name);

        vita2d_disable_clipping();

        // File information
        if (strcmp(file_entry->name, DIR_UP) != 0) {
          if (dir_level == 0) {
            char used_size_string[16], max_size_string[16];
            int max_size_x = ALIGN_RIGHT(INFORMATION_X, pgf_text_width("0000.00 MB"));
            int separator_x = ALIGN_RIGHT(max_size_x, pgf_text_width("  /  "));
            if (file_entry->size != 0 && file_entry->size2 != 0) {
              getSizeString(used_size_string, file_entry->size2 - file_entry->size);
              getSizeString(max_size_string, file_entry->size2);
            } else {
              strcpy(used_size_string, "-");
              strcpy(max_size_string, "-");
            }
            
            float x = ALIGN_RIGHT(INFORMATION_X, pgf_text_width(max_size_string));
            pgf_draw_text(x, y, color, max_size_string);
            pgf_draw_text(separator_x, y, color, "  /");
            x = ALIGN_RIGHT(separator_x, pgf_text_width(used_size_string));
            pgf_draw_text(x, y, color, used_size_string);
          } else {
            char *str = NULL;
            char string[16];
            if (!file_entry->is_folder) {
              // Folder/size
              getSizeString(string, file_entry->size);
              str = string;
            } else {
              str = language_container[FOLDER];
            }
            pgf_draw_text(ALIGN_RIGHT(INFORMATION_X, pgf_text_width(str)), y, color, str);
          }

          // Date
          char date_string[16];
          getDateString(date_string, date_format, &file_entry->mtime);

          char time_string[24];
          getTimeString(time_string, time_format, &file_entry->mtime);

          char string[64];
          sprintf(string, "%s %s", date_string, time_string);

          float x = ALIGN_RIGHT(SCREEN_WIDTH - SHELL_MARGIN_X, pgf_text_width(string));
          pgf_draw_text(x, y, color, string);
        }

        // Next
        file_entry = file_entry->next;
      }
    }

    }
    // Draw
    drawSettingsMenu();
    drawContextMenu();
#ifndef __SWITCH2__
    drawAdhocDialog();
#endif
    drawPropertyDialog();

    // End drawing
    endDrawing();
  }

  // Empty lists
  fileListEmpty(&copy_list);
  fileListEmpty(&mark_list);
  fileListEmpty(&file_list);
  fileListEmpty(&install_list);

  return 0;
}
