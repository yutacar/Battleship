# Battleship LoRa 対戦（Cap LoRa-1262）実装計画

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 接続方式「LoRa」を有効化し、Cap LoRa-1262 を付けた CardputerZero 2 台で Host/Join → 1 ゲーム完走できるようにして、1.1.0-1 として出せる状態にする。

**Architecture:** `src/net/lora/` に (a) 日本 920MHz 送信ポリシー、(b) 無線抽象 `IRadio` とその 3 実装（SX1262 実機・UDP 疑似・テスト用フェイク）、(c) BEACON/JOIN/ACCEPT/DATA/BYE の封筒を扱う `LoRaLink`、(d) それを `ITransport` に見せる `LoRaTransport` を置く。既存の FrameEncoder / ReliableLink / NetSession は無変更で上に載る。UI は BLE 分岐と同形の LoRa 分岐を足す。

**Tech Stack:** C++17, CMake, LVGL 9.5, GoogleTest 1.15.2, Linux spidev / GPIO v2 ioctl / i2c-dev（カーネルヘッダのみ）, Docker debian:trixie arm64 + BSP v0.0.7 のデバイスビルド, adb。

**Spec:** `docs/superpowers/specs/2026-11-27-lora-transport-design.md`（実装者は必ず併読）

## Global Constraints

