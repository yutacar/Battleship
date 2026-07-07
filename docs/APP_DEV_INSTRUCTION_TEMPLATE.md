# CardputerZero アプリ開発 指示書テンプレート

> **使い方**: `{{ }}` の箇所をテーマに合わせて書き換え、この文書全体を
> Claude Code（Sonnet 5 / Opus 4 系）への最初の指示として渡す。
> このテンプレートは Battleship プロジェクト（2026-07、
> github.com/yutacar/Battleship）で確立した進め方・知見の集約版。
> 疑問があれば当該リポジトリの PLAN.md / NOTES.md が実例になる。

---

## 0. 今回のテーマ（毎回ここだけ書き換える）

- **アプリ名**: {{アプリ名（例: Battleship）}}
- **概要**: {{1〜3文。何をするアプリか}}
- **機能要件**:
  - {{箇条書きで。例: 1人用モードは確率密度AIが対戦相手、難易度3段階}}
  - {{例: 2人対戦はBLEデフォルト・WiFiオプション}}
  - {{例: UIは英語標準、設定で日本語・简体中文に切替、永続化}}
- **対象外（やらないこと）**: {{例: クラウド連携、課金、LoRa は将来}}
- **参考にする既存アプリ/仕様**: {{URL や説明があれば}}
- **ユーザー環境**: macOS (Apple Silicon) + Docker。実機は {{あり/なし/後日}}。

---

## 1. 進め方の原則

1. **計画から始める**。実装前に `PLAN.md` を作成し、アーキテクチャ・
   フェーズ分割・完了条件・リスクを提示してユーザーの承認を得る。
   要件変更が来たら PLAN.md を改訂（Rev 番号を進める）してコミットする。
2. **エミュレータファースト**。SDL デスクトップビルド（Mac）で全機能を
   開発・検証し、実機依存の要素（BLE 等）だけを後回しの検証項目として
   明示的に残す。
3. **フェーズ制**。各フェーズに客観的な完了条件（テスト green、
   スクリーンショット確認等）を定め、完了条件を満たすまで次に進まない。
   標準フェーズ順は §6 参照。
4. **こまめにコミット**。大きな区切りごとに意味単位でコミットする
   （セッション中断への保険。1フェーズ1コミットは大きすぎる）。
   コミットメッセージ末尾: `Co-Authored-By: <モデル名> <noreply@anthropic.com>`
5. **記録を残す**。`NOTES.md` に各フェーズの完了状態・ビルド/検証手順・
   発見したバグと原因・実測値（サイズ/レイテンシ等）を追記していく。
   後続セッションはまず PLAN.md と NOTES.md を読むこと。
6. **検証は自分の目で**。UI 変更は必ずスクリーンショットを撮って
   Read（画像表示）で目視確認する。「ビルドが通った」は完了条件ではない。
7. **ユーザーに聞くのは本人にしか決められないことだけ**（§7）。
   技術的判断は自分で下し、判断理由を報告する。
8. **メタ情報は実態と一致させる**。ストア説明文・README に実装されて
   いない機能を書かない（審査の拒否事由）。機能を削ったら説明文も直す。

---

## 2. プラットフォーム前提知識（CardputerZero）

| 項目 | 内容 |
|---|---|
| 本体 | M5Stack CardputerZero（Raspberry Pi CM0 / BCM2837、A53×4 @1GHz、**RAM 512MB**） |
| OS | Debian ベース Linux（cardputer-zero-os、glibc 2.41 = **trixie 世代**） |
| 画面 | **320×170**（ST7789v3）。実寸での判読性検証が必須 |
| 入力 | 46キー物理キーボード（Linux では evdev）。タッチなし |
| 無線 | WiFi 2.4GHz / BT 4.2（BLE は BlueZ D-Bus、sd-bus 推奨）。LoRa 等は外付け（Grove UART / EXT SPI） |
| アプリ形態 | **LVGL v9.5 + C++ / CMake**。`.deb` で配布、Hub AppStore に登録 |
| 公式テンプレート | https://github.com/CardputerZero/template （SDL デスクトップ + デバイスビルド両対応、MVVM 構成） |

### ビルドコマンド（テンプレート準拠）
- macOS: `cmake --preset darwin-arm64` → `cmake --build --preset darwin-arm64-dbg`（または `-rel`）
- テスト: `build/darwin-arm64` で `ctest -C Debug`（テンプレートにテスト基盤は**無い**ので GoogleTest + CTest を FetchContent で自分で導入する）
- デバイス: Docker **debian:trixie** arm64 ネイティブコンテナ（**bookworm は glibc 不一致で不可**）+ BSP sysroot（M5CardputerZero-UserDemo releases の sdk_bsp.tar.gz、**展開後の `sysroot/` サブディレクトリ**を `-DCM0_SDK_ROOT` に渡す）、`-DUSE_DESKTOP=OFF` → `cpack -G DEB`

