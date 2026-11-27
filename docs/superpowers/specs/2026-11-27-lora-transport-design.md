# Battleship LoRa 対戦（Cap LoRa-1262）設計書

- 日付: 2026-11-27
- 対象: `~/work/Battleship`（CardputerZero 用 Battleship、ストア公開済み 1.0.0-2 / 未公開 1.0.0-3）
- 位置づけ: PLAN.md Phase 8「LoRa 対応」の実施設計。PLAN.md は Unit LoRaE220-JP（UART 透過）
  前提だったが、実機に付いているのは **Cap LoRa-1262（SX1262 / SPI）** なので本書が優先する。

## 1. 目的と背景

- **なぜ**: BLE が届かない距離（別の部屋・階・屋外で数十〜数百 m）でも、**テンポよく**
  2 人対戦できるようにする。遠距離最優先ではなく、1 手の往復が体感で待たされないことを優先。
- **宣伝**: LoRa は第三者の受信機にも拾われる。ホスト待機中に流す募集ビーコンに
  **人が読める ASCII で「BATTLESHIP / CardputerZero Store / BSHP」** を入れ、生パケットを
  見ている人にアプリが分かるようにする（ユーザー決定: 案 A）。
- 実機 2 台（それぞれ Cap LoRa-1262 装着、技適 R 211-251216 確認済み、付属 868 表記アンテナ）。

## 2. ゴール / 非ゴール / アンチゴール

**ゴール**
1. 接続方式選択画面の「LoRa (Coming soon)」を有効化し、BLE/WiFi と同じ
   Host / Join の流れで LoRa 対戦が 1 ゲーム完走できる。
2. Join 側は LoRa 上の募集ビーコンを一覧表示し（ホスト名・RSSI）、選んで参加できる。
3. 撃つ→結果表示の往復がおおむね 1 秒以内（SF7 前提の目標値、実測で確認）。
4. 日本の 920MHz 帯ルールを常に守る（§5）。
5. ストア配布の .deb のまま動く（OS 設定変更なし、root 不要）。

**非ゴール**
- Cap の GNSS（ATGM336H）は使わない。
- Meshtastic / LoRa Messenger との相互運用（相手アプリで表示させること）はしない。
- 暗号化・中継・3 台以上の同時対戦・周波数ホッピング・デスクトップ（macOS）での実 LoRa。
- LoRa Messenger との共通ライブラリ化（今回はコピー。§4 参照）。

**アンチゴール（動いても失敗とみなす）**
- 868MHz 帯での送信、13dBm 超の送信、キャリアセンスなしの送信。
- 対戦していない時（タイトル画面・アプリ終了後）に電波を出し続ける＝ビーコンの垂れ流し。
- LVGL の描画スレッドを無線処理で数十 ms 以上止め、UI がカクつく。
- アプリ終了後も Cap の 5V 電源を入れっぱなしにする。
- 既存の BLE / WiFi 対戦の挙動を変えてしまう。

## 3. 実機調査で確定した事実（2026-11-27、CardputerZero-7f96ffcb を adb で読み取りのみ）

