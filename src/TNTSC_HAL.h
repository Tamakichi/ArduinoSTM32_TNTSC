// HAL 抽象化ヘッダ — TNTSC ライブラリのハードウェア依存呼び出しを抽象化します
#pragma once

#include <Arduino.h>
#include <SPI.h>

// GPIO / PIN
void hal_gpio_write(uint8_t pin, uint8_t val);
void hal_pinMode(uint8_t pin, uint8_t mode);

// Timer / PWM
void hal_nvic_set_priority(int irq, int prio);
void hal_timer_pause();
void hal_timer_resume();
void hal_timer_set_prescale(uint32_t v);
void hal_timer_set_overflow(uint32_t v);
void hal_timer_set_compare(uint8_t idx, uint32_t v);
void hal_timer_set_mode(uint8_t idx, uint32_t mode);
void hal_timer_attach_interrupt(uint8_t idx, void (*func)());
void hal_timer_detach_interrupt(uint8_t idx);
void hal_timer_set_count(uint32_t v);
void hal_timer_refresh();
void hal_timer_set_pwm_pol(uint8_t timer, uint8_t channel, uint8_t pol);
void hal_pwm_write(uint8_t pin, uint32_t v);
void hal_timer_set_ccr(uint8_t channel, uint32_t v);

// SPI
SPIClass* hal_spi_begin(uint8_t spino);
void hal_spi_end(SPIClass* spi, uint8_t spino);
void hal_spi_setBitOrder(SPIClass* spi, uint8_t bo);
void hal_spi_setDataMode(SPIClass* spi, uint8_t mode);
void hal_spi_setClockDivider(SPIClass* spi, uint32_t div);
void hal_spi_enable_tx_dma(void* spi_dev);
void hal_spi_disable_tx_dma(void* spi_dev);

// DMA
void hal_dma_init(void* dev);
void hal_dma_attach_interrupt(void* dev, int ch, void (*handler)());
void hal_dma_detach_interrupt(void* dev, int ch);
void hal_dma_setup_transfer(void* dev, int ch, volatile void* dst, int dst_size, void* src, int src_size, uint32_t flags);
void hal_dma_set_num_transfers(void* dev, int ch, uint32_t len);
void hal_dma_enable(void* dev, int ch);

// SPI device specific action (used in DMA completion handler)
void hal_spi_wait_tx_complete_and_clear(SPIClass* spi);
