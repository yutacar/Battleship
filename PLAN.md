# Battleship for CardputerZero — 設計・開発計画（Rev.3）

クラシック海戦ゲーム（バトルシップ）を CardputerZero 向けに開発する。

- **2人対戦**: CardputerZero 同士の通信対戦。**デフォルトは BLE**（内蔵 BT 4.2、AP 不要・追加ハード不要）。**オプションとして LoRa（Cap LoRa-1262 (SX1262) — 1.1.0 で実装済み、[設計書](docs/superpowers/specs/2026-11-27-lora-transport-design.md)）**（Rev.3 で BLE をデフォルトに変更。当初あった WiFi (TCP/IP) 対戦は 1.1.0 で削除）
- **1人対戦**: **古典的な確率密度 AI**（十分強く即応答）が対戦相手。**ローカル LLM は解説役**（戦況分析・解説コメント）を担う

---

## 1. ターゲット環境（調査結果）

| 項目 | 内容 |
|---|---|
| 本体 | M5Stack CardputerZero（Raspberry Pi CM0 搭載） |
| SoC / RAM | BCM2837 Quad-Core Cortex-A53 @1GHz (aarch64) / **512MB** LPDDR2 |
| OS | Raspberry Pi OS / Debian ベースの Linux（cardputer-zero-os） |
| 画面 | 1.9" LCD ST7789v3、**320×170** |
| 入力 | 46キー マトリックスキーボード（Linux では evdev 入力） |
| 無線 | **WiFi 2.4GHz 802.11b/g/n（内蔵・v1 の対戦手段）** / BT 4.2。LoRa は非内蔵 |
| 拡張 | Grove HY2.0-4P（UART/I2C 切替）、EXT 2.54-14P（SPI/GPIO）← 将来の LoRa ユニット接続口 |
| アプリ形態 | LVGL v9.5 + C++ / CMake。`.deb` パッケージとして配布。公式テンプレート（[CardputerZero/template](https://github.com/CardputerZero/template)）は SDL によるデスクトップビルド（エミュレータ開発）とデバイスビルド（DRM/fb）の両対応、MVVM 構成（model / view / viewmodel / platform） |
| ビルド/配布 | czdev CLI（デスクトップエミュレータ）、[CardputerZero-AppBuilder](https://github.com/m5stack/CardputerZero-AppBuilder)（オンラインビルド → .deb）、CardputerZero Hub AppStore に登録可能 |

---

## 2. アーキテクチャ設計

公式テンプレートをベースに、以下のレイヤ構成とする。**ゲームロジック・通信・AI・UI を完全分離**し、デスクトップ（SDL）でほぼ全機能を開発・テストできるようにするのが最重要方針。

```
┌──────────────────────────────────────────────┐
│ View / ViewModel (LVGL v9)                   │  画面・入力
├──────────────────────────────────────────────┤
│ GameController                               │  状態遷移・ターン管理
├──────────────┬───────────────────────────────┤
│ GameCore     │ IOpponent                     │
│ (純粋ロジック) │  ├ RemotePlayer ── ITransport  │── TcpTransport(WiFi/v1)
│ 盤面・艦・判定 │  │                            │── Loopback(テスト)
│              │  │                            │── LoRaSerial(将来) / BLE(将来)
│              │  └ AIPlayer ── 確率密度エンジン  │
├──────────────┴───────────────────────────────┤
│ Commentator（解説役・任意）── ローカルLLM        │  非同期・ゲーム進行を止めない
└──────────────────────────────────────────────┘
```

### 2.1 GameCore（純粋ロジック・依存ゼロ）

- 10×10 盤 ×2（自艦隊盤 / 記録盤）、艦5隻（空母5・戦艦4・巡洋艦3・潜水艦3・駆逐艦2）
- 配置バリデーション（縦横直線・重なり禁止・盤内）
- 射撃判定（Hit / Miss / Sunk(艦種) / 勝敗）
- I/O・LVGL・スレッドに依存しない C++ クラス群 → **ユニットテストを厚く書く**

### 2.2 通信レイヤ — トランスポート抽象化が要

**プロトコル（トランスポート非依存の共通フレーム）**:

```
[MAGIC(2B)] [ver(1B)] [type(1B)] [seq(1B)] [len(1B)] [payload] [CRC16(2B)]
```

- メッセージ種: `HELLO`（マッチング/先攻決定：乱数交換）, `READY`（配置完了）, `SHOT(x,y)`, `RESULT(hit|miss|sunk+艦種)`, `GAME_OVER`, `ACK`, `PING`
- **信頼性層をアプリ側に持つ**: seq 番号 + ACK + タイムアウト再送（stop-and-wait）。TCP 上では冗長だが、**将来の LoRa（ロスあり・低帯域・半二重）でもプロトコル無変更で動かすため**、最初からこの設計とする
- 相手の盤面情報は送らず、判定は各自の手元で実施 → ペイロード数バイト/ターン。LoRa の帯域でも成立する

**トランスポート実装（`ITransport` の差し替え）**:

| 実装 | 時期 | 内容 |
|---|---|---|
| `LoopbackTransport` | 実装済み（開発） | 同一プロセス内テスト・AI 自動対戦 |
| `TcpTransport` | 実装済み（**オプション**） | WiFi 経由。ホスト側が待受、UDP ブロードキャストで LAN 内対戦相手を発見（同一 AP 前提）。**デスクトップ開発・自動検証の主経路**としても維持 |
| `BleTransport` | **Phase 6（デフォルト）** | **BT 4.2 内蔵を活用（追加ハード・AP 不要）— 製品のデフォルト接続方式**。Linux 側は BlueZ（D-Bus API）。GATT の Nordic UART Service (NUS) 互換（TX/RX キャラクタリスティック）でバイトストリーム化し、Host=ペリフェラル（アドバタイズ）/ Join=セントラル（スキャン）。ペイロードは数バイトなので MTU 20B でも1フレーム1通知で収まる。**Mac デスクトップでは実 BLE 検証不可** → 実装はデバイス向け、ロジックはヘッドレステスト、実通信は実機2台で検証 |
| `LoRaTransport` | **Cap LoRa-1262 (SX1262) — 1.1.0 で実装済み**（[設計書](docs/superpowers/specs/2026-11-27-lora-transport-design.md)）。`TcpTransport`（WiFi）は 1.1.0 で削除 | （当初計画: M5Stack Unit LoRaE220-JP（920MHz・Grove UART・技適対応）。Linux からはシリアルポート。透過モードで P2P。**インターフェースは確定済み、実装のみ後日追加**） |

**マッチング UI の接続方式選択**: Network Play 画面で接続方式を選択 — **デフォルト選択は BLE**、オプションで WiFi、LoRa は Phase 8 まで無効表示。

### 2.3 AI（1人対戦の相手）— 確率密度エンジン

対戦相手は**古典的な確率密度方式**。強く・即応答・省メモリで、512MB RAM でも余裕がある。

- **Hunt モード**: 残存艦が置けるパターン数を各マスで数え、最尤マスを撃つ（パリティ最適化込み）
- **Target モード**: ヒット後は隣接探索→直線延長で撃沈を狙う
- **難易度3段階**（Easy / Normal / Hard）: ランダム性の混合率と確率マップの精度で調整
- AI 同士の自動対戦（ヘッドレス）で統計テスト（平均手数・破綻なし）を CI に組み込む

### 2.4 定型文解説（Commentator）— LLM は廃止、コードのみ温存

解説役は**対戦相手ではなく実況役**。ゲーム進行の判断には一切関与しない。

- **役割**: 戦況の解説コメント生成。例: 命中/撃沈時の一言、開始/終局時の一言
- **実装**: Phase 3b〜5では llama.cpp + 小型量子化モデル（SmolLM2-360M クラス）による生成も実装したが、**その後の判断で EN/JA/ZH の全言語について LLM 解説を廃止し、定型文（`TemplateCommentator`）に統一**した（速度・再現性・配布サイズを優先。詳細は NOTES.md「LLM 廃止」節）。`commentator_factory.cpp` は言語・ビルドフラグに関わらず常に `TemplateCommentator` を返す
- **LLM コードの扱い**: `LlmCommentator` 一式（`src/commentary/llm_commentator.*`、`build_llm_prompt()` 等）はソースツリーに温存し、CMake オプション `BATTLESHIP_LLM`（**既定 OFF、全プラットフォーム共通**）でビルド対象・テストを切り替え可能な状態を維持。将来 LLM 解説を復活させる場合はこのフラグを ON にした上で `commentator_factory.cpp` を復元すればよい
- 表示は戦闘画面下部の解説行（スクロール/送りキー）。イベントごとに複数バリエーションの定型文をシード付き乱数で選択（EN/JA/ZH とも同数のバリエーションを持つ）

### 2.5 UI（320×170 横画面）

| 画面 | 内容 |
|---|---|
| タイトル/メニュー | 1人用（難易度選択・解説 ON/OFF）/ 2人用（WiFi 対戦: ホスト/参加）/ 設定 |
| 配置画面 | 10×10 盤 + 艦リスト。矢印キーで移動、`R` で回転、Enter で確定、ランダム配置キー |
| 戦闘画面 | **左: 記録盤（攻撃用）/ 右: 自艦隊盤** を並置（14px セル×10 + ラベルで 320px に収まる）。カーソル移動 + Enter でコール。下部1行にステータス/解説コメント |
| マッチング画面 | 2人用: LAN 内探索 → 相手選択/待受。接続状態表示 |
| リザルト | 勝敗・命中率・LLM 総評（ON 時）・再戦 |

- 通信対戦時は相手待ちインジケータ、切断検出（PING 途絶）→ 再接続/中断ダイアログ

---

## 3. 開発フェーズ計画

**エミュレータファースト**で進める。Phase 0–5 は Mac 上で完結でき、実機は Phase 4 後半（WiFi 実対戦）と Phase 6 で使用。
実装作業は **Sonnet 5 のサブエージェント**に委任して実施する（本セッションのオーケストレータが設計・レビュー・統合を担当）。

| Phase | 内容 | 完了条件 |
|---|---|---|
| **0. 環境構築** | template を取得しプロジェクト初期化（git init）、macOS で SDL デスクトップビルドが通ることを確認、czdev エミュレータ確認 | デスクトップでテンプレートアプリが起動 |
| **1. ゲームコア** | GameCore 実装 + ユニットテスト（配置検証・判定・撃沈・勝敗） | 全テスト green |
| **2. UI 一式** | 配置画面・戦闘画面・リザルト。まずホットシート（1台交互プレイ）で全画面フロー成立 | エミュレータで人間同士が1台で遊べる |
| **3. AI 対戦 + LLM 解説** | 確率密度エンジン + 難易度3段階 → 1人用モード完成。LLM 解説役を feature flag 付きで統合、実機メモリ実測で搭載判断 | 1人用が完成、AI 自動対戦テストで破綻なし、解説が非同期で表示される |
| **4. WiFi 通信対戦** | プロトコル + 再送層 + `TcpTransport` + LAN 内マッチング。2プロセス対戦テスト（Mac）→ 実機2台で実対戦 | 実機2台の WiFi 対戦で1ゲーム完走、切断時も破綻しない |
| **5. 多言語対応（i18n）** | **英語を標準**とし、設定画面で**日本語・中国語（簡体字）**に切替可能に。文字列カタログ層の導入、CJK フォント対応、言語設定の永続化。LLM 解説・定型文も選択言語に追従 | 3言語で全画面が正しく表示・切替でき、再起動後も言語設定が保持される |
| **6. BLE 対戦（デフォルト接続方式）** | `BleTransport` 実装（BlueZ D-Bus、NUS 互換 GATT、Host=ペリフェラル/Join=セントラル）+ マッチング画面に接続方式選択（**デフォルト BLE** / WiFi / LoRa=無効表示）。プロトコル・信頼性層・NetSession は無変更で流用。デスクトップはモック/Loopback でヘッドレステスト、デバイスビルドで BlueZ 実装をコンパイル | ヘッドレステスト green + デバイスビルド成功（実機2台での BLE 実対戦はユーザー実機到着後に実施） |
| **7. パッケージング** | aarch64 デバイスビルド、**APPLaunch 規約準拠**の .deb 化、czdev による提出（下記「Phase 7 の提出要件」参照） | prepublish check 全パス、実機で APPLaunch から起動、czdev publish で PR 作成 |
| **8. LoRa 対応** | **Cap LoRa-1262 (SX1262) — 1.1.0 で実装済み**（[設計書](docs/superpowers/specs/2026-11-27-lora-transport-design.md)）。当初案の Unit LoRaE220-JP は不採用。WiFi 対戦（Phase 4 の TcpTransport）は 1.1.0 で削除。接続方式選択の LoRa を有効化 | LoRa 経由で1ゲーム完走 |

### Phase 5（i18n）の設計方針
- **文字列カタログ**: 文字列 ID → 言語別テーブルの軽量な自前実装（gettext 等は導入しない）。UI 実装時からハードコード文字列をカタログ経由に置換
- **フォント**: 英語は既存フォントのまま。日本語・中国語は CJK グリフが必要 — テンプレートに FreeType があるため TTF 実行時レンダリング（Noto Sans CJK のサブセット）か、`lv_font_conv` による使用グリフ限定のビットマップフォント事前生成のどちらかを実装時に検証して選択。**.deb サイズと RAM 消費を考慮し、使用グリフのみのサブセット化を必須とする**
- **切替 UI**: メニューの「Settings」を有効化し、Language: English / 日本語 / 简体中文 を選択。設定は設定ファイルに永続化（テンプレートの config 機構を利用）し、起動時に復元。標準（未設定時）は英語
- **LLM 解説との連携**: 解説生成プロンプトと定型文フォールバックも選択言語に追従（Phase 3b の実装に言語パラメータを用意しておく）

### Phase 7 の提出要件（公式 App Submission Guide 2026-07 確認済み）
公式ガイド: https://cardputerzero.github.io/#/documents/app-submission-guide （実体: cardputerzero.github.io リポジトリ docs/app-submission-guide.md）

- **パッケージレイアウト（要修正）**: `.deb` は `/usr/share/APPLaunch/` 配下に配置 — `applications/<app>.desktop`（Name/Exec/Icon/Type=Application/Terminal=false）、実行体は `bin/`、正方形 PNG アイコンは `share/images/`、フォント等アセットは `share/`。**テンプレート由来の systemd サービス方式は廃止**（常駐サービスは審査対象でもある）。引数・作業ディレクトリ・FB 環境設定は Exec でなくラッパースクリプトに
- **control フィールド**: Package / Version / `Architecture=arm64` / Maintainer（**メールは GitHub アカウントと一致必須** — email_mismatch で弾かれる）
- **app-builder.json**（ソースリポジトリに必須）: `package_name`, `app_name`, `store{summary, categories, icon, screenshots, locales}`。**locales は zh-CN / en / ja の3言語**（title/summary/description）— アプリ内 i18n と揃える
- **ストアスクリーンショット**: **ちょうど4枚、320×170 のクリーンなフレームバッファ画像**（枠・デバイス画像・黒帯・拡縮禁止）→ lv_snapshot 撮影画像で適合
- **Esc キー規約（要実装）**: 短押し Esc = 1階層戻る/モーダルを閉じる、長押し Esc または Home = 安全終了
- **fbdev**: `LV_LINUX_FBDEV_DEVICE` を尊重、`/dev/fb0` ハードコード禁止
- **LLM モデルは .deb に同梱しない**（256MB は配布に不適切）: モデル無しで定型文解説にフォールバックする現行設計のまま出荷し、モデルは任意配置（README に手順）として申告
- **提出フロー**: prebuilt czdev を導入 → `prepublish_check.py` の ERROR 全解消（必須）→ 実機検証チェックリスト（APPLaunch からの起動/終了、CJK 豆腐なし、アンインストール確認等）→ `czdev login`（GitHub OAuth、ユーザー操作）→ `czdev publish --deb` → `CardputerZero/packages` への PR をメンテナが審査
- **メタデータ申告**: 通信は **BLE（デフォルト、近距離 P2P）+ WiFi オプション（UDP 35353 / TCP 35354、LAN 内 P2P のみ・外部送信なし）**、外部ハード（将来の LoRa ユニット）、バックグラウンドサービスなし、プライバシー（データ収集なし）を PR に明記

### フェーズ間の依存
- Phase 1 → 2 → 3 は直列。Phase 4 のプロトコル + TCP 実装は Phase 2 と並行着手可能
- Phase 5（i18n）は Phase 3 完了後に着手可能（Phase 4 と並行可）
- Phase 6（BLE）は Phase 4 完了後に着手可能。Phase 7（パッケージング）は Phase 5・6 の完了後（BLE の**実機実対戦のみ**は実機到着待ちで、パッケージング作業とは並行可）
- Phase 8（LoRa）は Phase 7 リリース後の拡張。**v1 の設計判断（信頼性層・数バイトペイロード）が低帯域・ロスあり回線前提で作られているため、追加実装はシリアル入出力の薄い層のみ**

### 必要な物品（ユーザー側で準備）
- CardputerZero 実機 ×2（Phase 4 後半以降。1台でも AI 対戦までは検証可）
- Unit LoRaE220-JP ×2 + アンテナ（Phase 8 時点で。入手可能と確認済み）
- ※Phase 0–4 前半は Mac 上のエミュレータのみで進行可能

---

## 4. リスクと対策

| リスク | 対策 |
|---|---|
| RAM 512MB で LLM 解説が載らない | 解説は feature flag 付きの付加機能。フォールバック定型文で OFF でも成立。実測で最終判断 |
| LLM 生成が遅い（A53 @1GHz） | 完全非同期・短文生成（1-2文）・ゲーム進行と分離。間に合わなければその手の解説はスキップ |
| LAN 対戦の相手発見が環境依存（AP のクライアント分離等） | UDP ブロードキャスト + IP 直接入力のフォールバック。将来 AP モード/BLE も検討 |
| 将来の LoRa（ロス・低帯域・半二重） | v1 から stop-and-wait ACK/再送・数バイトペイロードで設計し、トランスポート差し替えのみで対応 |
| 170px 高で 10×10 が見づらい | 14px セルで実寸検証をエミュレータで早期に実施。カーソル座標の拡大表示を併用 |
| テンプレート/czdev の仕様変更・情報不足 | Phase 0 で実際にビルドして早期検証。詰まったら Emulator/launcher リポジトリのソースを参照 |
| CJK フォントによる .deb サイズ・RAM 増（Phase 5） | 使用グリフのみサブセット化を必須とし、実測でサイズ確認。日中で共有できるグリフは共有 |
| BLE（BlueZ D-Bus の GATT ペリフェラル実装）が複雑・Mac で実通信検証不可（Phase 6） | NUS 互換の最小実装に絞る。ITransport 抽象でロジックはヘッドレステスト済みのため、リスクは BlueZ 連携部分に限定。実機2台での検証項目を NOTES に整備し、失敗時も WiFi がフォールバックとして常に利用可能 |
| 920MHz 帯の法規制（Phase 8） | 技適取得済みの M5Stack 純正 E220-JP を使用（自作モジュール不可） |
