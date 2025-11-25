// FILE: TNTSC.cpp
// Arduino STM32 用 NTSCビデオ出力ライブラリ by たま吉さん
// 作成日 2017/02/20, Blue Pillボード(STM32F103C8)にて動作確認
// 更新日 2017/02/27, delay_frame()の追加
// 更新日 2017/02/27, フック登録関数追加
// 更新日 2017/03/03, 解像度モード追加
// 更新日 2017/04/05, クロック48MHz対応
// 更新日 2017/04/18, SPI割り込みの廃止(動作確認中)
// 更新日 2017/04/27, NTSC走査線数補正関数追加
// 更新日 2017/04/30, SPI1,SPI2の選択指定を可能に修正
// 更新日 2017/06/25, 外部確保VRAMの指定を可能に修正

#include <TNTSC.h>
#include <SPI.h>
#include "TNTSC_HAL.h"

#define PWM_CLK PA1         // 同期信号出力ピン(PWM)
#define DAT PA7             // 映像信号出力ピン
#define NTSC_S_TOP 3        // 垂直同期開始ライン
#define NTSC_S_END 5        // 垂直同期終了ライン
#define NTSC_VTOP 30        // 映像表示開始ライン
#define IRQ_PRIORITY  2     // タイマー割り込み優先度
#define MYSPI1_DMA_CH DMA_CH3 // SPI1用DMAチャンネル
#define MYSPI2_DMA_CH DMA_CH5 // SPI2用DMAチャンネル
#define MYSPI_DMA DMA1        // SPI用DMA

// 画面解像度別パラメタ設定
typedef struct  {
  uint16_t width;   // 画面横ドット数
  uint16_t height;  // 画面縦ドット数
  uint16_t ntscH;   // NTSC画面縦ドット数
  uint16_t hsize;   // 横バイト数
  uint8_t  flgHalf; // 縦走査線数 (0:通常 1:半分)
  uint32_t spiDiv;  // SPIクロック分周
} SCREEN_SETUP;

#if F_CPU == 72000000L
#define NTSC_TIMER_DIV 3 // システムクロック分周 1/3
const SCREEN_SETUP screen_type[] __FLASH__ {
  { 112, 108, 216, 14, 1, SPI_CLOCK_DIV32 }, // 112x108
  { 224, 108, 216, 28, 1, SPI_CLOCK_DIV16 }, // 224x108 
  { 224, 216, 216, 28, 0, SPI_CLOCK_DIV16 }, // 224x216
  { 448, 108, 216, 56, 1, SPI_CLOCK_DIV8  }, // 448x108 
  { 448, 216, 216, 56, 0, SPI_CLOCK_DIV8  }, // 448x216 
};
#elif  F_CPU == 48000000L
#define NTSC_TIMER_DIV 2 // システムクロック分周 1/2
const SCREEN_SETUP screen_type[] __FLASH__ {
  { 128,  96, 192, 16, 1, SPI_CLOCK_DIV16 }, // 128x96
  { 256,  96, 192, 32, 1, SPI_CLOCK_DIV8  }, // 256x96 
  { 256, 192, 192, 32, 0, SPI_CLOCK_DIV8  }, // 256x192
  { 512,  96, 192, 64, 1, SPI_CLOCK_DIV4  }, // 512x96 
  { 512, 192, 192, 64, 0, SPI_CLOCK_DIV4  }, // 512x192 
  { 128, 108, 216, 16, 1, SPI_CLOCK_DIV16 }, // 128x108
  { 256, 108, 216, 32, 1, SPI_CLOCK_DIV8  }, // 256x108 
  { 256, 216, 216, 32, 0, SPI_CLOCK_DIV8  }, // 256x216
  { 512, 108, 216, 64, 1, SPI_CLOCK_DIV4  }, // 512x108 
  { 512, 216, 216, 64, 0, SPI_CLOCK_DIV4  }, // 512x216 
};
#endif

