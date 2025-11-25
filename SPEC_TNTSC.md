# SPEC_TNTSC.md

この仕様書は `src/TNTSC.cpp`（Arduino STM32 用 NTSC ビデオ出力ライブラリ）を基に作成した移植向けの設計・仕様ドキュメントです。

**概要**
- ライブラリ名: TNTSC（Arduino STM32 向け NTSC ビデオ出力ライブラリ）
- 目的: STM32（Blue Pill 等）上で NTSC 信号を生成し、SPI + DMA を使ってビデオデータ（VRAM）を高速に送出する。
- 動作環境: Arduino STM32 コア（F_CPU が 72MHz または 48MHz に対応）、Timer2、SPI、DMA、PWM、NVIC を利用。
- 主な機能:
  - 複数解像度モード対応（`screen_type` 配列で定義）
  - VRAM（フレームバッファ）管理（内部確保 or 外部指定）
  - タイマー割り込みでラスタ単位の出力制御（水平/垂直同期制御）
  - SPI + DMA を用いた走査線データ転送
  - フレーム同期（`delay_frame`）と表示開始/終了（`begin`/`end`）
  - ブランキング開始/終了用フック登録（`setBktmStartHook` / `setBktmEndHook`）

**公開 API（関数・メソッド）**
- グローバルオブジェクト: `TNTSC_class TNTSC;`
- `uint16_t width()` — 現在の論理画面幅（ドット）
- `uint16_t height()` — 論理画面高さ（ドット）
- `uint16_t vram_size()` — VRAM サイズ（バイト）
- `uint16_t screen()` — 選択中のスクリーンモード番号
- `void setBktmStartHook(void (*func)())` — ブランキング期間開始フック登録
- `void setBktmEndHook(void (*func)())` — ブランキング期間終了フック登録
- `void DMA1_CH3_handle()` — DMA 割り込みハンドラ（ライブラリ内で DMA 完了に割り当て）
- `void SPI_dmaSend(uint8_t *transmitBuf, uint16_t length)` — DMA 転送開始（SRAM → SPI.DR）
- `void handle_vout()` — タイマ割り込みで呼ばれるラスタ出力ハンドラ
- `void adjust(int16_t cnt, int16_t hcnt, int16_t vcnt)` — 走査線数・水平/垂直調整
- `void begin(uint8_t mode, uint8_t spino, uint8_t* extram)` — 出力開始（mode: 解像度、spino: SPI 番号、extram: 外部 VRAM）
- `void end()` — 出力停止・リソース解放
- `uint8_t* VRAM()` — VRAM ベースアドレス取得
- `void cls()` — VRAM を 0 でクリア
- `void delay_frame(uint16_t x)` — 指定フレーム数だけ待つ（垂直同期単位）

**主要データ構造**
- `typedef struct SCREEN_SETUP`:
  - `width`: 画面横ドット数
  - `height`: 画面縦ドット数
  - `ntscH`: NTSC での垂直表示ライン数（モードにより 192/216）
  - `hsize`: 1 ラインあたりのバイト数（SPI 転送バイト数）
  - `flgHalf`: 走査線半分フラグ（1 = インタレース半分）
  - `spiDiv`: SPI クロック分周設定（Arduino SPI 抽象値）

- ライブラリ内部状態（主な静的変数）
  - `vram` : `uint8_t*` — フレームバッファ
  - `ptr`  : `volatile uint8_t*` — 現在送出中ラインのポインタ
  - `count`: `volatile int` — 現在の走査線番号（1.._ntsc_line）
  - `_screen`, `_width`, `_height`, `_vram_size`, `_ntscHeight`, `_ntsc_line`, `_ntsc_adjust`, `_hAdjust`, `_vAdjust`
  - SPI と DMA: `pSPI` (`SPIClass*`), `_spi_dma_ch`, `_spi_dma`
  - フック: `_bktmStartHook`, `_bktmEndHook`

**解像度・クロックパラメタ**
- `screen_type[]` は `F_CPU` によって切替:
  - `F_CPU==72MHz` 系: 112x108～448x216 等、`NTSC_TIMER_DIV = 3`
  - `F_CPU==48MHz` 系: 128x96～512x216 等、`NTSC_TIMER_DIV = 2`