### RAM 512MB の含意
- ローカル LLM は実用にならない（Battleship では評価の上で全廃し定型文にした）。重い常駐処理・大アセットを避ける
- CJK フォントは**使用グリフのみサブセット化必須**（fontTools。Battleship の `tools/generate_cjk_fonts.py` が実例。JA+ZH 合計 ~160KB に収まる）

---

## 3. 開発ルール

### アーキテクチャ
- **ロジック / 通信 / UI を層分離**し、ロジック層は LVGL 非依存の純粋 C++ に
  してユニットテストを厚く書く（Battleship: `src/core/` 方式）
- 通信があるなら `ITransport` 抽象を最初に切る（TCP=開発用 / BLE=実機 /
  Loopback=テスト用に**故意のロス・重複・分断注入フックを付ける**）。
  信頼性層（seq/ACK/再送）をアプリ側に置くと低帯域トランスポートへの
  差し替えが効く
- 設定は `~/.config/<app>/settings.conf` に key=value 永続化。
  保存は**全キー書き戻し**（片方の設定保存が他方を消す事故の防止）

### UI（320×170・キーボードのみ）
- キー操作ガイドを画面内に常時表示（画面ごとに関係するキーだけ）
- **Esc 規約（提出要件）**: 短押し=1階層戻る/モーダルを閉じる（対戦・作業中は確認モーダル）、長押し(~1s) or Home=安全終了
- i18n: 文字列 ID → 言語テーブルの自前カタログ（EN 標準 / JA / ZH）。
  動的文字列は語順が変わるためテンプレート化。**全 StrId × 全言語の
  欠落検出テスト**を書く。文言追加時は CJK フォント再生成を忘れない
- デバッグ用に **APP_SCRIPT 方式**（環境変数でキー合成 + `lv_snapshot`
  スクリーンショットをスケジュール実行）を早期に整備すると、以降の
  全フェーズの UI 検証・自動走行・ストア用撮影が自動化できる
- デスクトップは `BATTLESHIP_WINDOW_ZOOM` 相当（`lv_sdl_window_set_zoom`）
  の整数倍拡大オプションがあるとテストプレーしやすい（論理解像度は不変）

### 検証
- ユニットテスト green を各フェーズの完了条件に含める
- 対戦/自動処理系はヘッドレスの統計テスト（多数シード実行）で担保
- UI は3言語 × 主要画面のスクリーンショットを撮り目視確認
  （豆腐・はみ出し・重なり。CJK は英語よりラベルが長く行送りも ~17px と
  高いことに注意）

---

## 4. 既知の落とし穴（Battleship で実際に踏んだもの）

1. **Homebrew fmt 12.2+**: `fmt/core.h` だけでは `fmt::format` が見えない → `fmt/format.h` を include
2. **`LV_LABEL_LONG_MODE_DOTS` はレンダースレッドを無限ループさせ得る**（特定テキスト幅で再現、アプリ全体フリーズ）→ CLIP/WRAP を使う。DOTS 禁止
3. **sdl2-compat（Homebrew の SDL2 実体）**: 合成 `SDL_TEXTINPUT` の `SDL_PushEvent` が SIGSEGV → キー合成は LVGL の `lv_sdl_keyboard_handler()` 直接呼び出しで
4. **デバイスビルドは debian:trixie**（bookworm は glibc 2.36 でヘッダ衝突）。BSP は `sysroot/` サブディレクトリをマウント
5. **FetchContent の外部ライブラリに `EXCLUDE_FROM_ALL`** を付けないと、その `install()` が .deb に混入して肥大化する
6. **CPack `SHLIBDEPS OFF` の手動 Depends は実バイナリの `readelf -d`（NEEDED）と突き合わせて棚卸し**する。クリーンな arm64 コンテナで `apt install ./x.deb` → `ldd` → `apt remove` まで通すこと
7. **macOS は UDP ブロードキャストを同一ホストにループバックしない** → 開発時は 127.0.0.1 にも announce
8. **`/dev/fb0` をハードコードしない**。`LV_LINUX_FBDEV_DEVICE` を実行時に読む（提出要件）
9. スクリーンショットのファイル名生成は入力をトリム・サニタイズ（改行混入事故の実例あり）
10. LVGL perf monitor（FPS 表示）はデスクトップ設定のみ ON にし、デバイス用 `lv_conf` では OFF（提出物に出さない）

---