#define NTSC_LINE (262+0)                     // 画面構成走査線数(一部のモニタ対応用に2本に追加)
// SYNC 出力は HAL を通して行う
#define SYNC(V)  hal_gpio_write(PWM_CLK,V)        // 同期信号出力(PWM)
static uint8_t* vram;                         // ビデオ表示フレームバッファ
static volatile uint8_t* ptr;                 // ビデオ表示フレームバッファ参照用ポインタ
static volatile int count=1;                  // 走査線を数える変数

static void (*_bktmStartHook)() = NULL;       // ブランキング期間開始フック
static void (*_bktmEndHook)()  = NULL;        // ブランキング期間終了フック

static uint8_t  _screen;
static uint16_t _width;
static uint16_t _height;
static uint16_t _ntscHeight;
static uint16_t _vram_size;
static uint16_t _ntsc_line = NTSC_LINE;
static uint16_t _ntsc_adjust = 0;
static uint16_t _hAdjust = 0; // 横表示位置補正
static uint16_t _vAdjust = 0; // 縦表示位置補正
static uint8_t  _spino = 1;
static dma_channel  _spi_dma_ch = MYSPI1_DMA_CH;
static dma_dev* _spi_dma    = MYSPI_DMA;
static SPIClass* pSPI;

uint16_t TNTSC_class::width()  {return _width;;} ;
uint16_t TNTSC_class::height() {return _height;} ;
uint16_t TNTSC_class::vram_size() { return _vram_size;};
uint16_t TNTSC_class::screen() { return _screen;};


 // ブランキング期間開始フック設定
void TNTSC_class::setBktmStartHook(void (*func)()) {
  _bktmStartHook = func;
}

// ブランキング期間終了フック設定
void TNTSC_class::setBktmEndHook(void (*func)()) {
  _bktmEndHook = func;
}

// DMA用割り込みハンドラ(データ出力をクリア)
void TNTSC_class::DMA1_CH3_handle() {
  hal_spi_wait_tx_complete_and_clear(pSPI);
}

// DMAを使ったデータ出力
void TNTSC_class::SPI_dmaSend(uint8_t *transmitBuf, uint16_t length) {
  hal_dma_setup_transfer(_spi_dma, _spi_dma_ch, &pSPI->dev()->regs->DR, DMA_SIZE_8BITS, transmitBuf, DMA_SIZE_8BITS,
    DMA_MINC_MODE | DMA_FROM_MEM | DMA_TRNS_CMPLT);
  hal_dma_set_num_transfers(_spi_dma, _spi_dma_ch, length);
  hal_dma_enable(_spi_dma, _spi_dma_ch);
}

// ビデオ用データ表示(ラスタ出力）
void TNTSC_class::handle_vout() {
  if (count >=NTSC_VTOP+_vAdjust && count <=_ntscHeight+NTSC_VTOP+_vAdjust-1) {  	

    SPI_dmaSend((uint8_t *)ptr, screen_type[_screen].hsize);
  	//pSPI->dmaSend((uint8_t *)ptr, screen_type[_screen].hsize,1);
  	if (screen_type[_screen].flgHalf) {
      if ((count-NTSC_VTOP) & 1) 
      ptr+= screen_type[_screen].hsize;
    } else {
      ptr+=screen_type[_screen].hsize;
    }
  }
	
  // 次の走査線用同期パルス幅設定
  if(count >= NTSC_S_TOP-1 && count <= NTSC_S_END-1){
    // 垂直同期パルス(PWMパルス幅変更)
    hal_timer_set_ccr(2, 1412);
  } else {
    // 水平同期パルス(PWMパルス幅変更)
    hal_timer_set_ccr(2, 112);
  }

   count++; 
  if( count > _ntsc_line ){
    count=1;
    ptr = vram;    
  } 

}

void TNTSC_class::adjust(int16_t cnt, int16_t hcnt, int16_t vcnt) {
  _ntsc_adjust = cnt;
  _ntsc_line = NTSC_LINE+cnt;
  _hAdjust = hcnt;
  _vAdjust = vcnt;
}
	
