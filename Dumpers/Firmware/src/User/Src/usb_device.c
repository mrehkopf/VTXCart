#include "main.h"
#include "usb_device.h"

extern uint8_t UserTxBufferFS[APP_TX_DATA_SIZE];

void BSP_USB_DEVICE_Init(void)
{
  /* Init Device Library, add supported class and start the library. */
  Error_Handler(USBD_Init(&hUsbDeviceFS, &FS_Desc, DEVICE_FS) != USBD_OK);
  Error_Handler(USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC) != USBD_OK);
  Error_Handler(USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS) != USBD_OK);
  Error_Handler(USBD_Start(&hUsbDeviceFS) != USBD_OK);

  /* USER CODE BEGIN USB_DEVICE_Init_PostTreatment */
  HAL_PWREx_EnableUSBVoltageDetector();
}

void USB_printf(const char *format, ...)
{
  va_list args;
  uint32_t length;
  uint32_t timer;
  uint8_t tmpbuf[sizeof UserTxBufferFS];

  timer = HAL_GetTick();
  va_start(args, format);
  length = vsnprintf((char *)tmpbuf, APP_TX_DATA_SIZE, (char *)format, args);
  va_end(args);

  uint8_t *tgt = UserTxBufferFS;
  int count = 0;
  for(uint8_t *ch = tmpbuf; *ch; ch++) {
    if(*ch == '\n') {
      *tgt++ = '\r';
      length++;
    }
    *tgt++ = *ch;
    count++;
    if(count >= sizeof(UserTxBufferFS)) {
      break;
    }
  }

  while(HAL_GetTick() - timer < 10)
  {
    if (CDC_Transmit_FS(UserTxBufferFS, length) == USBD_OK)
    {
      return;
    }
  }
}