- 各モードは `hsize`（横バイト）と `spiDiv`（SPI クロック分周）を定義
- VRAM サイズ = `hsize * height`

**タイミング / 信号生成**
- 全体走査線数: `NTSC_LINE = 262`
- 垂直同期ライン: `NTSC_S_TOP=3`, `NTSC_S_END=5`（垂直同期中に PWM の CCR を大きくする）
- 映像描画開始ライン: `NTSC_VTOP=30`（`count` がこれ以降で表示データ送出）
- `Timer2` を利用:
  - プリスケーラ: `NTSC_TIMER_DIV`（F_CPU 依存）
  - オーバーフロー: `Timer2.setOverflow(1524)` -> 約 63.5μs（1 ライン周期）
  - PWM（同期出力）ピン: `PWM_CLK = PA1`（`pwmWrite(PWM_CLK, 112)` -> H同期幅）
  - H同期 幅設定: `TIMER2->regs.adv->CCR2 = 112`（水平）、垂直同期期間は `1412`
  - 出力比較（Compare 1）を使い `handle_vout` を +9.4μs で呼ぶ: `Timer2.setCompare(1, 225-60+_hAdjust)` と `Timer2.attachInterrupt(1, handle_vout)`
- ラスタ処理: `handle_vout` は表示ライン範囲内で `SPI_dmaSend(ptr, hsize)` を呼び、`ptr` を次ラインに進める（`flgHalf` の場合は 1 ラインおきに進める）
- ライン終端で `count++`、`count > _ntsc_line` でリセットして `ptr = vram`

**SPI / DMA 動作**
- SPI 設定:
  - MSB first, MODE3（MODE1 でも可）
  - 送信のみモードとして `SPI_CR1_BIDIMODE_1_LINE | SPI_CR1_BIDIOE` を設定
  - `pSPI->setClockDivider(screen_type[_screen].spiDiv)`（`spiDiv` を使用）
- DMA 設定:
  - DMA は SPI の DR レジスタへメモリから転送するように設定（`dma_setup_transfer`）
  - フラグ: `DMA_MINC_MODE | DMA_FROM_MEM | DMA_TRNS_CMPLT`
  - 転送長は `dma_set_num_transfers(..., length)` で指定
  - DMA 完了割り込みに `DMA1_CH3_handle` を登録（ハンドラ内で SPI SR の BSY を待ち DR を 0 でクリア）
  - `spi_tx_dma_enable(pSPI->dev())` で SPI 側の DMA を有効化
- 実行フロー:
  - 表示走査線ごとのタイミングで `SPI_dmaSend(ptr, hsize)` → DMA 転送開始 → DMA 完了割り込みで DR のクリア

**メモリ管理**
- `begin(mode, spino, extram)`:
  - `extram` が指定されればそれを `vram` に使い、ライブラリは解放しない（`flgExtVram = true`）
  - 指定がなければ `malloc(_vram_size)` で内部確保し、`end()` 時に `free` する
- `cls()` は `memset(vram, 0, _vram_size)` を実行

**同期・フレーム待ち**
- `delay_frame(x)` は `count` と `_ntscHeight + NTSC_VTOP` を使って垂直同期完了をポーリングで待ち、x フレーム分ループする

**フック（Hooks）**
- `setBktmStartHook`, `setBktmEndHook` で関数ポインタを登録可能。ただし、このソース内で実際に呼ばれている箇所は見当たらない。移植時はブランキング開始/終了のタイミングで呼び出す実装が必要。

**定数 / ピン割り当て**
- `PWM_CLK` = `PA1`（同期出力）
- `DAT` = `PA7`（映像信号出力） — SPI MOSI に対応
- DMA チャンネル定義: `MYSPI1_DMA_CH`, `MYSPI2_DMA_CH`、DMA デバイス `MYSPI_DMA`
- 割り込み優先: `IRQ_PRIORITY = 2`
- `gpio_write` マクロは `gpio_write_bit(PIN_MAP[pin].gpio_device, PIN_MAP[pin].gpio_bit, val)` にマップされている — 移植時はピンマッピング機構を用意