// NTSCビデオ表示開始
void TNTSC_class::begin(uint8_t mode, uint8_t spino, uint8_t* extram) {
   // スクリーン設定
   _screen = mode <=4 ? mode: SC_DEFAULT;
   _width  = screen_type[_screen].width;
   _height = screen_type[_screen].height;   
   _vram_size  = screen_type[_screen].hsize * _height;
   _ntscHeight = screen_type[_screen].ntscH;
   _spino = spino;
   flgExtVram = false;
  
   if (extram) {
     vram = extram;
     flgExtVram = true;
   } else {
     vram = (uint8_t*)malloc(_vram_size);  // ビデオ表示フレームバッファ
   }
   cls();
   ptr = vram;  // ビデオ表示用フレームバッファ参照ポインタ
   count = 1;

  // SPIの初期化・設定
  // SPI の初期化・設定 (HAL 経由)
  pSPI = hal_spi_begin(spino);
  _spi_dma    = MYSPI_DMA;
  _spi_dma_ch = (spino == 2) ? MYSPI2_DMA_CH : MYSPI1_DMA_CH;
  hal_spi_setBitOrder(pSPI, MSBFIRST);
  hal_spi_setDataMode(pSPI, SPI_MODE3);
  if (_spino == 2) {
      hal_spi_setClockDivider(pSPI, screen_type[_screen].spiDiv-1);
  } else {
      hal_spi_setClockDivider(pSPI, screen_type[_screen].spiDiv);
  }
  pSPI->dev()->regs->CR1 |= SPI_CR1_BIDIMODE_1_LINE | SPI_CR1_BIDIOE; // 送信のみ利用の設定

  // SPIデータ転送用DMA設定
  hal_dma_init(_spi_dma);
  hal_dma_attach_interrupt(_spi_dma, _spi_dma_ch, &DMA1_CH3_handle);
  hal_spi_enable_tx_dma(pSPI);
  
  /// タイマ2の初期設定
  hal_nvic_set_priority(NVIC_TIMER2, IRQ_PRIORITY); // 割り込み優先レベル設定
  hal_timer_pause();                             // タイマー停止
  hal_timer_set_prescale(NTSC_TIMER_DIV);   // システムクロック 72MHzを24MHzに分周 
  hal_timer_set_overflow(1524);                   // カウンタ値1524でオーバーフロー発生 63.5us周期

  // +4.7us 水平同期信号出力設定
  hal_pinMode(PWM_CLK, PWM);          // 同期信号出力ピン(PWM)
  hal_timer_set_pwm_pol(TIMER2,2,1);  // 出力をアクティブLOWに設定
  hal_pwm_write(PWM_CLK, 112);        // パルス幅を4.7usに設定(仮設定)
  
  // +9.4us 映像出力用 割り込みハンドラ登録
  hal_timer_set_compare(1, 225-60+_hAdjust);  // オーバーヘッド分等の差し引き
  hal_timer_set_mode(1, TIMER_OUTPUTCOMPARE);
  hal_timer_attach_interrupt(1, handle_vout);

  hal_timer_set_count(0);
  hal_timer_refresh();       // タイマーの更新
  hal_timer_resume();        // タイマースタート  
}

// NTSCビデオ表示終了
void TNTSC_class::end() {
  hal_timer_pause();
  hal_timer_detach_interrupt(1);
  hal_spi_disable_tx_dma(pSPI);
  hal_dma_detach_interrupt(_spi_dma, _spi_dma_ch);
  hal_spi_end(pSPI, _spino);
  if (!flgExtVram)
     free(vram);
  // pSPI の解放は hal_spi_end が担当
}

// VRAMアドレス取得
uint8_t* TNTSC_class::VRAM() {
  return vram;  
}

// 画面クリア
void TNTSC_class::cls() {
  memset(vram, 0, _vram_size);
}

// フレーム間待ち
void TNTSC_class::delay_frame(uint16_t x) {
  while (x) {
    while (count != _ntscHeight + NTSC_VTOP);
    while (count == _ntscHeight + NTSC_VTOP);
    x--;
  }
}

	
TNTSC_class TNTSC;

