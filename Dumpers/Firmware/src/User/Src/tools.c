#include "main.h"
#include "tools.h"


char *fresult_names[] = { "FR_OK", "FR_DISK_ERR", "FR_INT_ERR",
  "FR_NOT_READY", "FR_NO_FILE", "FR_NO_PATH", "FR_INVALID_NAME",
  "FR_DENIED", "FR_EXIST", "FR_INVALID_OBJECT", "FR_WRITE_PROTECTED",
  "FR_INVALID_DRIVE", "FR_NOT_ENABLED", "FR_NO_FILESYSTEM", "FR_MKFS_ABORTED",
  "FR_TIMEOUT", "FR_LOCKED", "FR_NOT_ENOUGH_CORE", "FR_TOO_MANY_OPEN_FILES",
  "FR_INVALID_PARAMETER" };

char *fresult_friendly_names[] = { "No error", "Card I/O error", "FS driver int. error",
  "Drive not ready", "File not found", "Directory not found", "Invalid path name",
  "Access denied", "Acc. denied (exists)", "Invalid file object", "Write protected",
  "Invalid drive", "No work area", "No valid file system", "mkfs() aborted",
  "Drive timeout", "Shared access locked", "Not enough memory", "Too many open files",
  "Invalid parameter" };

char *get_fresult_name(FRESULT res) {
  return fresult_names[res];
}

char *get_fresult_friendlyname(FRESULT res) {
  return fresult_friendly_names[res];
}

int check_fresult(FRESULT res, char *message_format, ...) {
  va_list ap;

  if(res != FR_OK) {
    va_start(ap, message_format);
    LCD_vprintf(1, message_format, ap);
    va_end(ap);
    LCD_printf(1, "%s\n", get_fresult_friendlyname(res));
    waitButton();
    return 1;
  };
  return 0;
}

char const *get_chip_name (void)
{
  char const *chip_name;

  switch (cur_chip) {
    case CHIP_P:
      chip_name = CHIP_NAME_P;
      break;
    case CHIP_S:
      chip_name = CHIP_NAME_S;
      break;
    case CHIP_M:
      chip_name = CHIP_NAME_M;
      break;
    case CHIP_C:
      chip_name = CHIP_NAME_C;
      break;
    case CHIP_V:
      chip_name = CHIP_NAME_V;
      break;
    default:
      chip_name = 0;
      break;
  }

  return chip_name;
}

char const *get_dump_filename (void)
{
  char const *dump_filename;

  switch (cur_chip) {
    case CHIP_P:
      dump_filename = DUMP_FILENAME_P;
      break;
    case CHIP_S:
      dump_filename = DUMP_FILENAME_S;
      break;
    case CHIP_M:
      dump_filename = DUMP_FILENAME_M;
      break;
    case CHIP_C:
      dump_filename = DUMP_FILENAME_C;
      break;
    case CHIP_V:
      dump_filename = DUMP_FILENAME_V;
      break;
    default:
      dump_filename = 0;
      break;
  }

  return dump_filename;
}

uint32_t get_end_address (void)
{
  uint32_t end_address;

  switch (cur_chip) {
    case CHIP_P:
      end_address = END_ADDRESS_P;
      break;
    case CHIP_S:
      end_address = END_ADDRESS_S;
      break;
    case CHIP_M:
      end_address = END_ADDRESS_M;
      break;
    case CHIP_C:
      end_address = END_ADDRESS_C;
      break;
    case CHIP_V:
      end_address = END_ADDRESS_V;
      break;
    default:
      end_address = 0;
      break;
  }

  return end_address;
}

void waitButton() {
  while (!(flag_button & FLAG_BTN_BRD)) {
    __WFI();
  };
  flag_button &= ~FLAG_BTN_BRD;
}

FRESULT saveProgress(uint32_t addr, const char *filename, chip_t chiptype) {
  FIL fp;
  FRESULT res;
  if((res = f_open(&fp, PROG_SAVE_FILE, FA_CREATE_ALWAYS | FA_WRITE)) != FR_OK) {
    return res;
  }
  f_write(&fp, &addr, sizeof(addr), NULL);
  f_write(&fp, &chiptype, sizeof(chiptype), NULL);
  f_puts(filename, &fp);
  f_close(&fp);
  return 0;
}

FRESULT loadProgress(uint32_t *addr, char *filename, chip_t *chiptype) {
  FIL fp;
  FRESULT res;
  UINT bytesread;

  if((res = f_open(&fp, PROG_SAVE_FILE, FA_READ)) != FR_OK) {
    return res;
  }

  f_read(&fp, addr, sizeof(uint32_t), &bytesread);
  if(bytesread < sizeof(uint32_t)){
    res = 1;
  }
  f_read(&fp, chiptype, sizeof(chip_t), &bytesread);
  if(bytesread < sizeof(chip_t)){
    res = 1;
  }
  f_gets(filename, 80, &fp);
  f_close(&fp);

  return res;
}

int waitYesNo() {
  LCD_printf(0, "Short=NO Long=YES\n");
  while(1) {
    if(flag_button & FLAG_BTN_BRD) {
      flag_button &= ~FLAG_BTN_BRD;
      return 0;
    }
    if(flag_button & FLAG_BTN_BRD_LONG) {
      flag_button &= ~FLAG_BTN_BRD_LONG;
      return 1;
    }
  }
}