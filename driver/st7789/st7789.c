#include "st7789.h"
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include <stdbool.h>
#include <stddef.h>


#define CS_PORT     GPIOE
#define CS_PIN      GPIO_Pin_2
#define RESET_PORT  GPIOE
#define RESET_PIN   GPIO_Pin_3
#define DC_PORT     GPIOE
#define DC_PIN      GPIO_Pin_4
#define BL_PORT     GPIOE
#define BL_PIN      GPIO_Pin_5

static SemaphoreHandle_t write_gram_semaphore; // 写GRAM信号量

/**
 * @brief 通过SPI 轮询向显存写入数据
 * @param reg 寄存器地址
 * @param data 数据
 * @param length 数据长度
 */
static void st7789_write_register(uint8_t reg, uint8_t data[], uint16_t length)
{
    SPI_DataSizeConfig(SPI2, SPI_DataSize_8b);
    GPIO_ResetBits(CS_PORT, CS_PIN);
    GPIO_ResetBits(DC_PORT, DC_PIN);
    SPI_SendData(SPI2, reg);
    while (SPI_GetFlagStatus(SPI2, SPI_FLAG_TXE) == RESET);
    while (SPI_GetFlagStatus(SPI2, SPI_FLAG_BSY) != RESET);
    GPIO_SetBits(DC_PORT, DC_PIN);
    for (uint16_t i = 0; i < length; i++)
    {
        SPI_SendData(SPI2, data[i]);
        while (!SPI_GetFlagStatus(SPI2, SPI_FLAG_TXE));
    }
    while (SPI_GetFlagStatus(SPI2, SPI_FLAG_BSY) != RESET);
    GPIO_SetBits(CS_PORT, CS_PIN);
}

/**
* @brief 通过SPI DMA向显存写入数据
* @param data 数据
* @param length 数据长度
* @param singlecolor 是否单色
*/
static void st7789_write_gram(uint8_t data[], uint32_t length, bool singlecolor)
{
    SPI_DataSizeConfig(SPI2, SPI_DataSize_16b);  // 将SPI配置为16位传输
    GPIO_ResetBits(CS_PORT, CS_PIN);  // 拉低片选信号，启动SPI传输
    GPIO_SetBits(DC_PORT, DC_PIN);  // 拉高数据/命令信号，表示传输数据
    length >>= 1;  // 字节数除以2，得到像素个数（1个像素16位）
    do  // 分块传输（DMA一次最多传输65535个像素）
    {
        uint32_t chunk_size = length < 65535 ? length : 65535;  // 分块大小
        if (singlecolor)
            DMA1_Stream4->CR &= ~DMA_SxCR_MINC;  // 单色模式，关闭内存地址自增（反复读同一数据）
        else
            DMA1_Stream4->CR |= DMA_SxCR_MINC;  // 彩色模式，启用内存地址自增（读取下一像素数据）
        DMA1_Stream4->M0AR = (uint32_t)data;  // 设置DMA源地址
        DMA1_Stream4->NDTR = chunk_size;  // 设置DMA传输长度
        DMA_Cmd(DMA1_Stream4, ENABLE);  // 启动DMA传输
        xSemaphoreTake(write_gram_semaphore, portMAX_DELAY);  // 等待DMA传输完成
        if (!singlecolor)
            data += chunk_size * 2;  // 彩色模式，移动到下一个像素数据
        length -= chunk_size;  // 更新剩余数据长度
    } while (length > 0);  // 重复传输，直到所有数据都发送完毕
    while (SPI_GetFlagStatus(SPI2, SPI_FLAG_BSY) != RESET);  // 等待SPI传输完成
    GPIO_SetBits(CS_PORT, CS_PIN);  // 拉高片选信号，结束SPI传输
}

/**
* @brief 设置范围并准备GRAM
* @param x1 左上角x坐标
* @param y1 左上角y坐标
* @param x2 右下角x坐标
* @param y2 右下角y坐标
*/
static void st7789_set_range_and_prepare_gram(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    st7789_write_register(0x2A, (uint8_t[]){(x1 >> 8) & 0xff, x1 & 0xff, (x2 >> 8) & 0xff, x2 & 0xff}, 4);  // 设置列范围
    st7789_write_register(0x2B, (uint8_t[]){(y1 >> 8) & 0xff, y1 & 0xff, (y2 >> 8) & 0xff, y2 & 0xff}, 4);  // 设置行范围
    st7789_write_register(0x2C, NULL, 0);  // 之后送来的数据写入显存
}