## 5. パッケージング・AppStore 提出要件（公式ガイド準拠）

公式文書（SPA の URL では取得不可。**raw.githubusercontent.com の
CardputerZero/cardputerzero.github.io リポジトリ docs/*.md を読む**）:
- app-submission-guide.md / developer-submission-policy.md / appstore-registry-requirements.md

要点:
- **.deb レイアウト**: `/usr/share/APPLaunch/applications/<app>.desktop`（Type=Application, Name, Exec, Icon, **Terminal=false**）+ `bin/`（**ラッパースクリプト経由**で実行体を起動。引数・環境設定はラッパーに）+ `share/images/` に**正方形 PNG アイコン**。**systemd サービスは入れない**（テンプレート既定から要撤去）
- **control**: Package（小文字）/ Version / `Architecture=arm64` / **Maintainer のメールは公開者 GitHub アカウントの確認済みメールと一致必須**（yutacar の場合: `yutacar@yutacar.net`。email_mismatch で弾かれる）
- **app-builder.json**: `package_name` / `app_name` / `store{summary, categories, icon, screenshots, locales}`。**locales は zh-CN / en / ja**。icon/screenshots のパスは**リポジトリルート相対**
- **ストア用スクリーンショット: ちょうど4枚、320×170 のクリーンな
  フレームバッファ画像**（枠・FPS表示・拡縮禁止。lv_snapshot 出力が最適）
- **prepublish check 必須**: `CardputerZero/skill` リポジトリの
  `cardputer-app-publish/scripts/prepublish_check.py --deb <deb> --app-dir .`
  を ERROR 0 まで
- **czdev**: m5stack/CardputerZero-AppBuilder の Releases にプレビルドあり
  （macOS arm64 含む）。`czdev doctor` → `czdev login`（GitHub OAuth、
  ユーザー操作）→ `czdev publish --deb <deb>`（CardputerZero/packages に
  PR が立つ）
- **実機検証は PR 前に必須**（APPLaunch から起動/終了・実 LCD 表示・
  CJK 豆腐なし・Esc 規約・アンインストールで痕跡なし）。PR にテスト
  機材・OS を記載する欄がある
- **ポリシー**: ネットワーク/データ/外部ハード/常駐の申告は正確に。
  誤解を招く説明文は拒否事由。個人データ収集なし・LAN 内 P2P のみ等は
  その旨を PR に明記。ソース公開度も申告（公開リポジトリ推奨）

---

## 6. 標準フェーズ構成（テーマに合わせて増減）

| Phase | 内容 | 完了条件の例 |
|---|---|---|
| 0 | 環境構築: テンプレート取り込み・git init・macOS SDL ビルド | デスクトップでテンプレートアプリ起動 |
| 1 | コアロジック + テスト基盤新設（GoogleTest/CTest） | 全テスト green |
| 2 | UI 一式（ローカル完結モードで全画面フロー成立）+ APP_SCRIPT/スクリーンショット機構 | 通しプレイ可、全画面スクショ目視OK |
| 3 | {{テーマ固有機能（AI、センサー連携等）}} | 統計テスト/実測で担保 |
| 4 | 通信（必要なら）: プロトコル+TCP で開発 → 2プロセス実対戦 | ロス注入 E2E green、2プロセス完走 |
| 5 | i18n（EN/JA/ZH）+ Settings + フォントサブセット | 3言語全画面スクショOK、設定永続化 |
| 6 | 実機系トランスポート（BLE 等、Linux 専用コード） | ヘッドレステスト green + Linux ビルド成功 |
| 7 | パッケージング・提出準備（§5 全項目 + README/LICENSE/GitHub） | prepublish passed、クリーンルーム install/remove OK |
| 8 | 実機検証 → publish（ユーザーと共同） | 実機チェックリスト完走、PR 提出 |

---

## 7. ユーザーに確認すべきこと（それ以外は自分で判断）

- 要件・仕様の変更/追加（勝手に機能を増減しない。ただし提出要件への
  適合修正は判断して実施し報告）
- 購入が必要なハードウェア
- GitHub リポジトリの**公開/非公開**と名前（公開サーフェス作成は必ず確認）
- `gh auth login` / `czdev login` 等の OAuth 認証（ユーザー操作）
- Maintainer メールアドレス（GitHub 確認済みメールであること）
- 実機検証の実施タイミング

## 8. 完了報告のフォーマット

各フェーズ完了時に必ず: (1) 何を作ったか、(2) 検証方法と結果（テスト数・
スクショパス・実測値）、(3) 発見した問題と対処、(4) コミット一覧、
(5) 次フェーズへの引き継ぎ事項 — を簡潔に報告する。