- 作業ブランチ `feature/lora`（main から切る）。main には L3 完了後にマージ。
- 周波数 **921.8 MHz 固定**、帯域 125 kHz、**SF7**、CR 4/5、プリアンブル 8、**送信出力 13 dBm 上限**、同期ワード 0x12。868MHz 帯では絶対に送信しない。
- 送信前キャリアセンス: **5 ms の間 RSSI を繰り返し測り、すべて −90 dBm 未満のときだけ送信**（`kCarrierSenseMs = 5`, `kLbtThresholdDbm = -90`）。
- 送信時間ポリシー（2026-11-27 改訂、ARIB STD-T108 v1.5 第2部の表で確認）: **自分の送信終了から 50 ms 以上の休止**、1 パケットの送信時間 < 4 s（実装上は ≤ 400 ms で拒否してよい）、**1 時間あたり総送信時間の上限なし**（20mW・24〜32ch・キャリアセンス 5ms 以上の条件）。中心周波数は 920.6〜922.2 MHz に限定。旧記載の「最小間隔 100 ms・バケット 6000 ms・補充 1/10」は廃止。
- ビーコン ASCII 文言: `"BATTLESHIP for CardputerZero - Store code BSHP"`（3 語 BATTLESHIP / CardputerZero / BSHP 必須）。ビーコンは **Host のロビー待機中だけ** 送る。
- 既存の BLE / WiFi の挙動・テストを壊さない（`battleship_core_tests` 全 green を各タスクで維持）。
- 新しい外部ライブラリは追加しない。例外: Task 7 の L0 で LM 由来ドライバが実機で動かない場合のみ RadioLib（MIT）へ切替可（ユーザー承認済み、切替時はユーザーに報告）。
- LVGL スレッドを 1 回の poll で 10 ms 以上止めない（キャリアセンスの 5 ms は許容）。
- Cap 電源ファイル `/sys/class/leds/ext_5v_out/brightness`、マーカー `battleship-cap-power`（`$XDG_RUNTIME_DIR`、無ければ `/tmp`）。
- 実機ピン: spidev `/dev/spidev0.1`、gpiochip `/dev/gpiochip0`、RST=GPIO26、IRQ(DIO1)=GPIO23、BUSY=GPIO22、I2C `/dev/i2c-1` の PI4IOE5V6408 = 0x43 の P0 High でアンテナ切替。
- コード/コミットメッセージは英語、コミット末尾に `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。
- 移植元: `~/work/LoraMessenger/src/adapters/radio/{linux_cap_lora_1262_radio,japan_920_radio_policy,cap_lora_1262_radio}.*`（MIT、同一作者）。移植ファイル冒頭に出典コメント。

## Review Focus

1. **近くで別の Battleship が対戦中/募集中** → 自分の session_id 以外の DATA/BYE は無視、Join 一覧には複数ホストが並ぶ。→ Task 3 `SessionFilterIgnoresOtherMatches`, `ScanListsMultipleHosts`
2. **Meshtastic など他の LoRa 端末の電波（同期ワードが偶然一致したゴミ含む）** → 黙って捨て、クラッシュも状態遷移もしない。→ Task 2 `DecodeRejectsGarbage`（ランダム 10,000 個）, Task 3 `ForeignPacketsAreIgnored`
3. **JOIN への ACCEPT が消える／JOIN が二重に届く** → Join は再送し、Host は同じ nonce には何度でも ACCEPT を返す（二重接続にならない）。→ Task 3 `LostAcceptIsRecovered`, `DuplicateJoinDoesNotReset`
4. **Cap 未装着・電源ファイルが無い（他 OS イメージ）・アンテナ未確認** → エラー画面で理由を表示し、ハングしない・電源を残さない。→ Task 5 `AcquireFailsWithoutFile`, Task 9 `LoraHostShowsErrorWhenRadioMissing`
5. **対戦中に片方が電源断／圏外へ移動** → 20 秒以内に「Connection lost」、自分の Cap 電源は持ち主ルールで片付く。→ Task 4 `SilentPeerDisconnectsWithinTimeout`, Task 5 `ReleaseOnlyTurnsOffWhenOwned`

---

## ファイル構成

| ファイル | 責務 |
|---|---|
| `src/net/lora/japan920_policy.{h,cpp}` | 920MHz プロファイル検査・送信時間見積り・送信可否（LM 移植、既定値を Battleship 用に変更） |
| `src/net/lora/radio_iface.h` | `IRadio` 抽象・共通型・定数 |
| `src/net/lora/lora_packet.{h,cpp}` | 封筒のエンコード/デコード |
| `src/net/lora/lora_link.{h,cpp}` | ビーコン・スキャン・JOIN/ACCEPT・DATA キュー・BYE の状態機械 |
| `src/net/lora/lora_transport.{h,cpp}` | `ITransport` 実装 + LoRa 用 `SessionConfig` |
| `src/net/lora/cap_power.{h,cpp}` | Cap 5V の持ち主ルール（純ファイル I/O、全 OS でビルド） |
| `src/net/lora/sx1262_radio.{h,cpp}` | SX1262 実機ドライバ（Linux かつ `BATTLESHIP_LORA_HW` のみ） |
| `src/net/lora/udp_sim_radio.{h,cpp}` | localhost マルチキャストの疑似無線（開発用、全 OS） |
| `src/net/lora/radio_factory.{h,cpp}` | 実行環境に応じた `IRadio` 生成 |
| `tests/support/fake_radio_bus.{h,cpp}` | テスト用インメモリ無線（損失・遅延・半二重・混入・LBT busy） |
| `tests/support/net_game_driver.{h,cpp}` | 既存 `run_full_game` / `expect_consistent_final_state` を共通化 |
| `tests/test_lora_{policy,packet,link,transport,cap_power}.cpp` | 各層の試験 |
| `tools/lora_probe.cpp` | 実機 bring-up CLI（.deb に入れない） |
| 既存変更 | `CMakeLists.txt`, `tests/CMakeLists.txt`, `src/i18n/settings.{h,cpp}`, `src/i18n/strings*.{h,cpp}`, `src/view/screens/net_transport_screen.cpp`, `src/viewmodel/base_viewmodel.{h,cpp}`, `cmake/cm0-package.cmake`, `app-builder.json`, `README.md`, `NOTES.md`, `PLAN.md` |

---

### Task 1: ブランチ作成と 920MHz ポリシー移植

**Files:**
- Create: `src/net/lora/japan920_policy.h`, `src/net/lora/japan920_policy.cpp`
- Modify: `CMakeLists.txt`（`battleship_net` に lora ソース追加の雛形、`BATTLESHIP_LORA_HW` オプション定義）, `tests/CMakeLists.txt`
- Test: `tests/test_lora_policy.cpp`

**Interfaces:**
- Produces（namespace `battleship::net::lora`）:
  - `using RadioTick = uint64_t;`
  - `struct Japan920Profile { uint32_t frequency_hz = 921'800'000; uint32_t bandwidth_hz = 125'000; uint8_t spreading_factor = 7; uint8_t coding_rate_denominator = 5; uint8_t transmit_power_dbm = 13; uint16_t preamble_symbols = 8; uint8_t sync_word = 0x12; bool valid() const noexcept; };`
  - `struct Japan920PolicyConfig { Japan920Profile profile; RadioTick minimum_gap_ms = 100; RadioTick bucket_capacity_ms = 6000; uint32_t refill_denominator = 10; };`
  - `enum class TxDecisionStatus { Allowed, Deferred, Locked, Invalid };  struct TxDecision { TxDecisionStatus status; RadioTick not_before_ms; };`
  - `class Japan920Policy { explicit Japan920Policy(Japan920PolicyConfig = {}); TxDecision evaluate(RadioTick now_ms, size_t bytes); void record(RadioTick now_ms, size_t bytes); RadioTick airtime_ms(size_t bytes) const; bool valid() const; };`
  - CMake: `option BATTLESHIP_LORA_HW`（Linux 既定 ON / それ以外 OFF）、`battleship_net` に PUBLIC 定義 `BATTLESHIP_LORA_HW=$<BOOL:...>`

- [ ] **Step 1:** `git switch -c feature/lora`
- [ ] **Step 2: 失敗するテストを書く** — `tests/test_lora_policy.cpp`:
  - `TEST(LoraPolicy, DefaultProfileIsBattleshipProfile)`: 既定値が 921.8MHz/SF7/CR5/13dBm/pre8/0x12 で `valid()==true`
  - `TEST(LoraPolicy, RejectsIllegalProfiles)`: 868'000'000Hz、14dBm、帯域 250kHz、923'000'000Hz（上端はみ出し）、同期ワード 0x34 → それぞれ `valid()==false`
  - `TEST(LoraPolicy, AirtimeSf7)`: `airtime_ms(18)` が 41〜52 ms、`airtime_ms(65)` が 110〜135 ms
  - `TEST(LoraPolicy, MinimumGapDefers)`: t=0 で 18B を record → t=50 で evaluate が `Deferred` かつ `not_before_ms==100`
  - `TEST(LoraPolicy, BucketLimitsLongTermDuty)`: 18B を 100ms 間隔で連続 record し続けると、累計送信時間がバケット容量 6000ms を超える前に `Deferred` が返る
- [ ] **Step 3:** `tests/CMakeLists.txt` に追加しビルド → 失敗を確認  
  Run: `cmake --build build/desktop && ctest --test-dir build/desktop -R LoraPolicy`（既存のデスクトップビルドディレクトリ名に合わせる）  Expected: リンク/コンパイルエラー
- [ ] **Step 4:** LM の `japan_920_radio_policy.{h,cpp}` を移植（namespace・型名を上記に、既定値を上記に変更、`ports::` 依存を除去）
- [ ] **Step 5:** テスト実行 → PASS、`battleship_core_tests` 全体も PASS
- [ ] **Step 6: Commit** `feat(lora): port Japan 920 MHz transmit policy with Battleship profile`

### Task 2: 無線抽象・封筒コーデック・テスト用フェイク無線

**Files:**
- Create: `src/net/lora/radio_iface.h`, `src/net/lora/lora_packet.{h,cpp}`, `tests/support/fake_radio_bus.{h,cpp}`
- Test: `tests/test_lora_packet.cpp`

**Interfaces:**
- Produces（`battleship::net::lora`）:
  - `constexpr size_t kMaxRadioPacket = 80;`
  - `enum class RadioStart { Started, Busy, Failed };`  // Busy = キャリアセンスで使用中 or 受信中
  - `enum class RadioEvent { Idle, TxDone, Received, Failed };`
  - `struct RadioPoll { RadioEvent event = RadioEvent::Idle; size_t size = 0; int16_t rssi_dbm = 0; };`
  - `class IRadio { public: virtual ~IRadio(); virtual bool ready() const = 0; virtual RadioStart try_start_transmit(const uint8_t* data, size_t size) = 0; virtual RadioPoll poll(uint8_t* dst, size_t cap) = 0; virtual void shutdown() = 0; };`  
    契約: `try_start_transmit` はキャリアセンス込み。送信完了で `TxDone` を 1 回返し自動で受信待ちに戻る。受信は常時。
  - `enum class PacketType : uint8_t { Beacon = 0x01, Join = 0x02, Accept = 0x03, Data = 0x10, Bye = 0x11 };`
  - `inline constexpr char kBeaconAdText[] = "BATTLESHIP for CardputerZero - Store code BSHP";`
  - `constexpr size_t kMaxHostNameLen = 12; constexpr size_t kMaxDataLen = 16;`
  - `struct Packet { PacketType type; uint16_t session_id = 0; uint16_t nonce = 0; uint8_t flags = 0; std::string host_name; std::vector<uint8_t> data; };`
  - `std::vector<uint8_t> encode_packet(const Packet& p);`  `std::optional<Packet> decode_packet(const uint8_t* bytes, size_t len);`
  - バイト配置（仕様 §6）: `'B' 'S' type sid_lo sid_hi` 共通。Beacon: `+ flags + name(ASCII, ≤12) + 0x00 + kBeaconAdText`。Join/Accept: `+ nonce_lo nonce_hi`。Data: `+ data(1..16)`。Bye: ヘッダのみ。デコードは種類ごとの長さと、Beacon の広告文完全一致・名前が印字可能 ASCII であることを検査。
  - テスト支援（namespace `battleship::net::lora::testing`）:  
    `struct FakeBusConfig { double loss_rate = 0; uint32_t seed = 1; bool use_airtime = true; };`  
    `class FakeRadioBus { explicit FakeRadioBus(FakeBusConfig = {}); FakeRadio& add_radio(int16_t rssi_dbm = -60); void advance_to(uint64_t now_ms); void inject_foreign(const std::vector<uint8_t>& bytes); void set_channel_busy(bool busy); };`  
    `class FakeRadio : public IRadio`（送信中は受信しない＝半二重、送信時間は `Japan920Policy::airtime_ms` で遅延、`set_channel_busy(true)` 中は `Busy`）

- [ ] **Step 1: 失敗するテストを書く** — `tests/test_lora_packet.cpp`:
  - `RoundTripAllTypes`: 5 種類それぞれ encode→decode で全フィールド一致
  - `BeaconCarriesReadableAd`: エンコード結果のバイト列に `"BATTLESHIP"`, `"CardputerZero"`, `"BSHP"` が ASCII でそのまま含まれる、長さ ≤ `kMaxRadioPacket`
  - `BeaconTruncatesLongHostName`: 20 文字の名前 → デコード後 12 文字
  - `DecodeRejectsGarbage`: 固定シードの乱数で長さ 0〜80 のバイト列 10,000 個 → クラッシュせず、`'B''S'` で始まらないものは全て `nullopt`
  - `DecodeRejectsWrongLengths`: Join 長 6、Data 長 5（データ 0）、Data 長 22（データ 17） → `nullopt`
  - `FakeBusHalfDuplex`: 2 台、A が送信中に B が送信開始 → A は B のパケットを受信しない／B だけが A を受信
- [ ] **Step 2:** ビルドして失敗を確認（`ctest -R LoraPacket`）
- [ ] **Step 3:** `radio_iface.h`, `lora_packet.cpp`, `fake_radio_bus.cpp` を実装
- [ ] **Step 4:** PASS 確認（全テスト）
- [ ] **Step 5: Commit** `feat(lora): radio interface, packet envelope codec and fake radio bus`

### Task 3: LoRaLink（ビーコン・スキャン・接続・データ）

**Files:**
- Create: `src/net/lora/lora_link.{h,cpp}`
- Test: `tests/test_lora_link.cpp`

**Interfaces:**
- Consumes: Task 1 `Japan920Policy`, Task 2 `IRadio`/`Packet`/codec/`FakeRadioBus`
- Produces:
  - `enum class LinkState { Idle, Beaconing, Scanning, Joining, Connected, Closed, Failed };`
  - `struct LoRaPeer { std::string host_name; uint16_t session_id; int16_t rssi_dbm; uint64_t last_seen_ms; };`
  - `struct LinkTiming { uint32_t beacon_interval_ms = 2000; uint32_t peer_expiry_ms = 8000; uint32_t join_retry_ms = 1000; int join_max_attempts = 5; uint32_t lbt_backoff_min_ms = 10; uint32_t lbt_backoff_max_ms = 60; size_t tx_queue_limit = 16; };`
  - `class LoRaLink { LoRaLink(IRadio& radio, Japan920Policy policy, uint32_t seed, LinkTiming timing = {}); void start_host(const std::string& host_name, uint64_t now_ms); void start_scan(uint64_t now_ms); bool join(uint16_t session_id, uint64_t now_ms); void poll(uint64_t now_ms); LinkState state() const; uint16_t session_id() const; const std::vector<LoRaPeer>& peers() const; bool send_data(const uint8_t* data, size_t len); size_t take_received(std::vector<uint8_t>& out); void close(uint64_t now_ms); };`
  - 振る舞い: Host は乱数 session_id、`Beaconing` 中は `beacon_interval_ms` ごとに BEACON。JOIN 受信 → ACCEPT 送信・ビーコン停止・その nonce を記憶して `Connected`。`Connected` 後も同 nonce の JOIN には ACCEPT を再送、異なる nonce は無視。Join は ACCEPT 受信で `Connected`、`join_max_attempts` 回で応答なければ `Failed`。送信は FIFO（ACCEPT/BYE を DATA より優先）、ポリシー `Deferred` は待つ、`RadioStart::Busy` は乱数バックオフ。キュー上限超過は最も古い DATA を捨てる（上位の ReliableLink が再送で回復）。`close` は Connected なら BYE を 1 回試みてから `Closed`。BYE 受信で `Closed`。

- [ ] **Step 1: 失敗するテストを書く**（FakeRadioBus で 2〜3 台、時刻は 5ms 刻みで進める）:
  - `HostBeaconsPeriodically`: 10 秒で BEACON 送信回数 4〜6 回
  - `ScanListsMultipleHosts`: Host 2 台 + Scanner 1 台 → 3 秒以内に `peers()` が 2 件、名前・session_id・rssi が入る
  - `PeerExpires`: Host を close → 8 秒後に一覧から消える
  - `JoinConnectsBothSides`: Join → 両者 `Connected`、Host のビーコンが止まる（以後 BEACON 0 回）
  - `LostAcceptIsRecovered`: 最初の ACCEPT だけ落とす設定 → Join 再送で 3 秒以内に両者 `Connected`
  - `DuplicateJoinDoesNotReset`: 接続後に同じ JOIN を注入 → Host の session_id 不変、DATA が流れ続ける
  - `DataFlowsBothWays`: 13 バイトを双方向 20 往復、順序どおり受信
  - `SessionFilterIgnoresOtherMatches`: 別 session_id の DATA/BYE を注入 → 受信されず状態不変
  - `ForeignPacketsAreIgnored`: `inject_foreign` で乱数パケット 500 個を流しながら接続・データ送受信が成功
  - `CarrierBusyBacksOff`: `set_channel_busy(true)` 中は送信 0 回、解除後に送信される
  - `PolicyDeferralRespected`: すべての送信間隔 ≥ 100ms（自機の送信時刻を記録して検査）
  - `ByeClosesPeer`: 片方 `close` → もう片方 2 秒以内に `Closed`
  - `JoinGivesUpWhenNoHost`: 存在しない session_id に join → 約 5 秒で `Failed`
- [ ] **Step 2:** 失敗を確認
- [ ] **Step 3:** `lora_link.cpp` 実装
- [ ] **Step 4:** PASS（全テスト）
- [ ] **Step 5: Commit** `feat(lora): LoRaLink beacon/scan/join state machine with session filtering`

### Task 4: LoRaTransport と LoRa 越し 1 ゲーム E2E

**Files:**
- Create: `src/net/lora/lora_transport.{h,cpp}`, `tests/support/net_game_driver.{h,cpp}`
- Modify: `tests/test_net_session.cpp`（`run_full_game` / `expect_consistent_final_state` / `make_session_pair` を support へ移し、既存テストはそれを使う。挙動は不変）
- Test: `tests/test_lora_transport.cpp`

**Interfaces:**
- Consumes: Task 3 `LoRaLink`
- Produces:
  - `class LoRaTransport final : public ITransport { LoRaTransport(LoRaLink& link, std::function<uint64_t()> clock_ms); void poll() override; TransportState state() const override; bool send(const uint8_t*, size_t) override; size_t receive(std::vector<uint8_t>&) override; void close() override; };`  
    状態対応: `Connected`→`Connected`、`Closed/Failed`→`Disconnected`、それ以外→`Connecting`。`send` はゲームフレームを `kMaxDataLen` 以下に分割して `send_data`（通常 1 フレーム = 1 パケット）。
  - `SessionConfig lora_session_config();` → `ack_timeout_ms = 800, max_retransmits = 6, ping_interval_ms = 5000, disconnect_timeout_ms = 20000`
  - `tests/support/net_game_driver.h`: `int run_full_game(net::NetSession& host, net::NetSession& client, std::function<void(uint32_t now_ms)> step, uint32_t fire_seed, uint32_t step_ms = 25);` と `void expect_consistent_final_state(const net::NetSession& host, const net::NetSession& client);`

- [ ] **Step 1:** support への移設（リファクタのみ）→ 既存 `NetSession.*` テストが全て PASS することを確認 → コミット `test: share full-game driver between transport tests`
- [ ] **Step 2: 失敗するテストを書く** — `tests/test_lora_transport.cpp`:
  - `TransportReportsConnectingThenConnected`
  - `FullGameOverFakeLoRaCleanLink`: シード 0〜4、`lora_session_config()` で NetSession 2 つを LoRaTransport 上に → 完走・最終状態一致
  - `FullGameOverFakeLoRaLossyLink`: 損失 10%、シード 0〜3 → 完走
  - `ShotRoundTripUnderOneSecond`: クリーン回線で「撃つ→結果」の往復が模擬時間で 1000ms 未満（SF7 の送信時間込み）
  - `SilentPeerDisconnectsWithinTimeout`: 対戦中に片方の radio を `shutdown` → もう片方が 20,000ms ±1 poll 以内に Disconnected
- [ ] **Step 3:** 失敗を確認 → **Step 4:** 実装 → **Step 5:** PASS（全テスト）
- [ ] **Step 6: Commit** `feat(lora): LoRaTransport ITransport adapter and LoRa session timings`

### Task 5: Cap 電源の持ち主ルール

**Files:**
- Create: `src/net/lora/cap_power.{h,cpp}`
- Test: `tests/test_lora_cap_power.cpp`

**Interfaces:**
- Produces:
  - `inline constexpr char kCapPowerPath[] = "/sys/class/leds/ext_5v_out/brightness";  inline constexpr char kCapPowerMarker[] = "battleship-cap-power";`
  - `std::string default_marker_dir();`（`$XDG_RUNTIME_DIR`、未設定なら `/tmp`）
  - `class CapPower { CapPower(std::string brightness_path = kCapPowerPath, std::string marker_dir = default_marker_dir(), uint32_t settle_ms = 100); bool acquire(); void release(); bool owned() const; };`  
    acquire: ファイル無し/書けない → false。値 0 → 1 を書きマーカー作成・`settle_ms` 待つ・owned。値 1 かつマーカー有 → owned（前回の自分の残り）。値 1 かつマーカー無 → 他者のもの、触らず true・not owned。release: owned のときだけ 0 を書きマーカー削除。デストラクタで release。

- [ ] **Step 1: 失敗するテストを書く**（一時ディレクトリに偽の brightness ファイル、`settle_ms = 0`）:
  - `AcquireTurnsOnAndMarks` / `ReleaseOnlyTurnsOffWhenOwned` / `ForeignPowerIsLeftOn` / `StaleMarkerMeansOurs` / `AcquireFailsWithoutFile` / `DestructorReleases`
- [ ] **Step 2:** 失敗確認 → **Step 3:** 実装 → **Step 4:** PASS
- [ ] **Step 5: Commit** `feat(lora): Cap 5V power ownership with marker file`

### Task 6: SX1262 実機ドライバ移植（Linux のみ）

**Files:**
- Create: `src/net/lora/sx1262_radio.{h,cpp}`
- Modify: `CMakeLists.txt`（`BATTLESHIP_LORA_HW` 時のみ `sx1262_radio.cpp` を `battleship_net` に追加）

**Interfaces:**
- Consumes: Task 1 `Japan920Profile`, Task 2 `IRadio`
- Produces:
  - `enum class RadioInitError { None, PowerFailed, SpiOpenFailed, GpioFailed, ExpanderMissing, RadioNoResponse, ConfigFailed };`（`PowerFailed` は Task 8 の `open_radio` が `CapPower::acquire` 失敗時に使う。全 OS で見えるよう `radio_iface.h` 側に置いてよい）
  - `struct Sx1262Config { std::string spi_device = "/dev/spidev0.1"; std::string gpio_chip = "/dev/gpiochip0"; std::string i2c_device = "/dev/i2c-1"; uint32_t reset_gpio = 26, irq_gpio = 23, busy_gpio = 22; uint32_t spi_speed_hz = 4'000'000; int16_t lbt_threshold_dbm = -90; uint32_t carrier_sense_ms = 5; Japan920Profile profile; };`
  - `class Sx1262Radio final : public IRadio { explicit Sx1262Radio(Sx1262Config); RadioInitError init_error() const; /* IRadio */ };`
- 移植時の変更点（LM の `LinuxCapLora1262Radio` から）:
  1. キャリアセンス: `GetRssiInst` を `carrier_sense_ms` の間 ≈200µs 間隔で繰り返し、1 回でも閾値以上なら `Busy`
  2. 受信時 RSSI: `GetPacketStatus`(0x14) で `RadioPoll::rssi_dbm` を埋める
  3. 初期化失敗理由を `RadioInitError` で保持（0x43 無応答 → `ExpanderMissing`、BUSY 解除せず/レジスタ読出し不一致 → `RadioNoResponse`）
  4. `antenna_confirmed` フラグは削除（UI 側で確認する）
  5. TCXO(DIO3) 設定値は LM のまま。Task 7 で照合し必要なら修正

- [ ] **Step 1:** 移植・変更点 1〜5 を実装
- [ ] **Step 2:** デバイスビルドが通ることを確認  
  Run: `docker start battleship-cp0-build` → 既存手順（NOTES.md §20 / ChainLED と同じ BSP マウント）で `-DBATTLESHIP_LORA_HW=ON -DAPP_DEBIAN_REVISION=1` ビルド  Expected: エラー・警告なしでリンク
- [ ] **Step 3:** macOS デスクトップビルドとテストが変わらず通る
- [ ] **Step 4: Commit** `feat(lora): SX1262 Linux driver ported from LoRa Messenger with sustained carrier sense`

### Task 7: L0 実機 bring-up（`lora_probe`）— ユーザーの 2 台が必要

**Files:**
- Create: `tools/lora_probe.cpp`
- Modify: `CMakeLists.txt`（`BATTLESHIP_LORA_HW` 時のみ `battleship_lora_probe` 実行ファイル。install 対象外）, `NOTES.md`（新章「LoRa L0 bring-up」）

**Interfaces:**
- Consumes: Task 5 `CapPower`, Task 6 `Sx1262Radio`, Task 1 `Japan920Policy`, Task 2 codec
- CLI: `battleship_lora_probe info` / `ping [--count N]` / `pong` / `listen [--seconds S]`  
  info: 電源取得→0x43 応答→SX1262 リセット→同期ワードレジスタ(0x0740)読出し（0x12 設定後 `0x14 0x24`）→結果表示。ping: Data 型で連番送信、pong の返送を待ち RTT と RSSI を表示。listen: 受信した全パケットを hex+ASCII 表示（Beacon の広告文確認用）。終了時に必ず `CapPower::release`。

- [ ] **Step 1:** 実装しデバイスビルド
- [ ] **Step 2:** ユーザーに 2 台（Cap・アンテナ装着、ADB 有効）を同時接続してもらい `adb devices` で 2 台を確認
- [ ] **Step 3:** 両機に `adb -s <serial> push` → `info` 実行  Expected: `expander 0x43 OK`, `sync word 14 24`, 電源が終了後 0 に戻る
- [ ] **Step 4:** A=`pong`, B=`ping --count 50`  Expected: 受信率 ≥ 95%、RTT の中央値と RSSI を NOTES に記録
- [ ] **Step 5:** キャリアセンス確認: A で連続送信中に B の送信が `Busy` で延期されることをログで確認
- [ ] **Step 6:** TCXO 設定・送信出力を公式アプリ／Cap 回路図（`U214-sche-Cap-LoRa1262_SCH_V1.1`）と照合、ARIB STD-T108 の該当規定と本実装の値を NOTES に表で記録
- [ ] **Step 7（条件付き）:** Step 3〜4 が修正を重ねても通らない場合 → RadioLib（固定タグ、FetchContent）＋自作 Linux HAL で `Sx1262Radio` を再実装（`IRadio` は不変）、ユーザーに報告
- [ ] **Step 8:** 実機の後片付け（`/tmp` の probe 削除）→ **Commit** `feat(lora): lora_probe bring-up tool and L0 hardware notes`

### Task 8: 疑似無線（UDP）と無線ファクトリ

**Files:**
- Create: `src/net/lora/udp_sim_radio.{h,cpp}`, `src/net/lora/radio_factory.{h,cpp}`
- Test: `tests/test_lora_transport.cpp` に追記

**Interfaces:**
- Produces:
  - `class UdpSimRadio final : public IRadio { explicit UdpSimRadio(double loss_rate = 0.0, uint16_t port = 35360); };` — 239.255.53.60:35360 マルチキャスト（`IP_MULTICAST_LOOP=1`、`SO_REUSEADDR/SO_REUSEPORT`）、先頭 4 バイトのインスタンス ID で自分の送信を除外、送信時間は `Japan920Policy::airtime_ms` で `TxDone` を遅延、RSSI は −60 固定
  - `enum class RadioBackend { Hardware, UdpSim };  struct RadioHandle { std::unique_ptr<IRadio> radio; std::unique_ptr<CapPower> power; RadioInitError error = RadioInitError::None; };`
  - `RadioBackend default_radio_backend();`（`BATTLESHIP_LORA_HW` かつ環境変数 `BATTLESHIP_LORA_SIM` が `1` でない → Hardware、他は UdpSim）
  - `RadioHandle open_radio(RadioBackend backend);`（Hardware: `CapPower::acquire` 失敗 → `error = PowerFailed`。`Sx1262Radio` の初期化失敗 → その `init_error()` を返し、返す前に電源を持ち主ルールで戻す）

- [ ] **Step 1: 失敗するテスト** `UdpSimTwoInstancesExchange`: 同一プロセスに UdpSimRadio 2 つ → LoRaLink で接続しデータ往復（ネットワーク不可の CI では `GTEST_SKIP`）
- [ ] **Step 2:** 失敗確認 → **Step 3:** 実装 → **Step 4:** PASS
- [ ] **Step 5: Commit** `feat(lora): UDP multicast simulated radio and radio factory`

### Task 9: UI 組み込み（接続方式・Host/Join・エラー・アンテナ確認・i18n）

**Files:**
- Modify: `src/i18n/settings.{h,cpp}`（`NetTransportPref::Lora`、キー `lora`、`antenna_confirmed` の保存/読込）, `src/i18n/strings.h`, `src/i18n/strings_{en,ja,zh}.cpp`, `src/view/screens/net_transport_screen.cpp`（LoRa 行を選択可、BLE の device-only 表示とは無関係に常時有効）, `src/viewmodel/base_viewmodel.{h,cpp}`, 必要なら新規 `src/view/screens/net_antenna_screen.{h,cpp}` と `model::AppPage::NetAntennaConfirm`
- Modify: `tools/generate_cjk_fonts.py` 実行（新文字列の CJK サブセット再生成）
- Test: `tests/test_i18n_settings.cpp`, `tests/test_i18n_catalog.cpp`

**Interfaces:**
- Consumes: Task 3 `LoRaLink`, Task 4 `LoRaTransport`/`lora_session_config`, Task 8 `open_radio`/`RadioHandle`
- Produces（viewmodel 内部）: `bool net_using_lora() const;`, メンバ `RadioHandle net_lora_radio_; std::unique_ptr<LoRaLink> net_lora_link_; std::unique_ptr<LoRaTransport> net_lora_transport_; bool net_flow_lora_ = false;`。`net_peer_count()/net_peer_label()/join_host_at()` は LoRa 時 `peers()` を使う（ラベル `"<name>  <rssi> dBm"`）。`teardown_network()` で link.close→transport/link/radio 破棄→`CapPower` release。
- 新 StrId（EN / JA / ZH）:
  - `NetLoraHostingAs` "Recruiting over LoRa as %s ..." / "LoRa で募集中: %s ..." / "正在通过 LoRa 招募: %s ..."
  - `NetJoinScanningLora` "Listening for LoRa hosts..." / "LoRa のホストを探しています..." / "正在搜索 LoRa 主机..."
  - `NetLoraCapMissing` "Cap LoRa-1262 not found. Power off, attach it, then try again." / "Cap LoRa-1262 が見つかりません。電源を切って装着してから再試行してください。" / "未找到 Cap LoRa-1262。请关机安装后重试。"
  - `NetLoraUnavailable` "LoRa radio could not start." / "LoRa 無線を開始できませんでした。" / "无法启动 LoRa 无线。"
  - `NetLoraAntennaQuestion` "Is the antenna attached? Transmitting without it can damage the Cap." / "アンテナを取り付けましたか？付けずに送信すると Cap が壊れることがあります。" / "天线已安装吗？未安装天线发射可能损坏 Cap。"
  - `NetLoraAntennaConfirm` "Enter: Yes   Esc: Back" / "Enter: はい   Esc: 戻る" / "Enter: 是   Esc: 返回"
  - `NetTransportLora` の "(Coming soon)" 表示を外す

- [ ] **Step 1: 失敗するテスト**:
  - `test_i18n_settings`: `LoraPrefRoundTrips`（save→load で `Lora`）、`AntennaConfirmedPersists`、`UnknownTransportKeyFallsBackToBle`（既存挙動の確認）
  - `test_i18n_catalog`: 新 StrId が 3 言語すべてで非空（既存の網羅テストに含まれるならそれで可）
- [ ] **Step 2:** 失敗確認 → **Step 3:** settings・strings 実装 → PASS
- [ ] **Step 4:** viewmodel: `select_net_transport(Lora)` → 未確認ならアンテナ確認画面 → Enter で保存して NetworkMenu。`start_network_host/join` に LoRa 分岐（BLE 分岐と同形。`open_radio` 失敗時は `ExpanderMissing/RadioNoResponse` → `NetLoraCapMissing`、それ以外 → `NetLoraUnavailable` で NetError）。Host はロビー表示で `NetLoraHostingAs`（`BSHIP-<name>`）、Join は `NetJoinScanningLora` と peers 一覧。poll タイマーで `link.poll(now)` と peers 変化時の `net_hosts_dirty_subject` 更新。
- [ ] **Step 5:** 手動確認（デスクトップ、疑似無線）: ターミナル 2 つで `BATTLESHIP_WINDOW_ZOOM=2 ./build/desktop/battleship` を起動 → 両方で LoRa 選択 → A Host / B Join → 一覧に A → 参加 → 1 ゲーム完走 → 両方 Esc で戻る。`BATTLESHIP_NET_AUTOPILOT=win|lose` を使ってよい
  - `LoraHostShowsErrorWhenRadioMissing`: 実機ビルドを Cap 無しの本体で起動（または `BATTLESHIP_LORA_SIM` 無しで spidev の無い環境）→ NetError に `NetLoraCapMissing` か `NetLoraUnavailable`、ハングしない
- [ ] **Step 5b: 終了経路の電源片付け**: アプリ終了（タイトルの Esc 2 回、ランチャーの Esc 長押し＝シグナル終了、SIGTERM）で `teardown_network()` → `CapPower::release` が必ず走ることを確認。既存の終了処理がシグナルでデストラクタを通らない場合は、SIGTERM/SIGINT で終了要求フラグを立てメインループから通常終了させるハンドラを追加。実機で LoRa ロビー表示中に各経路で終了 → `ext_5v_out` が 0 に戻ることを確認（Task 10 Step 2 と合わせて実施可）
- [ ] **Step 6:** CJK フォント再生成、3 言語で LoRa 関連 4 画面（接続方式・アンテナ確認・ロビー・Join 一覧）のスクショを `docs/screenshots/` に保存し目視（はみ出し・豆腐なし）
- [ ] **Step 7:** 全テスト PASS → **Commit** `feat(lora): enable LoRa in Network Play (host/join, antenna confirm, i18n)`

### Task 10: L3 実機対戦 — ユーザーの 2 台が必要

**Files:** Modify: `NOTES.md`（「LoRa L3 実機対戦」章）、必要なら `lora_link.h` の `LinkTiming` / `lora_session_config()` の値

- [ ] **Step 1:** デバイス用 .deb をビルド（Task 11 の版番号前でもよい）→ 両機に `adb push` + `dpkg -i`
- [ ] **Step 2:** 近距離で LoRa 対戦 1 ゲーム完走（ユーザー操作）。終了後 `cat /sys/class/leds/ext_5v_out/brightness` が両機 0
- [ ] **Step 3:** BLE が届かない距離（別の部屋・階など）で 1 ゲーム完走。体感テンポと「撃つ→結果」往復の目安をユーザーに確認
- [ ] **Step 4:** 対戦中に片方を電源断 → もう片方が 20 秒以内に Connection lost
- [ ] **Step 5:** 片方で `battleship_lora_probe listen` を走らせ、もう片方の Host 募集ビーコンの ASCII 広告文が読めることを確認
- [ ] **Step 6:** 実測でタイミング値を調整した場合はテストを更新して PASS、NOTES に実測値・調整理由 → **Commit** `test(lora): on-device LoRa match results and timing tuning`

### Task 11: リリース準備（1.1.0-1）

**Files:**
- Modify: `CMakeLists.txt`（`project(battleship VERSION 1.1.0 ...)`）, `cmake/cm0-package.cmake`（`APP_DEBIAN_REVISION` を `"1"`）, `app-builder.json`（`version` `"1.1.0-1"`、3 言語の説明に LoRa を追記、`permissions.additional_hardware: true`）, `README.md`（LoRa の使い方・周波数/出力/ビーコン文言の明記・Cap とアンテナの注意）, `NOTES.md`, `PLAN.md`（Phase 8 行を Cap LoRa-1262 に更新し設計書へリンク）, ストア用スクショ（必要なら 4 枚のうち 1 枚を LoRa 画面に）
- 説明文追記（例）: EN "Optional: with a Cap LoRa-1262 attached you can also play over LoRa (920 MHz, Japan rules) beyond Bluetooth range." / JA "Cap LoRa-1262 を装着すれば、Bluetooth の届かない距離でも LoRa（920MHz）で対戦できます（任意）。" / ZH "可选：安装 Cap LoRa-1262 后，可在蓝牙范围之外通过 LoRa（920MHz）对战。"

- [ ] **Step 1:** 上記を更新し、デバイスビルドで `dist/battleship_1.1.0-1_arm64.deb` を作る（`-DAPP_DEBIAN_REVISION=1 -DAPP_MAINTAINER="yutacar <yutacar@users.noreply.github.com>"` を明示）
- [ ] **Step 2:** `dpkg-deb -f` で Package/Version `1.1.0-1`/Maintainer を確認、`lora_probe` が含まれていないことを `dpkg-deb -c` で確認
- [ ] **Step 3:** 実機に入れて BLE / WiFi / LoRa / 1P がそれぞれ起動することを確認（回帰）
- [ ] **Step 4:** 全テスト PASS → コミット → `feature/lora` を main にマージ（`--no-ff`）→ push
- [ ] **Step 5:** **ユーザーの指示を待って** czdev publish（`gh repo sync yutacar/packages --source CardputerZero/packages --branch main` → `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=url.https://github.com/.insteadOf GIT_CONFIG_VALUE_0=git@github.com: ~/work/AppBuilder/czdev publish --deb dist/battleship_1.1.0-1_arm64.deb`）
