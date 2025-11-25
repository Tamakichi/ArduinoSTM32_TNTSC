// デフォルト HAL 実装 (Arduino STM32 向け)
#include "TNTSC_HAL.h"
#include <TNTSC.h>
#include <SPI.h>

// GPIO / PIN
void hal_gpio_write(uint8_t pin, uint8_t val) {
  gpio_write_bit(PIN_MAP[pin].gpio_device, PIN_MAP[pin].gpio_bit, val);
}
void hal_pinMode(uint8_t pin, uint8_t mode) {
  pinMode(pin, mode);
}

// Timer / PWM (Arduino STM32 の Timer2 API をラップ)
void hal_nvic_set_priority(int irq, int prio) { nvic_irq_set_priority(irq, prio); }
void hal_timer_pause() { Timer2.pause(); }
void hal_timer_resume() { Timer2.resume(); }
void hal_timer_set_prescale(uint32_t v) { Timer2.setPrescaleFactor(v); }
void hal_timer_set_overflow(uint32_t v) { Timer2.setOverflow(v); }
void hal_timer_set_compare(uint8_t idx, uint32_t v) { Timer2.setCompare(idx, v); }
void hal_timer_set_mode(uint8_t idx, uint32_t mode) { Timer2.setMode(idx, mode); }
void hal_timer_attach_interrupt(uint8_t idx, void (*func)()) { Timer2.attachInterrupt(idx, func); }
void hal_timer_detach_interrupt(uint8_t idx) { Timer2.detachInterrupt(idx); }
void hal_timer_set_count(uint32_t v) { Timer2.setCount(v); }
void hal_timer_refresh() { Timer2.refresh(); }
void hal_timer_set_pwm_pol(uint8_t timer, uint8_t channel, uint8_t pol) { timer_cc_set_pol(timer, channel, pol); }
void hal_pwm_write(uint8_t pin, uint32_t v) { pwmWrite(pin, v); }
void hal_timer_set_ccr(uint8_t channel, uint32_t v) { TIMER2->regs.adv->CCR2 = v; }

// SPI
SPIClass* hal_spi_begin(uint8_t spino) {
  if (spino == 2) {
    SPIClass* s = new SPIClass(2);
    s->begin();
    return s;
  }
  SPI.begin();
  return &SPI;
}
void hal_spi_end(SPIClass* spi, uint8_t spino) {
  if (spino == 2) {
    if (spi) {
      spi->end();
      delete spi;
    }
  } else {
    SPI.end();
  }
}
void hal_spi_setBitOrder(SPIClass* spi, uint8_t bo) { spi->setBitOrder(bo); }
void hal_spi_setDataMode(SPIClass* spi, uint8_t mode) { spi->setDataMode(mode); }
void hal_spi_setClockDivider(SPIClass* spi, uint32_t div) { spi->setClockDivider(div); }
void hal_spi_enable_tx_dma(void* spi_dev) { spi_tx_dma_enable(((SPIClass*)spi_dev)->dev()); }
void hal_spi_disable_tx_dma(void* spi_dev) { spi_tx_dma_disable(((SPIClass*)spi_dev)->dev()); }

// DMA
void hal_dma_init(void* dev) { dma_init((dma_dev*)dev); }
void hal_dma_attach_interrupt(void* dev, int ch, void (*handler)()) { dma_attach_interrupt((dma_dev*)dev, (dma_channel)ch, handler); }
void hal_dma_detach_interrupt(void* dev, int ch) { dma_detach_interrupt((dma_dev*)dev, (dma_channel)ch); }
void hal_dma_setup_transfer(void* dev, int ch, volatile void* dst, int dst_size, void* src, int src_size, uint32_t flags) {
  dma_setup_transfer((dma_dev*)dev, (dma_channel)ch, (void*)dst, dst_size, src, src_size, flags);
}
void hal_dma_set_num_transfers(void* dev, int ch, uint32_t len) { dma_set_num_transfers((dma_dev*)dev, (dma_channel)ch, len); }
void hal_dma_enable(void* dev, int ch) { dma_enable((dma_dev*)dev, (dma_channel)ch); }

// DMA 完了ハンドラでの SPI 処理をラップ
void hal_spi_wait_tx_complete_and_clear(SPIClass* spi) {
  while(spi->dev()->regs->SR & SPI_SR_BSY);
  spi->dev()->regs->DR = 0;
}
