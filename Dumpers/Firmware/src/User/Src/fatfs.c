#include "main.h"
#include "fatfs.h"

void MX_FATFS_Init(void) {
	FATFS_LinkDriver(&SD_Driver, SDPath);
}

DWORD get_fattime(void) {
	return 0;
}