| 項目 | 値 |
|---|---|
| OS / kernel | Debian 13, `6.18.39+rpt-rpi-v8` aarch64 |
| SPI | `/dev/spidev0.1`（SPI0 CS1 = GPIO7、`root:spi 660`）。CS0 は LCD（panel-mipi-dbi-spi）がカーネルで使用中。spi0 バスは LCD と共有だがカーネルが排他するので spidev0.1 経由なら共存可 |
| GPIO | `/dev/gpiochip0`（`root:gpio`）。GPIO22（BUSY）/23（IRQ=DIO1）/26（RST）は未使用の input |
| I2C | `/dev/i2c-1`（`root:i2c`）。既存: 0x10 codec, 0x32 RTC, 0x34 キーボード, 0x4F M5IOE1, 0x55 電池, 0x68 IMU。Cap の IO 拡張 PI4IOE5V6408 は **0x43**（Cap 電源 ON 後に出現する想定。未電源のため未確認） |
| Cap 電源 | `/sys/class/leds/ext_5v_out/brightness`（`root:gpio 664`、既定 0 = OFF）。1 を書くと拡張端子 5V ON |
| USB/GPIO 切替 | `/sys/class/leds/ext_usb_gpio_fun` = 0（GPIO26/23 が GPIO モード。既定のまま触らない） |
| ユーザー `pi` | `spi, i2c, gpio, dialout` 等に所属 → 上記すべてアプリ権限でアクセス可 |
| 参考実装 | M5Stack 公式 `m5cardputerzero-cap-lora-1262` 1.0.1 が同一資源（ext_5v_out, spidev0.1, gpiochip0, i2c-1 0x43, RadioLib）を使用 |

ピン対応（Cap 資料 × CardputerZero 資料、実機の gpio 状態と整合）:
NSS=GPIO7(CE1) / SCK=GPIO11 / MOSI=GPIO10 / MISO=GPIO9 / BUSY=GPIO22 / IRQ(DIO1)=GPIO23 /
RST=GPIO26 / アンテナ切替 = PI4IOE5V6408 の P0 を High。

## 4. アプローチ（検討した案と選択）

| 案 | 内容 | トレードオフ |
|---|---|---|
| **1. LoRa Messenger の無線層をコピーして ITransport 化（採用）** | `~/work/LoraMessenger/src/adapters/radio/`（`linux_cap_lora_1262_radio.*` 約 800 行 = spidev/GPIO v2 ioctl/i2c-dev による SX1262 直接制御、`japan_920_radio_policy.*` = 920MHz 帯の LBT・送信時間バケット）を Battleship に持ち込み、`LoRaTransport : ITransport` を被せる | 依存ライブラリ追加なし（カーネルヘッダのみ）、自作コードで読める。欠点はコードの重複と、**LoRa Messenger 側でも実機未検証**であること → Phase L0 で最初に実機検証する |
| 2. RadioLib + 自作 Linux HAL | 公式アプリと同じ方式 | SX1262 制御は実績あり。ただし HAL（spidev/GPIO）と日本ルール層は結局自作、依存 1 つ増 |
| 3. 共通ライブラリ化して両アプリで共有 | LoRa Messenger を先に再構成 | 長期的に綺麗だが今回の目的に対して過大 |

ユーザー選択: **案 1**（2026-11-27）。
**フォールバック（ユーザー承認済み）**: L0 で LM ドライバを直しても実機の Cap と通信できない場合は、
案 2（RadioLib + 自作 Linux HAL）に切り替えてよい。切り替えたらユーザーに報告する。
そのため `radio_iface.h`（送信開始・poll・受信・RSSI 取得・停止）はどちらの実装でも載る形で先に決める。Battleship 側で見つけた無線層の修正は LoRa Messenger にも
バックポートする（別作業として扱う）。

## 5. 無線パラメータと法令順守

（ユーザー決定: テンポ優先、日本の 920MHz ルール順守。具体値は**私の提案**）

