// FILE: TNTSC.h
// Arduino STM32 用 NTSCビデオ出力ライブラリ by たま吉さん
// 作成日 2017/02/20, Blue Pillボード(STM32F103C8)にて動作確認
// 更新日 2017/02/27, delay_frame()の追加、
// 更新日 2017/02/27, フック登録関数追加
// 更新日 2017/03/03, 解像度モード追加
//

#ifndef __TNTSC_H__
#define __TNTSC_H__

#include <Arduino.h>
#define SC_112x108  0 // 112x108
#define SC_224x108  1 // 224x108
#define SC_224x216  2 // 224x216
#define SC_448x108  3 // 448x108
#define SC_448x216  4 // 448x216

// ntscビデオ表示クラス定義
class TNTSC_class {    
  public:
    void begin(uint8_t mode=SC_224x216);   // NTSCビデオ表示開始
    void end();                            // NTSCビデオ表示終了 
    uint8_t*  VRAM();                      // VRAMアドレス取得
    void cls();                            // 画面クリア
    void delay_frame(uint16_t x);          // フレーム換算時間待ち
	void setBktmStartHook(void (*func)()); // ブランキング期間開始フック設定
    void setBktmEndHook(void (*func)());   // ブランキング期間終了フック設定

    uint16_t width() ;
    uint16_t height() ;
    uint16_t vram_size();
    uint16_t screen();
    
  private:
    static void handle_vout();
    static void SPI_dmaSend(uint8_t *transmitBuf, uint16_t length) ;
    static void DMA1_CH3_handle();
};

extern TNTSC_class TNTSC; // グローバルオブジェクト利用宣言

#endif