**既知の注意点 / 移植時の検討事項**
- `_bktmStartHook` / `_bktmEndHook` は宣言のみで呼出箇所なし。垂直ブランキング区間の開始/終了で呼ぶ実装が必要なら `handle_vout` 内の該当カウント範囲で呼び出す。
- `DMA1_CH3_handle` は SPI レジスタ (`pSPI->dev()->regs->SR/DR`) に依存。移植先では SPI レジスタ操作と BSY チェックの代替手段を取る必要がある。
- `Timer2`、`timer_cc_set_pol`、`pwmWrite`、`Timer2.attachInterrupt` 等は Arduino STM32 コアの API。移植先では同等のタイマー/PWM/割り込み API を提供するか、HAL レイヤを実装する必要がある。
- `pSPI->setClockDivider(screen_type[_screen].spiDiv-1)` といった微調整（`-1` 等）はボード特性に依存するため再評価が必要。

**移植チェックリスト（必須機能）**
- 正確なライン周期を生成できるハードウェアタイマー（出力比較割り込み + PWM CCR を動的変更可能）
- PWM 出力ピン（同期信号）の出力、割り込みから CCR を切替可能
- SPI ペリフェラルが TX のみで 1 線出力が可能、かつ DR レジスタへ DMA で書き込めること
- DMA によるメモリ→SPI.DR 転送が可能で、転送完了割り込みを受けられること
- 割り込み優先度制御（NVIC 相当）
- 必要な SRAM を確保できること（最大 VRAM サイズは選択モードに依存）
- タイミング微調整用のパラメータ（`_hAdjust/_vAdjust/_ntsc_adjust`）
- ピンマッピング / `gpio_write_bit` 相当の API

**推奨の抽象化 API（移植用ラッパー）**
- HAL_Timer
  - `timer_init(timer_id, prescaler, overflow)`
  - `timer_set_pwm(pin, channel, polarity)`
  - `timer_set_ccr(channel, value)`
  - `timer_attach_compare_interrupt(timer_id, compare_index, callback)`
  - `timer_set_count/refresh/pause/resume`
- HAL_SPI_DMA
  - `spi_init(spi_id, mode, bitorder, clkdiv)`
  - `spi_enable_tx_dma(spi_id)`
  - `dma_transfer_to_spidr(spi_id, buffer, length, callback)`
  - `dma_init_channel(channel_id, flags)`
- HAL_GPIO
  - `gpio_write(pin, value)`
  - `pinMode(pin, mode)`
- メモリ
  - `vram_alloc(size)`, `vram_free(ptr)`

これらの抽象化を実装すれば、`TNTSC.cpp` のロジックは移植先に合わせた最小限の変更で動作させやすくなります。

**移植時の微調整ポイント**
- `screen_type[].spiDiv` は移植先 SPI クロック源と分周ロジックに合わせて再評価。
- タイマの `Overflow` 値（1524）や Compare 値（`225-60+_hAdjust`）は CPU クロックやタイマー分周に合わせて再計算。
- 垂直/水平同期の CCR 値（`1412` / `112`）もタイマ分解能に合わせて調整。
- DMA 完了の扱い：DR を 0 でクリアする実装が移植先で必要か確認。

**提案 API（高レベル、移植先で提供するラッパー）**
- `init_tntsc(mode, spi_num, extram_ptr)`
- `start_tntsc()`
- `stop_tntsc()`
- `tntsc_get_vram()`
- `tntsc_clear()`
- `tntsc_wait_frames(n)`
- `tntsc_set_hooks(start_hook, end_hook)`
- `tntsc_adjust(ntsc_delta, h_delta, v_delta)`

---

次のステップ:
- 希望があればこの `SPEC_TNTSC.md` をさらに整形・追加項目（ターゲット別移植ガイド、差分パッチ例）を入れます。
- 移植ターゲット（例: ESP32, RP2040, STM32 HAL/CMSIS 等）を指定いただければ、具体的なラッパー実装スケルトンを作成します。