| 項目 | 値 | 理由 |
|---|---|---|
| 周波数 | **921.8 MHz 固定**（提案） | 920.5〜923.0 MHz（Cap 上限）内に 125kHz 幅が収まる。LoRa Messenger の 920.8 MHz と分けて干渉を避ける |
| 帯域 | 125 kHz | ARIB STD-T108 の 1 単位チャネル（200kHz）に収まる。LM ポリシーも 125kHz 固定 |
| SF | **SF7** | 16 バイト程度のパケットで約 46ms。SF9（約 150ms）よりテンポ優先。それでも BLE より十分遠い |
| CR | 4/5 | 速度優先 |
| プリアンブル | 8 シンボル | LM ポリシー下限。Builder が実測で 12 に上げてもよい |
| 送信出力 | **13 dBm（20mW）上限** | 特定小電力の上限。Cap は 22dBm まで出せるが使わない |
| 同期ワード | 0x12（LoRa private） | LM ポリシーと同じ。第三者の一般的なスニファ設定で受かりやすい |
| LBT | 送信前に RSSI を測り −90dBm 以上なら延期 | ARIB のキャリアセンス。**LM 実装は `GetRssiInst` を 1 回読むだけなので不十分** → 921.8MHz に適用される ARIB のキャリアセンス時間の間、RSSI を繰り返し測ってすべて閾値未満のときだけ送信するよう直す（L0/L1 で実施、NOTES に根拠を記録） |
| 送信時間制限 | LM の `Japan920RadioPolicy`（最小送信間隔 + 送信時間トークンバケット）を流用 | 1 パケット ≤400ms、長時間の総送信時間を制限 |

- `Japan920Profile::valid()` は SF7〜12・125kHz・≤13dBm・sync 0x12・920.5〜923.0MHz を検査済み。
  Battleship の値はこの範囲内なのでそのまま使える（範囲外の値は構築時に拒否される安全装置として残す）。
- Builder への注意: 移植時に ARIB STD-T108 の該当チャネル（921.8MHz）のキャリアセンス時間・
  送信時間・休止時間の規定と、LM ポリシーの既定値（`minimum_gap_ms`、バケット容量、補充率）が
  合っているかを一度照合し、NOTES.md に記録すること。

## 6. LoRa リンク層（Battleship 独自の封筒）

LoRa は放送型なので、既存のゲームフレーム（`FrameEncoder`、最大 13 バイト、CRC 付き）を
**そのまま** 小さな封筒に入れて送る。上位（ReliableLink / NetSession）は無変更。

```
共通ヘッダ: 'B' 'S' | type(1) | session_id(2, little endian)
type 0x01 BEACON : ヘッダ + flags(1) + host_name(≤12, ASCII) + 0x00 +
                   "BATTLESHIP for CardputerZero - Store code BSHP"（ASCII 固定文）
type 0x02 JOIN   : ヘッダ + joiner_nonce(2)
type 0x03 ACCEPT : ヘッダ + joiner_nonce(2)
type 0x10 DATA   : ヘッダ + ゲームフレームのバイト列（≤13）
type 0x11 BYE    : ヘッダ（任意。切断を早く伝えるため）
```

- **宣伝文**は BEACON にだけ載せる（DATA は短く保ちテンポ優先）。ASCII 固定文の正確な文言は
  Builder が上限（1 パケット送信時間・バケット）に収まる範囲で調整してよいが、
  「BATTLESHIP」「CardputerZero」「BSHP」の 3 語は必須。
- **session_id**: Host が乱数 16bit で決める。DATA/BYE は自分の session_id 以外を捨てる
  （近くで別の対戦が行われても混線しない）。
- 先頭 `'B''S'` と長さ・type が合わないパケットは黙って捨てる（他の LoRa 端末の電波対策）。

**接続手順（BLE と同じ見え方）**
1. Host: 「Host」選択 → Cap 起動 → BEACON を約 2 秒間隔で送信（ポリシーが延期したら従う）、
   その合間は受信。画面は既存ロビー（「LoRa で募集中: BSHIP-<名前>」）。
2. Join: 「Join」選択 → Cap 起動 → 受信のみで BEACON を集め、ホスト一覧（名前・RSSI、
   一定時間聞こえないものは消す）を既存の Join 画面形式で表示。
3. Join が選ぶ → JOIN 送信（最大数回再送）→ Host が ACCEPT を返し **BEACON を停止** →
   両者 `TransportState::Connected` → 以降 DATA。既存 NetSession の HELLO から先は従来どおり。
4. 対戦終了・ロビー離脱・アプリ終了で BYE（任意）→ 受信停止 → SX1262 スリープ →
   Cap 電源を **起動前の状態に戻す**。

