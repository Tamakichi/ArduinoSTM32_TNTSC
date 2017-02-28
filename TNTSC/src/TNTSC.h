// FILE: TNTSC.h
// Arduino STM32 用 NTSCビデオ出力ライブラリ by たま吉さん
// 作成日 2017/02/20, Blue Pillボード(STM32F103C8)にて動作確認
// 更新日 2017/02/27, delay_frame()の追加、
// 更新日 2017/02/27, フック登録関数追加
//

#ifndef __TNTSC_H__
#define __TNTSC_H__

#include <Arduino.h>

// ntscビデオ表示クラス定義
class TNTSC_class {
  public:
    static const uint16_t width  = 224;
    static const uint16_t height = 216;
    static const uint16_t vram_size = width*height/8;

  public:
    void begin();            // NTSCビデオ表示開始
    void end();              // NTSCビデオ表示終了 
    uint8_t*  VRAM();        // VRAMアドレス取得
    void cls();              // 画面クリア
    void delay_frame(uint16_t x); // フレーム換算時間待ち
	
	void setBktmStartHook(void (*func)()); // ブランキング期間開始フック設定
    void setBktmEndHook(void (*func)());   // ブランキング期間終了フック設定
	
  private:
    static void handle_vout();
    static void SPI_dmaSend(uint8_t *transmitBuf, uint16_t length) ;
    static void DMA1_CH3_handle();
};

extern TNTSC_class TNTSC; // グローバルオブジェクト利用宣言

#endif
