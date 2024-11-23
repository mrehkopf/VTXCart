#include "main.h"
#include "menu.h"
#include "variables.h"


menu_entry MENU_PROM[] = {
  MENU_ENTRY_TITLE("P-ROM Actions:"),
  MENU_ENTRY_FUNC("Test", P_Test),
  MENU_ENTRY_FUNC("Erase", P_Erase),
  MENU_ENTRY_FUNC("Program", P_Program),
  MENU_ENTRY_FUNC("Verify", P_Verify),
  MENU_ENTRY_FUNC("Dump", P_Dump),
  MENU_ENTRY_FUNC("Line Capacitance", P_CapaView),
  MENU_ENTRY_EXIT(),
  MENU_ENTRY_TERM()
};

menu_entry MENU_SROM[] = {
  MENU_ENTRY_TITLE("S-ROM Actions:"),
  MENU_ENTRY_FUNC("Test", NULL),
  MENU_ENTRY_FUNC("Program", NULL),
  MENU_ENTRY_FUNC("Verify", NULL),
  MENU_ENTRY_FUNC("Dump", NULL),
  MENU_ENTRY_EXIT(),
  MENU_ENTRY_TERM()
};

menu_entry MENU_MROM[] = {
  MENU_ENTRY_TITLE("M-ROM Actions:"),
  MENU_ENTRY_FUNC("Test", NULL),
  MENU_ENTRY_FUNC("Program", NULL),
  MENU_ENTRY_FUNC("Verify", NULL),
  MENU_ENTRY_FUNC("Dump", NULL),
  MENU_ENTRY_EXIT(),
  MENU_ENTRY_TERM()
};

menu_entry MENU_CROM[] = {
  MENU_ENTRY_TITLE("C-ROM Actions:"),
  MENU_ENTRY_FUNC("Test", CV_Test),
  MENU_ENTRY_FUNC("Erase", CV_Erase),
  MENU_ENTRY_FUNC("Blank Check", CV_BlankCheck),
  MENU_ENTRY_FUNC("Program", C_Program),
  MENU_ENTRY_FUNC("Verify", C_Verify),
  MENU_ENTRY_FUNC("Dump", C_Dump),
  MENU_ENTRY_FUNC("Line Capacitance", CV_CapaView),
  MENU_ENTRY_FUNC("Read Stress Test", CV_ReadTest),
  MENU_ENTRY_EXIT(),
  MENU_ENTRY_TERM()
};

menu_entry MENU_VROM[] = {
  MENU_ENTRY_TITLE("V-ROM Actions:"),
  MENU_ENTRY_FUNC("Test", CV_Test),
  MENU_ENTRY_FUNC("Program", V_Program),
  MENU_ENTRY_FUNC("Verify", V_Verify),
  MENU_ENTRY_FUNC("Dump", V_Dump),
  MENU_ENTRY_FUNC("Line Capacitance", CV_CapaView),
  MENU_ENTRY_EXIT(),
  MENU_ENTRY_TERM()
};

menu_entry MENU_TOP[] = {
  MENU_ENTRY_TITLE("Select Chip Type:"),
  MENU_ENTRY_SUBMENU("P-ROM", MENU_PROM),
//  MENU_ENTRY_SUBMENU("S-ROM", MENU_SROM),
//  MENU_ENTRY_SUBMENU("M-ROM", MENU_MROM),
  MENU_ENTRY_SUBMENU("C-ROM", MENU_CROM),
  MENU_ENTRY_SUBMENU("V-ROM", MENU_VROM),
  MENU_ENTRY_TERM()
};

int print_center(int y, const char *text) {
  char line[LCD_COLS + 1];
  memset(line, ' ', LCD_COLS);
  line[LCD_COLS] = 0;

  int xstart = (LCD_COLS - (int)strlen(text)) / 2;
  if(xstart < 0) {
    xstart = 0;
  }
  int count = snprintf(line + xstart, LCD_COLS-xstart, "%s", text);
  if(count + xstart < LCD_COLS) {
    line[count + xstart] = ' ';
  }
  return LCD_xyprintf(0, y, 0, "%s", line);
}

int print_menu_entry(menu_entry *ent) {
  return print_center(2, ent->text);
}

int print_menu_title(menu_entry *ent) {
  return print_center(0, ent->text);
}

menu_entry *menu_select(menu_entry *ent) {
  menu_entry *title, *cur;
  title = ent;
  int index = 1;
  int refresh = 1;

  LCD_Clear();
  print_menu_title(title);

  while(1) {
    if(refresh) {
      refresh = 0;
      cur = ent + index;
      /* wrap around if after last entry */
      if((ent + index)->text == NULL) {
        index = 1;
      }
      cur = ent + index;
      print_menu_entry(cur);
    }
    __WFI();
    /* short press: next entry */
    if(flag_button & FLAG_BTN_BRD) {
      flag_button &= ~FLAG_BTN_BRD;
      index++;
      refresh = 1;
    }
    if(flag_button & FLAG_BTN_BRD_LONG) {
      flag_button &= ~FLAG_BTN_BRD_LONG;
      switch(cur->type) {
        case TYPE_SUBMENU:
          menu_select(cur->tgt);
          LCD_Clear();
          print_menu_title(ent);
          refresh = 1;
          break;

        case TYPE_FUNCTION:
          void *func = cur->tgt;
          ((void (*)(void))func)();
          LCD_Clear();
          print_menu_title(ent);
          refresh = 1;
          break;

        case TYPE_SELECT:
          return cur;
          break;

        case TYPE_EXIT:
          return NULL;
          break;

        default:
          break;
      }
    }
  }
}

FRESULT choose_file(FILINFO *fno, char *path, BYTE mode) {
  DIR dir;
  FRESULT res;
  int refresh = 1;
  LCD_xyprintf(0, 0, 0, "Choose File:");
  f_opendir(&dir, path);

  do {
    if(refresh) {
      res = f_readdir(&dir, fno);
      if (res != FR_OK || fno->fname[0] == 0) {
        f_rewinddir(&dir);
        continue;
      };
      if(!(fno->fattrib & AM_DIR)) {
        print_center(2, fno->fname);
        refresh = 0;
      }
    }
    if(flag_button & FLAG_BTN_BRD) {
      flag_button &= ~FLAG_BTN_BRD;
      refresh = 1;
    }
  } while (!(flag_button & FLAG_BTN_BRD_LONG));
  flag_button &= ~FLAG_BTN_BRD_LONG;
  return FR_OK;
}