## 7. ソフトウェア構成

```
src/net/lora/
  sx1262_radio.{h,cpp}          LM linux_cap_lora_1262_radio を移植（namespace battleship::net::lora）
  radio_iface.h                 LM ICapLora1262Radio 相当（テスト用フェイク差し替え口）
  japan920_policy.{h,cpp}       LM japan_920_radio_policy を移植
  cap_power.{h,cpp}             ext_5v_out の ON/OFF と元状態の復元
  lora_link.{h,cpp}             §6 の封筒・ビーコン・JOIN/ACCEPT 状態機械
  lora_transport.{h,cpp}        ITransport 実装（DATA の送受信を ReliableLink に見せる）
  lora_scanner.{h,cpp}          Join 用ビーコン一覧（BleScanner と同じ形の API）
tests/                          フェイク無線（損失・遅延・半二重・第三者パケット混入）で
                                lora_link / lora_transport / 1 ゲーム E2E をヘッドレス試験
tools/lora_probe.cpp            実機 bring-up 用 CLI（Phase L0）
```

- CMake オプション `BATTLESHIP_LORA_HW`（Linux で既定 ON、macOS は OFF）は**実機ドライバ**
  （`sx1262_radio.cpp`, `cap_power.cpp`）だけを切り替える。リンク層・ポリシー・UI の LoRa 分岐は
  全プラットフォームで常にビルドする（BLE のように `#if` で UI ごと消さない）。
- **デスクトップ用の疑似無線（私の判断）**: `radio_iface.h` の実装として localhost UDP
  ブロードキャストで「電波」を模す `udp_sim_radio` を用意し、macOS では LoRa を選ぶとこれを使う。
  2 プロセスで Host/Join/対戦まで通しで確認でき、損失・遅延の注入もできる。実機ビルドでは
  環境変数 `BATTLESHIP_LORA_SIM=1` のときだけ使える（開発用）。
- UI: `NetTransportPref` に `Lora` を追加（設定ファイルに永続化）、`net_transport_screen` の
  LoRa 行を選択可能化、`BaseViewModel` の `start_network_host/join` に BLE と同形の LoRa 分岐。
- 文字列（EN/JA/ZH）: 「LoRa で募集中」「LoRa ホストを探しています」「Cap LoRa-1262 が見つかりません」
  「アンテナを取り付けてから使ってください」等。CJK フォントサブセット再生成が必要。
- **タイムアウト**: LoRa 用 `SessionConfig` を別に持つ。初期値（提案）: `ack_timeout_ms` 800、
  `max_retransmits` 6、`ping_interval_ms` 5000、`disconnect_timeout_ms` 20000。Phase L0 の
  実測往復時間を見て Builder が調整し NOTES に記録。
- **UI を止めない**: 無線処理は既存どおり LVGL タイマーの `poll()` から呼ぶ。LM ドライバ内の
  `sleep_for`／BUSY 待ち（最大 100ms）が poll 経路で発生する場合は、送受信の開始・完了確認を
  非ブロッキング化するか、無線専用スレッド + ロックなしキューに分ける（どちらにするかは
  Builder の判断。条件は「LVGL の 1 フレームを 10ms 以上止めない」）。
- **安全装置**:
  - Cap 未装着（電源 ON 後に 0x43 が応答しない / SX1262 が応答しない）→ NetError 画面に
    「Cap LoRa-1262 が見つかりません（電源を切って装着してください）」。ホットプラグ禁止の注意込み。
  - アンテナ: ソフトでは検出できないため、LoRa を初めて選んだ時に
    「アンテナを取り付けましたか？ Enter=はい」の確認を 1 回だけ出し、設定に記録（**私の提案**）。
  - **Cap 電源の持ち主ルール（私の判断）**: 自分が ON にした時だけ、`$XDG_RUNTIME_DIR`
    （なければ `/tmp`）にマーカーファイル `battleship-cap-power` を作る。終了時はマーカーがある
    場合だけ OFF にしてマーカーを消す。起動時にすでに ON の場合、マーカーがあれば前回の
    自分の異常終了の残りなので自分のものとして扱う（終了時に OFF）。マーカーがなければ
    他の誰かが使っている 5V なので、終了時も触らない。
  - 終了経路（Esc 長押し・ホーム・SIGTERM など通常の終了）すべてで上記ルールに従って片付ける。

