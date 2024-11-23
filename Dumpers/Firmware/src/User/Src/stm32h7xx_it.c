#include "main.h"
#include "stm32h7xx_it.h"
#include "stm32h7xx_ll_dma.h"


/*
   LED error blink codes:
   ======================

   1x short, 1x long: NMI Exception
   2x short, 1x long: Hardfault
   3x short, 1x long: MemManage
   4x short, 1x long: BusFault
   5x short, 1x long: UsageFault

   32 blinks, short/long combined:
     -> address of caller of ErrorHandler function,
        MSB->LSB, short=0, long=1
 */

void Blink_Handler(uint32_t n, int count) {
  __disable_irq();
  BSP_LED_Init(LED_BLUE);
  while(1) {
    for(int i=count; i>=0; i--) {
      int ontime = (n & (1<<i)) ? 17000000 : 3000000;
      int offtime = 20000000 - ontime;
      BSP_LED_On(LED_BLUE);
      for(volatile int j=0; j<ontime; j++);
      BSP_LED_Off(LED_BLUE);
      for(volatile int j=0; j<offtime; j++);
    }
    for(volatile int j=0; j<60000000; j++);
  }
}

/******************************************************************************/
/*            Cortex-M7 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief   This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
  Blink_Handler(1, 2);
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  Blink_Handler(1, 3);
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  Blink_Handler(1, 4);
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  Blink_Handler(1, 5);
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  Blink_Handler(1, 6);
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
void SVC_Handler(void)
{
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
void PendSV_Handler(void)
{
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
  HAL_SYSTICK_IRQHandler();
}

/******************************************************************************/
/*                 STM32H7xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32h7xx.s).                                               */
/******************************************************************************/

/**
  * @brief  On Error Handler on condition TRUE.
  * @param  condition : Can be TRUE or FALSE
  * @retval None
  */
void Error_Handler(uint32_t condition)
{
  uint32_t retaddr;
  __asm ("MOV %[result], r14"
      : [result] "=r" (retaddr)
    );
  if(condition)
  {
    Blink_Handler(retaddr, 32);
  }
}

/**
  * @brief This function handles USART3 global interrupt.
  */
void USART3_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart3);
}

/**
  * @brief This function handles EXTI15_10 interrupts.
  */
void EXTI15_10_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
}

/**
  * @brief This function handles SDMMC1 global interrupt.
  */
void SDMMC1_IRQHandler(void)
{
  HAL_SD_IRQHandler(&hsd1);
}

/**
  * @brief This function handles USB On The Go FS global interrupt.
  */
void OTG_FS_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS);
}

/**
  * @brief This function handles TIM1 update interrupt.
  */
void TIM1_UP_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim1);
}