/**
* @brief 初始化ST7789
*/
void st7789_init(void)
{
    // 初始化信号量
    write_gram_semaphore = xSemaphoreCreateBinary();
    configASSERT(write_gram_semaphore);
    
    // 初始化SPI
    SPI_InitTypeDef SPI_InitStruct;
    SPI_StructInit(&SPI_InitStruct);
    SPI_InitStruct.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStruct.SPI_Mode = SPI_Mode_Master;
    SPI_InitStruct.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStruct.SPI_CPOL = SPI_CPOL_Low;
    SPI_InitStruct.SPI_CPHA = SPI_CPHA_1Edge;
    SPI_InitStruct.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStruct.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    SPI_InitStruct.SPI_FirstBit = SPI_FirstBit_MSB;
    SPI_Init(SPI2, &SPI_InitStruct);
    SPI_DMACmd(SPI2, SPI_I2S_DMAReq_Tx, ENABLE);
    SPI_Cmd(SPI2, ENABLE);

    // 初始化DMA
    DMA_InitTypeDef DMA_InitStruct;
    DMA_StructInit(&DMA_InitStruct);
    DMA_InitStruct.DMA_Channel = DMA_Channel_0;
    DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t)&SPI2->DR;
    DMA_InitStruct.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStruct.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStruct.DMA_Priority = DMA_Priority_High;
    DMA_InitStruct.DMA_FIFOMode = DMA_FIFOMode_Enable;
    DMA_InitStruct.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStruct.DMA_MemoryBurst = DMA_MemoryBurst_INC8;
    DMA_InitStruct.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_ITConfig(DMA1_Stream4, DMA_IT_TC, ENABLE);
    DMA_Init(DMA1_Stream4, &DMA_InitStruct);

    // 初始化NVIC
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Stream4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_SetPriority(DMA1_Stream4_IRQn, 5);

    // 初始化GPIO
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_StructInit(&GPIO_InitStruct);
    GPIO_SetBits(GPIOE, GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5);
    GPIO_ResetBits(BL_PORT, BL_PIN);
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_Init(GPIOE, &GPIO_InitStruct);
    
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF_SPI2);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource2, GPIO_AF_SPI2);
    GPIO_PinAFConfig(GPIOC, GPIO_PinSource3, GPIO_AF_SPI2);

    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_13;
    GPIO_Init(GPIOB, &GPIO_InitStruct);
    GPIO_InitStruct.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_Init(GPIOC, &GPIO_InitStruct);

    // 复位ST7789
    GPIO_ResetBits(RESET_PORT, RESET_PIN);
    vTaskDelay(pdMS_TO_TICKS(2)); // 20us at least
    GPIO_SetBits(RESET_PORT, RESET_PIN);
    vTaskDelay(pdMS_TO_TICKS(120));

    // 初始化ST7789
    st7789_write_register(0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    st7789_write_register(0x36, (uint8_t[]){0x00}, 1);
    st7789_write_register(0x3A, (uint8_t[]){0x55}, 1);
    st7789_write_register(0xB7, (uint8_t[]){0x46}, 1);
    st7789_write_register(0xBB, (uint8_t[]){0x1B}, 1);
    st7789_write_register(0xC0, (uint8_t[]){0x2C}, 1);
    st7789_write_register(0xC2, (uint8_t[]){0x01}, 1);
    st7789_write_register(0xC4, (uint8_t[]){0x20}, 1);
    st7789_write_register(0xC6, (uint8_t[]){0x0F}, 1);
    st7789_write_register(0xD0, (uint8_t[]){0xA4,0xA1}, 2);
    st7789_write_register(0xD6, (uint8_t[]){0xA1}, 1);
    st7789_write_register(0xE0, (uint8_t[]){0xF0,0x00,0x06,0x04,0x05,0x05,0x31,0x44,0x48,0x36,0x12,0x12,0x2B,0x34}, 14);
    st7789_write_register(0xE0, (uint8_t[]){0xF0,0x0B,0x0F,0x0F,0x0D,0x26,0x31,0x43,0x47,0x38,0x14,0x14,0x2C,0x32}, 14);
    st7789_write_register(0x21, NULL, 0);
    st7789_write_register(0x29, NULL, 0);
    st7789_fill_color(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1, 0x0000);
    GPIO_WriteBit(BL_PORT, BL_PIN, Bit_SET);
}

/**
 * @brief 填充颜色
 * @param x1 左上角x坐标
 * @param y1 左上角y坐标
 * @param x2 右下角x坐标
 * @param y2 右下角y坐标
 * @param color 颜色
 */
void st7789_fill_color(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    if (x1 >= ST7789_WIDTH || y1 >= ST7789_HEIGHT)
        return;
    if (x2 >= ST7789_WIDTH || y2 >= ST7789_HEIGHT)
        return;
    if (x1 > x2 || y1 > y2)
        return;

    st7789_set_range_and_prepare_gram(x1, y1, x2, y2);
    
    uint32_t pixels = (x2 - x1 + 1) * (y2 - y1 + 1);
    st7789_write_gram((uint8_t *)&color, pixels * 2, true);
}

/**
* @brief 刷新窗口（LVGL）
* @param x1 窗口左上角x坐标
* @param y1 窗口左上角y坐标
* @param x2 窗口右下角x坐标
* @param y2 窗口右下角y坐标
* @param pixels 像素数据
* @param length 像素数据长度
*/
void st7789_write_pixels(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
    const uint8_t *pixels, uint32_t length)
{
    st7789_set_range_and_prepare_gram(x1, y1, x2, y2);  // 设置刷新范围并准备发送数据
    st7789_write_gram((uint8_t *)pixels, length, false);  // 发送数据到显存
}

/**
* @brief DMA1_Stream4中断处理函数（DMA传输完成中断）
*/
void DMA1_Stream4_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_Stream4, DMA_IT_TCIF4) == SET)  
    {
        BaseType_t pxHigherPriorityTaskWoken;
        xSemaphoreGiveFromISR(write_gram_semaphore, &pxHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
        
        DMA_ClearITPendingBit(DMA1_Stream4, DMA_IT_TCIF4);
    }
}