## 8. フェーズ

| Phase | 内容 | 完了条件 |
|---|---|---|
| **L0 実機 bring-up** | `lora_probe` CLI で Cap 電源 ON→0x43 確認→P0 High→SX1262 リセット・レジスタ読出し→2 台間で ping/pong。RSSI・往復時間・LBT 動作を測る。TCXO 設定（LM は DIO3=3.0V）を公式アプリ／Cap 回路図と照合 | 2 台で双方向にパケット往復、実測値を NOTES に記録。LM ドライバの不具合を洗い出し修正 |
| L1 無線層移植 + リンク層 | §7 の net/lora 一式、フェイク無線でのヘッドレス試験 | 全テスト green（既存 BLE/WiFi テストも green のまま） |
| L2 UI 組み込み | 接続方式 LoRa 有効化、Host/Join/ロビー/エラー、i18n、アンテナ確認、`udp_sim_radio` | デスクトップ 2 プロセス（疑似無線）で LoRa 対戦を完走、3 言語スクショ |
| L3 実機対戦 | 2 台で LoRa 対戦を 1 ゲーム完走（近距離 → BLE が届かない距離） | 完走、往復時間・切断検知・終了時の電源 OFF を確認、第三者受信機があれば BEACON の ASCII を確認 |
| L4 リリース | バージョン 1.1.0-1、ストア説明（3 言語）に LoRa 追記、メタデータ更新、NOTES/README 更新、czdev publish（ユーザーの指示で） | 更新版 PR の CI 通過 |

## 9. ストア・ドキュメント

- `app-builder.json`: version を 1.1.0-1 に。説明文 3 言語に「Cap LoRa-1262 があれば LoRa でも
  対戦可（任意）」を追記。`permissions.additional_hardware` は **true**（任意だが外付けハードを
  操作するため正直に申告する — 私の判断。ChainLED では任意ハードを false にしたので、
  審査で指摘されたら合わせる）。
- 宣伝の ASCII 文言・周波数・出力・ビーコン間隔を README と NOTES に明記（電波を出すアプリとして透明性）。
- PLAN.md の Phase 8 行を「Cap LoRa-1262（SX1262/SPI）」に更新し、本書へリンク。

## 9.5 ブランチとリリース順序（私の判断）

- ストアは 1.0.0-2（packages#531、2026-11-20 マージ済み）が公開中。記号キー修正の 1.0.0-3 は
  コミット済み・未公開。**1.0.0-3 は LoRa を待たずに先に単独で出す**（ユーザーの指示で czdev publish）。
- LoRa は機能ブランチ `feature/lora` で進め、main は不具合修正をすぐ出せる状態に保つ。
  L3 完了後に main へマージし、1.1.0-1 として出す。

## 10. Builder に任せること

- 移植時の命名・ファイル分割の細部、LM コードの不要部分の削除。
- 無線処理の非ブロッキング化の方式（§7）。
- ビーコン間隔・タイムアウト・プリアンブル長の実測に基づく微調整（§5・§7 の範囲内）。
- ビーコン一覧の消去時間、RSSI の表示形式。
- テストのフェイク無線の詳細。

## 11. 未決事項

なし（ユーザー決定: Cap LoRa-1262 を使用、テンポ優先、宣伝は案 A、実装は案 1、
L0 で動かなければ RadioLib へ切り替え可）。
