#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"
#include "console.h"
#include "rtc.h"
#include "aht20.h"
#include "board.h"
#include "elog.h"

static void board_lowlevel_close(void)  
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, DISABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, DISABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, DISABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, DISABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, DISABLE);  
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, DISABLE);
#if IS_DEBUG_VERSION == 0
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, DISABLE);  
#endif
}

static void board_lowlevel_start(void)  
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);  
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
#if IS_DEBUG_VERSION == 0
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
#endif
}

void per_sleep_processing(void)  
{
    board_lowlevel_close();  
}

void post_sleep_processing(void)  
{
    board_lowlevel_start();  
}

/**
* @brief 初始化外设时钟
*/
void board_lowlevel_init(void)  
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);  
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);  
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);  
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);  
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);  
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);  
    PWR_BackupAccessCmd(ENABLE);  
    RCC_LSEConfig(RCC_LSE_ON);  
    while(RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);  
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);  
}

/**
 * @brief 初始化片上外设
 */
void board_init(void)  
{
#if  IS_DEBUG_VERSION  // 调试版本，初始化调试串口和EasyLogger
    console_init();  
    console_EasyLogger_Init();  
    elog_i("SYS", "Build\r\n");  
#endif
    rtc_init();  // 初始化RTC
    aht20_init();  // 初始化AHT20
}

int fputc(int ch, FILE *f)  
{
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}

void vAssertCalled(const char *file, int line)  
{
    portDISABLE_INTERRUPTS();  
#if  IS_DEBUG_VERSION
    elog_a("ASSERT", "Assert Called: %s(%d)\r\n", file, line);  
#endif	
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)  
{
#if  IS_DEBUG_VERSION
    elog_e("OVERFLOWED", "Stack Overflowed: %s\r\n", pcTaskName);  
#endif
    configASSERT(0);  
}

void vApplicationMallocFailedHook(void)  
{
#if  IS_DEBUG_VERSION
    elog_e("MEMORY", "Malloc Failed\r\n");  
#endif
    configASSERT(0);  
}
