// FILE: TNTSC.cpp
// Arduino STM32 用 NTSCビデオ出力ライブラリ by たま吉さん
// 作成日 2017/02/20, Blue Pillボード(STM32F103C8)にて動作確認
// 更新日 2017/02/27, delay_frame()の追加
// 更新日 2017/02/27, フック登録関数追加
//

#include "TNTSC.h"
#include <SPI.h>

#define gpio_write(pin,val) gpio_write_bit(PIN_MAP[pin].gpio_device, PIN_MAP[pin].gpio_bit, val)

#define PWM_CLK PA1       // 同期信号出力ピン(PWM)
#define DAT PA7           // 映像信号出力ピン
#define NTSC_S_TOP 3      // 垂直同期開始ライン
#define NTSC_S_END 5      // 垂直同期終了ライン
#define NTSC_VTOP 30      // 映像表示開始ライン
#define IRQ_PRIORITY 2    // タイマー割り込み優先度

#define NTSC_LINE (262+2)                     // 画面構成走査線数(一部のモニタ対応用に2本に追加)
#define SYNC(V)  gpio_write(PWM_CLK,V)        // 同期信号出力(PWM)
static uint8_t vram[TNTSC_class::vram_size];  // ビデオ表示フレームバッファ
static volatile uint8_t* ptr;                 // ビデオ表示フレームバッファ参照用ポインタ
static volatile int count=1;                  // 走査線を数える変数

static void (*_bktmStartHook)() = NULL;       // ブランキング期間開始フック
static void (*_bktmEndHook)()  = NULL;        // ブランキング期間終了フック

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
  while(SPI.dev()->regs->SR & SPI_SR_BSY);
    SPI.dev()->regs->DR = 0;
}

// DMAを使ったデータ出力
void TNTSC_class::SPI_dmaSend(uint8_t *transmitBuf, uint16_t length) {
  dma_setup_transfer( 
    DMA1,DMA_CH3,          // SPI1用DMAチャンネル3を指定
    &SPI.dev()->regs->DR, // 転送先アドレス    ：SPIデータレジスタを指定
    DMA_SIZE_8BITS,       // 転送先データサイズ : 1バイト
    transmitBuf,          // 転送元アドレス     : SRAMアドレス
    DMA_SIZE_8BITS,       // 転送先データサイズ : 1バイト
    DMA_MINC_MODE|        // フラグ: サイクリック
    DMA_FROM_MEM|         //         メモリから周辺機器、転送完了割り込み呼び出しあり 
    DMA_TRNS_CMPLT        //         転送完了割り込み呼び出しあり 
  );
  dma_set_num_transfers(DMA1, DMA_CH3, length); // 転送サイズ指定
  dma_enable(DMA1, DMA_CH3);  // DMA有効化
}

// ビデオ用データ表示(ラスタ出力）
void TNTSC_class::handle_vout() {
  if (count >=NTSC_VTOP && count <=height+NTSC_VTOP-1) {
    SPI_dmaSend((uint8_t *)ptr, (width/8));
    ptr+=(width/8);    
  }
  // 次の走査線用同期パルス幅設定
  if(count >= NTSC_S_TOP-1 && count <= NTSC_S_END-1){
    // 垂直同期パルス(PWMパルス幅変更)
    TIMER2->regs.adv->CCR2 = 1412;
  } else {
    // 水平同期パルス(PWMパルス幅変更)
    TIMER2->regs.adv->CCR2 = 112;
  }

  if (count == NTSC_S_END-1) {
  	if (_bktmEndHook !=NULL)  // ブランキング期間終了
  	  _bktmEndHook();
  }	

  if (count == height+NTSC_VTOP-1) {
  	if (_bktmStartHook !=NULL)  // ブランキング期間開始
  	  _bktmStartHook();
  }

   count++; 
  if( count > NTSC_LINE ){
    count=1;
    ptr = vram;    
  } 
}

// NTSCビデオ表示開始
void TNTSC_class::begin() {
   ptr = vram; // ビデオ表示用フレームバッファ参照ポインタ
   count = 1;
  // SPIの初期化・設定
  SPI.begin(); 
  SPI.setBitOrder(MSBFIRST);  // データ並びは上位ビットが先頭
  SPI.setDataMode(SPI_MODE3); // MODE3(MODE1でも可)
  SPI.setClockDivider(SPI_CLOCK_DIV16); // クロックをシステムクロック72MHzの1/16に設定

  SPI.dev()->regs->CR1 |=SPI_CR1_BIDIMODE_1_LINE|SPI_CR1_BIDIOE; // 送信のみ利用の設定

  // SPIデータ転送用DMA設定
  dma_init(DMA1);
  dma_attach_interrupt(DMA1, DMA_CH3, &DMA1_CH3_handle);
  spi_tx_dma_enable(SPI.dev());  
  
  /// タイマ2の初期設定
  nvic_irq_set_priority(NVIC_TIMER2, IRQ_PRIORITY); // 割り込み優先レベル設定
  Timer2.pause();                // タイマー停止
  Timer2.setPrescaleFactor(3);   // システムクロック 72MHzを24MHzに分周 
  Timer2.setOverflow(1524);      // カウンタ値1524でオーバーフロー発生 63.5us周期

  // +4.7us 水平同期信号出力設定
  pinMode(PWM_CLK,PWM);          // 同期信号出力ピン(PWM)
  timer_cc_set_pol(TIMER2,2,1);  // 出力をアクティブLOWに設定
  pwmWrite(PWM_CLK, 112);        // パルス幅を4.7usに設定(仮設定)
  
  // +9.4us 映像出力用 割り込みハンドラ登録
  Timer2.setCompare(1, 225-48);  // オーバーヘッド分等の差し引き
  Timer2.setMode(1,TIMER_OUTPUTCOMPARE);
  Timer2.attachInterrupt(1, handle_vout);   

  Timer2.setCount(0);
  Timer2.refresh();       // タイマーの更新
  Timer2.resume();        // タイマースタート  
}

// NTSCビデオ表示終了
void TNTSC_class::end() {
  Timer2.pause();
  Timer2.detachInterrupt(1);
  spi_tx_dma_disable(SPI.dev());  
  dma_detach_interrupt(DMA1, DMA_CH3);
  SPI.end();
}

// VRAMアドレス取得
uint8_t* TNTSC_class::VRAM() {
  return vram;  
}

// 画面クリア
void TNTSC_class::cls() {
  memset(vram, 0, vram_size);
}

// フレーム間待ち
void TNTSC_class::delay_frame(uint16_t x) {
  while (x) {
    while (count != height+NTSC_VTOP);
    while (count == height+NTSC_VTOP);
    x--;
  }
}

	
TNTSC_class TNTSC;

