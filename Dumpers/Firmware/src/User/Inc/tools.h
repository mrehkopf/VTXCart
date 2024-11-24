#ifndef __TOOLS__H__
#define __TOOLS__H__

#ifdef __cplusplus
 extern "C" {
#endif

char *get_fresult_name(FRESULT res);
char *get_fresult_friendlyname(FRESULT res);
int check_fresult(FRESULT res, char *message_format, ...);

char const *get_chip_name (void);
char const *get_dump_filename (void);
char const *get_prog_filename (void);
uint32_t get_end_address (void);

void waitButton(void);
int waitYesNo(void);

FRESULT saveProgress(uint32_t addr, const char *filename, chip_t chiptype);
FRESULT loadProgress(uint32_t *addr, char *filename, chip_t *chiptype);

#ifdef __cplusplus
}
#endif

#endif /* __TOOLS__H__ */
