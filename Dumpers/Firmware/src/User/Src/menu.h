#ifndef __MENU_H
#define __MENU_H

#define MENU_ENTRY_TITLE(x)     { .text = x, .type = TYPE_TITLE, .tgt = NULL }
#define MENU_ENTRY_FUNC(x,y)    { .text = x, .type = TYPE_FUNCTION, .tgt = y }
#define MENU_ENTRY_EXIT()       { .text = "- leave menu -", .type = TYPE_EXIT, .tgt = NULL }
#define MENU_ENTRY_SUBMENU(x,y) { .text = x, .type = TYPE_SUBMENU, .tgt = y }
#define MENU_ENTRY_SELECT(x,y)  { .text = x, .type = TYPE_SELECT, .tgt = y }
#define MENU_ENTRY_TERM()       { .text = NULL }

typedef enum menu_entry_type {
  TYPE_SUBMENU,
  TYPE_FUNCTION,
  TYPE_TITLE,
  TYPE_EXIT,
  TYPE_SELECT
} menu_entry_type;

typedef struct menu_entry {
  const char *text;
  const menu_entry_type type;
  void *tgt;
} menu_entry;

menu_entry *menu_select(menu_entry *ent);

extern menu_entry MENU_TOP[];

FRESULT choose_file(FILINFO *fno, char *path, BYTE mode);

#endif
