#include "console.h"
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "elog.h"

static SemaphoreHandle_t write_async_semaphore;  // 写异步信号量

/**
 * @brief 初始化串口
 */
void console_init(void)
{
    write_async_semaphore = xSemaphoreCreateBinary();  
    configASSERT(write_async_semaphore);
    
    // 初始化USART
    USART_InitTypeDef USART_InitStructure;
    USART_StructInit(&USART_InitStructure);
    USART_InitStructure.USART_BaudRate = 115200u;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_Init(USART1, &USART_InitStructure);
    USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
    USART_Cmd(USART1, ENABLE);

    // 初始化DMA
    DMA_InitTypeDef DMA_InitStruct;
    DMA_StructInit(&DMA_InitStruct);
    DMA_InitStruct.DMA_Channel = DMA_Channel_4;
    DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
    DMA_InitStruct.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStruct.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStruct.DMA_Priority = DMA_Priority_Low;
    DMA_InitStruct.DMA_FIFOMode = DMA_FIFOMode_Enable;
    DMA_InitStruct.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStruct.DMA_MemoryBurst = DMA_MemoryBurst_INC8;
    DMA_InitStruct.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_ITConfig(DMA2_Stream7, DMA_IT_TC, ENABLE);
    DMA_Init(DMA2_Stream7, &DMA_InitStruct);

    // 初始化NVIC
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream7_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_SetPriority(DMA2_Stream7_IRQn, 5);

    // 初始化GPIO
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

/**
 * @brief 写入数据到串口
 * @param data 数据指针
 * @param len 数据长度
 */
void console_write(const char *data, uint32_t len)
{
    uint32_t remaining = len;
    const char *ptr = data;
 
    while (remaining > 0)
    {
        uint32_t chunk_size = remaining < 65535 ? remaining : 65535;
 
        DMA2_Stream7->M0AR = (uint32_t)ptr;
        DMA2_Stream7->NDTR = chunk_size;
        DMA_Cmd(DMA2_Stream7, ENABLE);
        xSemaphoreTake(write_async_semaphore, portMAX_DELAY);  // 等待DMA完成
 
        ptr += chunk_size;
        remaining -= chunk_size;
    }
 
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    USART_ClearFlag(USART1, USART_FLAG_TC);
}

void console_EasyLogger_Init(void) 
{
    // 1. 初始化EasyLogger
    elog_init();

    // 2. 使用EasyLogger的格式
    // 例如：显示所有信息，显示时间，标签，函数，行号，目录
    elog_set_fmt(ELOG_LVL_ASSERT, ELOG_FMT_ALL);
    elog_set_fmt(ELOG_LVL_ERROR, ELOG_FMT_LVL | ELOG_FMT_TIME | ELOG_FMT_TAG | ELOG_FMT_FUNC | ELOG_FMT_LINE | ELOG_FMT_DIR);
    elog_set_fmt(ELOG_LVL_WARN, ELOG_FMT_LVL | ELOG_FMT_TIME | ELOG_FMT_TAG | ELOG_FMT_FUNC | ELOG_FMT_LINE | ELOG_FMT_DIR);
    elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME | ELOG_FMT_FUNC | ELOG_FMT_DIR | ELOG_FMT_LINE);
    elog_set_fmt(ELOG_LVL_DEBUG, ELOG_FMT_LVL | ELOG_FMT_TIME | ELOG_FMT_TAG | ELOG_FMT_FUNC | ELOG_FMT_LINE | ELOG_FMT_DIR);
    elog_set_fmt(ELOG_LVL_VERBOSE, ELOG_FMT_LVL);

    // 3. 启动EasyLogger
    elog_start();
}

/**
 * @brief DMA2 Stream7中断处理函数
 */
void DMA2_Stream7_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA2_Stream7, DMA_IT_TCIF7) == SET)
    {
        BaseType_t pxHigherPriorityTaskWoken;
        xSemaphoreGiveFromISR(write_async_semaphore, &pxHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
        
        DMA_ClearITPendingBit(DMA2_Stream7, DMA_IT_TCIF7);
    }
}
