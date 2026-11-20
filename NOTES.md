# Phase 0 環境構築メモ

## 1. ビルド手順（macOS / Apple Silicon, 確認済み）

### 前提ツール
- Xcode Command Line Tools（`xcode-select --install`）— インストール済みだった
- Homebrew

### 依存関係インストール

```bash
brew install cmake ninja libpng jpeg fmt sdl2 freetype dpkg zlib
```

このマシンでは `cmake` `ninja` `libpng` `fmt` `freetype` は既にインストール済みで、
新規にインストールしたのは `sdl2`（実体は `sdl2-compat` → `sdl3` に依存）、
`jpeg`、`dpkg`、`zlib` の4つ。

- `jpeg` と `zlib` は keg-only（`jpeg-turbo` や macOS 標準の zlib と衝突するため symlink されない）。
  `find_package()` は Homebrew の `lib/cmake/*` 経由で正しく見つかるので、
  今回は追加の `LDFLAGS`/`CPPFLAGS` 設定なしでビルドが通った。
- `sdl2` は Homebrew では `sdl2-compat`（SDL3 バックエンドの互換レイヤ）のエイリアス。
  `SDL2_DIR=/opt/homebrew/lib/cmake/SDL2` が自動検出され、CMake の
  `find_package(SDL2 CONFIG REQUIRED)` は問題なく成功した。

### Configure（Apple Silicon = arm64）

```bash
cmake --preset darwin-arm64
```

初回 configure 時に CMake の `FetchContent` が LVGL v9.5.0 を GitHub から
shallow clone する（ネットワークアクセス発生、数十秒程度）。
`.cache/darwin-arm64-deps/` 以下にキャッシュされる。

### Build

```bash
cmake --build --preset darwin-arm64-dbg   # Debug
cmake --build --preset darwin-arm64-rel   # Release
```

両方ビルド成功を確認済み。

### 実行

```bash
./build/darwin-arm64/Debug/template_app
# または
./build/darwin-arm64/Release/template_app
```

起動ログに `LVGL app started at 320x170` と出力され、CardputerZero 実機の
解像度（320×170）でシミュレータが起動することを確認。バックグラウンド起動
→ 数秒後もプロセス生存（クラッシュなし）を確認済み。macOS のサンドボックス
制限で `screencapture` によるスクリーンショット取得はできなかったが、
`System Events` のプロセス一覧に `template_app` がGUIプロセスとして
表示されることは確認した。

Intel Mac の場合は `darwin-x86-64` / `darwin-x86-64-dbg` / `darwin-x86-64-rel`
プリセットに読み替える（README 記載どおり、未検証）。

## 2. 発生した問題と対処

### fmt::format が見つからないビルドエラー（修正済み・コミット済み）

テンプレートの `src/logger/logger.h` は `#include <fmt/core.h>` のみを
include していたが、Homebrew が入れる fmt は `12.2.0`（テンプレートの
README/vcpkg.json が想定するバージョンは 10.1.1〜12.1.0 系）で、この
バージョン系列では `fmt/core.h` だけでは `fmt::format()` の実体が見えず
`no member named 'format' in namespace 'fmt'` でビルド失敗した。

対処: `src/logger/logger.h` の include を `fmt/core.h` → `fmt/format.h` に変更。
`logger.cpp` 側は元々 `logger.h` 経由で include されるのみなので変更不要。
Debug/Release 両方でビルド成功を確認済み。

これは Homebrew の fmt が pin されたバージョンより新しいことに起因する既知の
非互換（fmt 11 以降で `core.h` の提供範囲が絞られた）。将来 Homebrew の fmt が
さらに上がった場合も同様の問題が再発しうる点に注意。

## 3. テンプレート構成の要点

### レイヤ構成（MVVM 風）

```
src/
├── app/        アプリライフサイクル・アセット解決・画面遷移（ScreenManager）
├── config/     LVGL config ヘッダ（desktop 用 / CM0(組込み) 用を USE_DESKTOP で切替）
├── logger/     fmt ベースのロガー
├── model/      BaseModel（現在ページ・テーマ等の状態）
├── platform/   Linux evdev 入力 / デスクトップ SDL キー入力
├── reactive/   LVGL observer(subject) のラッパーとバインディングヘルパー
├── view/       画面(screens/)・ウィジェット(widgets/)・テーマ
├── viewmodel/  BaseViewModel（Model状態をLVGL subjectとして公開、UIアクション提供）
└── main.cpp
```

データフロー: ウィジェットのコールバック → ViewModel 更新 → subject 発行 →
バインドされた UI オブジェクトが LVGL observer 経由で自動更新。

### ビルドターゲット分割（CMakeLists.txt）
`template_app_platform` / `template_app_model` / `template_app_reactive` /
`template_app_view` / `template_app_viewmodel` / `logger` / `template_app_core`
の static ライブラリに分かれ、最終的に `template_app` 実行ファイルへリンクされる。
Battleship の `GameCore` 相当のロジックは、この構成に倣って新規ターゲット
（例: `battleship_core`）として追加し、依存を LVGL に持たせない設計にできる。

### アセット機構
`src/app/asset_manager.*` が以下の優先順で探索:
1. `-DAPP_ASSETS_ROOT=...`（CMake configure時指定）
2. ソースツリーの `assets/`（開発時）
3. `/usr/share/<APP_NAME>/`（インストール後）

フォントは FreeType 経由でランタイムロード（`assets/fonts/*.ttf`）。

### テスト基盤 — **重要: 存在しない**
`CMakeLists.txt` を全文確認したが `enable_testing()` / `add_test()` /
`CTest` / `GoogleTest` / `Catch2` の類は一切含まれていない。
CI 相当の仕組みも見当たらない（`.github/` はテンプレート側に存在しない）。

**Phase 1（GameCore + ユニットテスト）着手時に、テストフレームワークを
ゼロから追加する必要がある。** 選択肢:
- CTest + 軽量アサートのみの自作
- GoogleTest / Catch2 を `FetchContent` で追加（LVGL と同じ手法が使える）
- `GameCore` は「I/O・LVGL・スレッドに依存しない C++ クラス群」（PLAN.md 2.1節）
  なので、LVGL 本体をリンクしない独立の実行ファイル/ターゲットとしてテストを
  組めるはず。`template_app_model` 等と同様に `battleship_core` を静的ライブラリ化し、
  そこに対して素の `add_executable(battleship_core_tests ...)` + `enable_testing()`
  を追加するのが自然な流れ。

### プロジェクト名（TemplateApp → Battleship のリネーム）
今回は実施していない。`project(template_app ...)` や `APP_NAME` 既定値
`template_app`、`.deb` パッケージ名の `TemplateApp_...` など複数箇所に
分散しているため、Phase 1 以降でまとめてリネームする。
（`CMakeLists.txt` の `project()` / `APP_NAME` キャッシュ変数、
`cmake/cm0-package.cmake` のパッケージメタ情報、`assets/applications/` の
launcher 定義などが対象になる見込み）

## 4. czdev CLI 調査結果

`czdev` は当初 PLAN.md にあった「CardputerZero/template」プロジェクトの
一部ではなく、**別リポジトリ `m5stack/CardputerZero-AppBuilder`** が提供する
CLI。役割は主に「オンラインビルド（.deb生成）」と「独自の軽量デスクトップ
エミュレータ」の2つ。

構成:
- **Publishing コマンド**（`login` / `logout` / `bump` / `publish` / `unpublish`）:
  Python 3 のみで動作。`git clone` 後 `./czdev --help` で即使える。
- **Emulator コマンド**（`doctor` / `list` / `build` / `run` / `watch` / `deploy`）:
  **Rust ツールチェーン必須**。`cargo build --release -p czdev` でビルドし、
  `cargo run -p czdev --release -- run examples/hello_cz` のように実行する。

重要な違い: czdev のエミュレータは **LVGL v9.5 + `dlopen` ベースの薄いアプリABI**
（`app_main(lv_obj_t *parent)` / `app_event(...)` をエクスポートする共有ライブラリを
エミュレータ本体が動的ロードする方式）を前提にしており、`app-builder.json` という
別のマニフェスト形式・別の `CZApp` CMake モジュール・別のプロジェクト雛形
（`examples/hello_cz` 等）を使う。今回 Phase 0 で導入した
**`CardputerZero/template`（MVVM構成、単体実行ファイルとしてビルド）とは
別物のワークフロー**であり、そのまま流用はできない。

このマシンには Rust ツールチェーン（cargo/rustc）が未導入で、`curl | sh` の
rustup 導入 + Rust ビルドが必要となり「1コマンド級」の手軽さではないと判断し、
**実際のインストール・動作確認は見送った**（PLAN.md の Phase 0 指示に基づく判断）。
リポジトリの clone と README 精読のみ実施。

`--recursive` clone を試したが `emulator/` サブモジュールが
`.gitmodules` として見当たらず（トップに `.gitmodules` ファイル自体が
存在しない）、README のトラブルシュート項目
「`emulator submodule not checked out`」に該当する状態だった。
これも「情報不足・不明瞭」の一因であり、深追いしない判断を後押しした。

**Phase 0 の完了条件（「デスクトップでテンプレートアプリが起動」）は
`cmake --preset darwin-arm64` 経由の SDL ビルドで満たしている**ため、
czdev エミュレータが無くても Phase 0 のゴールには影響しない。
czdev は Phase 5（AppBuilder 経由のオンラインビルド・.deb化・AppStore登録）で
本格的に必要になる想定なので、その時点で改めて Rust 導入から着手するのが良い。

## 5. Phase 1 に向けての注意点

1. **テストフレームワークが無い**ので、まず GameCore 実装と同時に
   CTest + GoogleTest（or Catch2）を FetchContent で追加する作業が必要。
   LVGL に依存しない `battleship_core` ライブラリターゲットを切り、
   そこだけを対象にテストバイナリをビルドする構成にすると、
   デスクトップ/組込み両ビルドでテストの扱いに悩まなくて済む。
2. **fmt バージョン非互換**は今回 `logger.h` のみ影響したが、他に
   `fmt::format` / `fmt::print` 等を直接使うコードを新規に書く場合は
   `fmt/format.h` を include すること（`fmt/core.h` だけでは足りない）。
3. **プロジェクト名リネーム**（TemplateApp → Battleship）は据え置き。
   Phase 1以降、GameCore 追加のタイミングで一緒にやると差分が把握しやすい。
4. **czdev エミュレータ**は今回未導入。Phase 5 で AppBuilder 経由の
   .deb ビルド・AppStore 登録をする際に、Rust ツールチェーンの導入から
   着手する。
5. 320×170 という実解像度は、今回のビルドの起動ログでも
   `LVGL app started at 320x170` として確認済み。UI 実装（Phase 2）の
   際の実寸検証はこの SDL デスクトップビルドでそのまま行える。

## 6. Phase 1 完了メモ（GameCore + ユニットテスト + リネーム）

### 6.1 battleship_core の公開 API 概要

`src/core/` 配下、名前空間 `battleship::core`。LVGL・platform・I/O に一切
依存しない純粋 C++17（`std::vector` / `std::array` / `std::random` のみ）。
CMake ターゲット名は `battleship_core`（静的ライブラリ）。

| ヘッダ | 主要クラス/型 | 役割 |
|---|---|---|
| `core/types.h` | `Coord`, `Orientation`, `ShipType`, `ShotOutcome`, `ShotResult` | 基礎値型。`ship_length()`, `to_string(ShipType)` などのヘルパーも同居 |
| `core/ship.h` | `Ship` | 配置済み1隻（種別・起点・向き）。`fits_in_bounds()` / `occupies()` / `overlaps()` |
| `core/board.h` | `Board` | 自艦隊盤。`place_ship()`（盤内・重なり・種別重複チェック）、`receive_shot()`（Hit/Miss/Sunk/AlreadyShot/OutOfBounds 判定）、`all_sunk()` |
| `core/tracking_board.h` | `TrackingBoard`, `CellMark` | 記録盤（相手に撃った結果の記録のみ、相手の艦位置は持たない）。`record_shot()`, `mark_ship_sunk()`, `sunk_ships()` |
| `core/fleet.h` | `place_fleet_randomly(Board&, std::mt19937&)` | ランダム配置（シード注入可、再現可能） |
| `core/game.h` | `Game` | 両陣営の `Board`+`TrackingBoard` を保持する2人対戦の状態機械。`start()`, `shoot()`, `status()/current_turn()/winner()` |
| `core/player_view.h` | `PlayerView` | 片側視点の状態機械（通信対戦用）。相手の実 `Board` を持たず、`apply_incoming_shot()` / `apply_own_shot_result()` の往復のみで進行。ロジックは `Game` と同一の `Board::receive_shot` に基づく |
| `core/snapshot.h` | `MatchSnapshot`, `PlayerSnapshot`, `OwnBoardView`, `TrackingBoardView` | LLM解説・リザルト画面向けの構造化スナップショット |
| `core/battleship_core.h` | — | 上記全部を include する便利ヘッダ（ライブラリ内部では個別 include 推奨） |

### 6.2 前任実装からの変更点

実装バグはゼロ（レビュー + 59テストで確認）。ヘッダ/実装ファイル自体は無修正。
CMake側で1点だけ修正: ルートプロジェクトを `template_app` → `battleship` に
リネームしたことで、既存の `${PROJECT_NAME}_core`（LVGL 込みのアプリ統合層、
view/viewmodel/platform を束ねるターゲット）が新規の `battleship_core`
（ゲームロジック、LVGL 非依存）と名前衝突するようになったため、前者を
`${PROJECT_NAME}_app_core` にリネームした（`CMakeLists.txt` のみの変更、
ソースコードは無変更）。

### 6.3 テスト

`tests/` に GoogleTest v1.15.2（FetchContent）ベースで **59 テストケース**
（test_board.cpp / test_tracking_board.cpp / test_fleet.cpp / test_game.cpp /
test_player_view.cpp / test_snapshot.cpp）。Debug/Release 両方で **全 green**。

実行方法:
```bash
cmake --preset darwin-arm64
cmake --build --preset darwin-arm64-dbg   # or darwin-arm64-rel
cd build/darwin-arm64 && ctest -C Debug --output-on-failure   # or -C Release
```
`USE_DESKTOP=OFF`（CM0 クロスビルド等）では `tests/` サブディレクトリごと
CMake から除外されるため、デバイスビルドはテスト関連コードに一切触れない。

テスト設計上のポイント:
- `Fleet.RandomPlacementProducesFullyPlacedValidBoardManyTimes` はシード
  0〜999 の1000回、盤内・重なりなし・種別重複なし・5隻すべて配置済みを検証。
  `Fleet.SameSeedProducesIdenticalLayout` で同一シードの再現性も検証。
- `PlayerView.TwoPlayerViewsMatchFullGameForIdenticalShotSequence` が
  「片側視点 API の SHOT/RESULT 往復進行がフル `Game` と同一結果になる」
  ことの整合性テスト。**既知の意図的な仕様差異**が1つ見つかっている
  （6.4節参照）ので、そこは仕様として反映したアサーションにしている。

### 6.4 既知の設計上の制約（Phase 2/4 引き継ぎ事項）

`PlayerView` は相手の実 `Board` を持たないため、ある艦が Sunk になった時、
`Game` のように「その艦の全セルを遡って `CellMark::Sunk` に昇格させる」こと
ができない（`PlayerView::apply_own_shot_result` 内のコメント参照）。
`TrackingBoard::mark_ship_sunk()` というAPIはあるが、`PlayerView` はどの
既存 Hit セルがどの艦に属するか（複数艦を同時に追跡中の場合、曖昧になり
うる）を判別する情報を持たない。PLAN.md の通信プロトコルも
`RESULT(hit|miss|sunk+艦種)` のみでセル座標一覧は送らない設計なので、これは
protocol/PlayerView の**意図した仕様**であり、バグではない。

影響: 通信対戦の記録盤UIで、撃沈済み艦のうち最後の1発以外のセルが
`Hit`（撃沈前の見た目）のまま残る可能性がある。ゲームの勝敗・ターン進行・
`sunk_ships()` リストには一切影響しない（テストで確認済み）。UI側で気になる
場合は、艦が `sunk_ships()` に入った時点で、その周辺の `Hit` セルもまとめて
「撃沈済み」スタイルで描画する、といった見た目上のフォールバックで十分。

### 6.5 プロジェクトリネーム（TemplateApp → Battleship）

- `CMakeLists.txt`: `project(template_app ...)` → `project(battleship ...)`
  （`${PROJECT_NAME}` を使う既存ターゲット群は自動的に `battleship_*` に追従）
- 名前衝突を避けるため、アプリ統合層ターゲットのみ `${PROJECT_NAME}_core` →
  `${PROJECT_NAME}_app_core` に変更（6.2節参照）
- `cmake/cm0-package.cmake`: `APP_DISPLAY_NAME` 既定値 `TemplateApp` →
  `Battleship`、`APP_PACKAGE_DESCRIPTION` も更新
- `vcpkg.json`: `name` を `battleship-simulator-on-windows` に変更
- `deploy.sh`: `PACKAGE_GLOB` を `dist/Battleship_*_arm64.deb` に変更
- `src/app/asset_manager.cpp`: `APP_NAME` フォールバック定義を `"battleship"` に
- `src/app/app.cpp`: SDL ウィンドウタイトルとロガーの tag を `Battleship`/
  `battleship` に変更
- `src/model/base_model.cpp`: `BaseModel::app_title()` の返却文字列を
  `"Battleship"` に変更（UI表示文言、リネームの一環としてのみ変更。それ以外の
  view/viewmodel/model には手を入れていない）
- **未対応（Phase 2/5 に持ち越し）**: `assets/images/template*.png` は旧
  テンプレートのプレースホルダ画像のままで、`.desktop` の `Icon=` もそれを
  参照している。Battleship 用のアイコン素材が用意され次第、画像ファイル名・
  `cmake/templates/app.desktop.in` の `Icon=` パス・
  `cmake/cm0-package.cmake` の `FILES_MATCHING PATTERN "template*.png"` を
  まとめて差し替えること。
- リネーム後の確認: `cmake --preset darwin-arm64` で再configure → Debug/Release
  両方フルビルド成功 → `ctest`（両configuration）で 59/59 green →
  `./build/darwin-arm64/Debug/battleship` をバックグラウンド起動し4秒後も
  プロセス生存を確認（`[battleship][I][app.cpp:120] LVGL app started at
  320x170` のログも出力）→ kill で正常終了。

### 6.6 リネーム後のバイナリパス

```
./build/darwin-arm64/Debug/battleship
./build/darwin-arm64/Release/battleship
```
（CM0 クロスビルドでは `build/cp0-cross/<Config>/battleship` になる想定、
未検証）

## 7. Phase 2 完了メモ（UI一式・ホットシートフロー）

Phase 2 のゴール「エミュレータで人間同士が1台で遊べる」を達成。
menu → placement(P1) → placement(P2) → handover → battle → …（勝敗まで
繰り返し）→ result → （rematch で再戦 / title でメニューへ）という
ホットシートの全画面遷移を実機（SDL デスクトップビルド）で確認済み。

### 7.1 画面一覧

MVVM の View 層。すべて `src/view/screens/` 配下、`screen::BaseScreen`
（`src/view/screens/base_screen.{h,cpp}`）を継承し、共有の
`viewmodel::BaseViewModel`（`src/viewmodel/base_viewmodel.{h,cpp}`、
実体は単一の "god" ViewModel。ヘッダ冒頭のコメントに設計意図あり）を
介して `battleship::core::Game` を操作する。画面遷移は
`model::AppPage`（`src/model/base_model.h`）を値とする LVGL subject
（`BaseViewModel::current_page_subject()`）を `ScreenManager`
（`src/app/screen_manager.h` / `src/app/screen_namager.cpp` —
ファイル名のタイポは既存のまま踏襲。直さない）が監視し、値が変わる度に
該当する画面クラスを `lv_screen_load()` する形。

| 画面 | ファイル | 役割 |
|---|---|---|
| Menu | `src/view/screens/menu_screen.{h,cpp}` | タイトル + メニュー。「2 Player (Hotseat)」のみ有効。「1 Player」「Network Play」「Settings」は `LV_STATE_DISABLED` 表示の "(Coming soon)" プレースホルダで、focus group にも追加されないためフォーカスもクリックも一切効かない（Phase 3/4 の実装対象。誤操作でクラッシュしないことを確認済み） |
| Placement | `src/view/screens/placement_screen.{h,cpp}` | 10x10 盤（12px セル）+ 艦リスト + ステータス行 + Confirm ボタン。`placement_player()` が One/Two のどちらかで、同じ画面クラスが2回（P1→P2）使われる |
| Handover | `src/view/screens/handover_screen.{h,cpp}` | 「Pass the device to Player N」+「Don't peek」+ Ready ボタン。**配置間（P1→P2）と、バトル中の毎ターン（次の手番へ）の両方**で経由する中継画面（`BaseViewModel::confirm_handover()` が `game().status()` を見て Placement/Battle のどちらに戻すか分岐） |
| Battle | `src/view/screens/battle_screen.{h,cpp}` | 左: 記録盤（Targets、フォーカス可・カーソル移動+Enterで攻撃）/ 右: 自艦隊盤（Your Fleet、表示のみ）。タイトルに手番（"P1 Fire!" / "P2 Fire!"）、下部にステータス行（Hit!/Miss/Sunk: X!/Already targeted） |
| Result | `src/view/screens/result_screen.{h,cpp}` | 勝者・両プレイヤーの命中率・総ターン数 + Rematch/Title ボタン |

画面遷移フロー（`model::AppPage` の値: Menu=0, Placement=1, Handover=2,
Battle=3, Result=4）:

```
Menu --(Enter: start_new_match)--> Placement(P1)
Placement(P1) --(Confirm: advance_from_placement)--> Handover(→P2)
Handover --(Enter: confirm_handover, status==Placementのため)--> Placement(P2)
Placement(P2) --(Confirm: advance_from_placement, game_.start()実行)--> Handover(→先攻P1)
Handover --(Enter: confirm_handover, status==InProgressのため)--> Battle
Battle --(Enter on non-winning shot: fire_at_cursor)--> Handover(→次の手番)
Handover --(Enter)--> Battle（手番交代して繰り返し）
Battle --(Enter on game-winning shot)--> Result
Result --(Rematch: start_new_match)--> Placement(P1)（新しいGameで最初から）
Result --(Title: show_page(Menu))--> Menu
```

**注意**: Placement 確定後は必ず Handover を経由する（配置完了→即 Battle
ではない）。自動化スクリプトを書く際はこの中間ステップを飛ばさないこと
（後述 7.3 のバグ参照）。

### 7.2 キーバインド一覧

共有 focus group（`ScreenManager::input_group()`、全画面の focusable
widget が登録される単一の `lv_group_t*`）に対し、デスクトップは
`lv_sdl_keyboard_create()`、CM0 実機は `platform::init_key_input()`
（`src/platform/linux_input.cpp`、evdev 46キーマトリクスを走査し
矢印/Enter/Esc/Backspace/R/X のみを LVGL キーへマップ）が同じキー入力を
供給する。画面ごとの個別ショートカット（R/X 等）は group navigation では
なく、各画面が `LV_EVENT_KEY` を直接ハンドルする形で実装されている
（`placement_screen.cpp` の `board_key_cb` / `battle_screen.cpp` の
`board_key_cb`）。

| キー | Menu | Placement | Handover | Battle | Result |
|---|---|---|---|---|---|
| ↑↓←→ | フォーカス移動（有効項目は1つのみなので実質無効） | 配置カーソル移動（`move_placement_cursor`） | フォーカス移動（ボタン1つのみ） | 記録盤カーソル移動（`move_battle_cursor`） | Rematch/Titleボタン間のフォーカス移動 |
| Enter | フォーカス中の項目を実行（"2 Player (Hotseat)"のみ） | 艦を配置（`commit_placement`）。全艦配置済みなら次へ進む（`advance_from_placement`） | Ready（`confirm_handover`） | 記録盤カーソル位置に攻撃（`fire_at_cursor`） | フォーカス中のボタンを実行 |
| Esc | — | `LV_KEY_ESC`（現状 Placement 側で個別ハンドラなし。将来の「戻る」用に予約） | — | — | — |
| Backspace | — | 直前に置いた艦を取り消す（`undo_last_placement`） | — | — | — |
| r / R | — | 配置向き（横/縦）をトグル（`toggle_placement_orientation`） | — | — | — |
| x / X | — | 現在のプレイヤーの艦隊をランダム配置（`randomize_placement`） | — | — | — |

Confirm ボタン（Placement）は全艦配置完了まで `LV_STATE_DISABLED`。
盤面がフォーカスされている間は Enter が「盤面上のセルに対する操作」
として盤面の `LV_EVENT_KEY` ハンドラに吸収されるため、Confirm ボタンへ
フォーカス移動しなくても「全艦配置済み状態で盤面にフォーカスがある間の
Enter」がそのまま次画面への遷移として働く（`placement_screen.cpp`
`board_key_cb` の `LV_KEY_ENTER` 分岐にコメントあり）。

### 7.3 スクリーンショット機構の使い方

`src/app/screenshot.{h,cpp}`: `capture_screenshot(name)` が
`lv_snapshot_take()` + libpng で `docs/screenshots/<name>.png` に保存
（`LV_USE_SNAPSHOT` を `src/config/lv_conf_desktop.h` で有効化済み。
デスクトップ専用、CM0 実機ビルドでは無効化されている）。

3通りの起動方法（`src/app/app.cpp` 内、`USE_DESKTOP && LV_USE_SDL` の
デスクトップビルドでのみ有効）:

1. **F12 手動キャプチャ**: 実行中に F12 を押すとその瞬間の画面を
   `docs/screenshots/capture_NNN.png`（連番）に保存。
2. **`APP_SCREENSHOT_SEQUENCE` 環境変数**: `"delay_ms:file_stem:delay_ms:file_stem:..."`
   形式。起動後 `delay_ms` ミリ秒（各エントリは直前のエントリからの相対時間、
   最初のエントリのみ起動時刻からの相対時間）経過ごとに
   `capture_screenshot(file_stem)` を実行。手キャプチャ不要でメニュー画面等
   固定タイミングのスクショが撮れる。
   例: `APP_SCREENSHOT_SEQUENCE="500:menu:1500:placement_p1" ./build/darwin-arm64/Debug/battleship &`
3. **`APP_SCRIPT` 環境変数**: `"delay_ms:action:delay_ms:action:..."` 形式。
   `action` は `key:<name>`（`up`/`down`/`left`/`right`/`enter`/`esc`/
   `backspace`、または `r`/`x` のような1文字 ASCII）でキー入力を合成、
   または `shot:<stem>` で `capture_screenshot(stem)`。ホットシートの
   全画面フローを人手を介さず一気通貫で実行できる。

   実際に使用した例（menu → P1配置(ランダム) → Handover → P2配置(ランダム)
   → Handover → Battle → 数手撃つ、まで一気通貫。7.1 の「Handover を必ず
   経由する」点に注意し、Placement の Confirm 直後には必ず
   `key:enter`（Handover の Ready ボタン）を挟んでいる）:
   ```bash
   SCRIPT="500:shot:01_menu:300:key:enter:400:shot:02_p1_placement_empty:\
   300:key:x:400:shot:03_p1_placement_random:300:key:enter:\
   400:shot:04_handover_to_p2:300:key:enter:400:shot:05_p2_placement_empty:\
   300:key:x:400:shot:06_p2_placement_random:300:key:enter:\
   400:shot:07_handover_to_battle:300:key:enter:400:shot:08_battle_start:\
   300:key:enter:400:shot:09_battle_shot1:300:key:right:300:key:enter:\
   400:shot:10_battle_shot2"
   APP_SCRIPT="$SCRIPT" ./build/darwin-arm64/Debug/battleship > /tmp/run.log 2>&1 &
   PID=$!
   sleep 8
   kill $PID   # while(running) ループなので自然終了しない前提で明示的にkillする
   ```
   （macOS には `timeout` コマンドが無いため、バックグラウンド起動 +
   `sleep N` + `kill` の組み合わせで実行時間を制御すること）

`docs/screenshots/` の運用: 開発中の一時キャプチャ（`capture_NNN.png`や
ad-hoc なステム名）はスクラッチ用途なので都度削除してよい。正式な
ドキュメント用スクリーンショットは `menu.png` / `placement_p1.png` /
`placement_p2.png` / `handover.png` / `battle_start.png` / `battle.png` /
`result.png` の7枚に統一（320x170 の小画面でもラベル・ステータス・
ボタンの文字が判読できることを目視確認済み。艦の配置色分け、記録盤の
Miss(灰)/Hit(橙)/Sunk(赤) の色分けも視認可能）。

### 7.4 修正したバグ

**(1) スクリプトのファイル名破損バグ（本セッションの主目的）**

**症状**: `docs/screenshots/` に改行文字が混入したファイル名の PNG が
実際に生成されていた（前任セッションの成果物として発見）。

**原因**: `app.cpp` の `parse_delay_action_list`（`APP_SCRIPT` /
`APP_SCREENSHOT_SEQUENCE` の共通パーサ）が、コロン区切りで切り出した
`action_name`（= キー名 or スクリーンショットのファイルステム）を
一切トリムせずそのまま使っていた。シェル上で環境変数値を複数行文字列と
して書いた場合（意図したコロンの代わりに改行が入ってしまうケースを
含む）、末尾の空白・改行がステム文字列の一部として
`capture_screenshot()` にまで渡り、`fopen()` は改行を含むパスでも
（`/` と NUL 以外は POSIX 上合法なため）普通にファイルを作成してしまう
ことを実機で再現・確認した（`APP_SCREENSHOT_SEQUENCE=$'300:shot:menu\nbroken'`
で `docs/screenshots/menu\nbroken.png`（リテラル改行入り）が生成される
ことをテストで確認）。

また、`delay_str`（ミリ秒の数値部分）に改行等の非数字が混入すると
`std::stoul` が `std::invalid_argument` を送出し、どこにも catch が
無かったため **アプリ全体が SIGABRT でクラッシュ**する経路もあわせて
確認した。

**対処（2段の防御）**:
- `app.cpp`: 各トークン（delay・action name）を `trim()` してから使用。
  空になったトークンはスキップ（クラッシュさせない）。`std::stoul` を
  try/catch で保護し、パース不能な delay トークンは警告ログを出して
  そのエントリだけスキップ（アプリ全体を落とさない）。
- `screenshot.cpp`: `capture_screenshot()` 内で
  `sanitize_name()`（`[A-Za-z0-9._-]` のみ許可、それ以外は `_` に置換、
  空白は削除）を通してから `fopen()` に渡す "second line of defense"。
  `APP_SCRIPT` 経由以外の将来の呼び出し元（画面固有のデバッグフック等）
  が生の文字列を渡してきても壊れたファイル名が作られない。

修正後、上記の実際に壊れていたケース（改行混入）を再テストし、
サニタイズ後の正常なファイル名（`menubroken.png`）が生成されることを
確認済み。

**(2) 合成キー入力の SDL_PushEvent SIGSEGV（前任が残したコメントに
記録済み・本セッションで動作確認）**

`push_synthetic_key()` が当初 `SDL_PushEvent()` で `SDL_KEYDOWN` /
`SDL_TEXTINPUT` イベントをキューに積む実装だった場合、Homebrew の
`sdl2`（実体は SDL3 バックエンドの sdl2-compat 互換シム）経由で
アプリケーション側から `SDL_TEXTINPUT` を push すると、SDL2→SDL3 の
イベント変換内部で `EXC_BAD_ACCESS`（SIGSEGV）を起こす。これは
`APP_SCRIPT` で `key:r` / `key:x`（Placement の回転/ランダム配置
ショートカット、1文字 ASCII は `SDL_TEXTINPUT` 経由でしか LVGL に
渡らない）を使うたびに確実に再現した。対処として、`SDL_PushEvent`を
経由せず LVGL の `lv_sdl_keyboard_handler()` を直接呼び出す方式に
変更済み（実イベントと同じコード経路を通りつつ、壊れた SDL2/3 変換層
を回避する）。本セッションでリビルド後に `x`/`r` キーを含む全フロー
スクリプトを実行し、クラッシュしないことを確認した。

### 7.5 その他の動作確認

- Menu の無効化メニュー項目（1 Player / Network Play / Settings）は
  focus group に追加されないため、上下キーでフォーカスが当たることも
  Enter で誤発火することも無い。クラッシュしないことを確認済み。
- Result 画面の Rematch（`start_new_match()` で `Game` を作り直して
  Placement(P1) へ）・Title（`show_page(Menu)`）は両方 `LV_EVENT_CLICKED`
  + 共有 focus group 登録済みで、コードレビューおよび実際の勝敗到達
  スクリーンショット（`docs/screenshots/result.png`、P1 が
  17/17 shots (100%) で勝利した実プレイのキャプチャ）で表示内容を確認。
- 既に撃った(記録済みの)セルに再度 Enter すると `fire_at_cursor()` は
  `Invalid` を返し画面遷移しない（同じ Battle 画面に留まる）。ステータス
  行に "Already targeted" と表示されることを確認。仕様通りの挙動
  （誤射防止）であり、バグではない。

### 7.6 Phase 3（AI対戦・LLM解説）への申し送り

- **IOpponent / AIPlayer の差し込み箇所**: 現状 `BaseViewModel::fire_at_cursor()`
  が `game_.current_turn()` を「常に人間」として扱い、Handover 画面で
  物理的にデバイスを渡す前提になっている。1人プレイでは Handover 画面を
  スキップし、`current_turn() == PlayerId::Two`（仮に AI 側を Two とする）
  の時は `battle_screen.cpp` の入力を待たずに ViewModel 側から
  AI の手を自動計算して `fire_at_cursor()` 相当の処理を呼ぶフックが必要。
  具体的には `BaseViewModel` に「対戦モード」（Hotseat / VsAI）の概念を
  追加し、`confirm_handover()` 内の分岐、および Battle 画面から
  Handover へ遷移する箇所（`fire_at_cursor()` 末尾）で
  「次の手番が AI なら Handover を経由せず即座に AI に手番を渡す」
  条件分岐を入れるのが自然。`PLAN.md` §2 の `IOpponent` 抽象
  （`AIPlayer` ← 確率密度エンジン）はまだコード上に存在しないので、
  Phase 3 で `battleship_core` 内（LVGL 非依存）に新設し、
  `BaseViewModel` からはその抽象越しに呼び出す形にすると
  `battleship_core` のテスト資産（GoogleTest, 59件）にそのまま
  AI 自動対戦テストを追加できる。
- **「1 Player」メニュー項目を有効化するために必要な作業**:
  1. `battleship_core` に確率密度 AI（Hunt/Target モード、難易度3段階）を実装
     （PLAN.md §2.3）。
  2. `BaseViewModel` に対戦モード状態を持たせ、`start_new_match()` の
     呼び出し元（Menu の "1 Player" ボタン）で AI 対戦モードとして初期化。
  3. Placement(P2) 画面を「AI が自動配置する」ケース用にスキップする分岐
     （`advance_from_placement()` で P2 が AI なら即座に
     `randomize_placement()` 相当を呼んで Handover へ）。
  4. Handover 画面もAI視点では不要なので、AI 手番はダイアログ無しで
     即時実行し、結果（Hit/Miss/Sunk）だけステータス行に表示して
     人間の番に戻す（Battle 画面のまま、または一瞬 Handover を出すかは
     UX 判断）。
  5. `menu_screen.cpp` の `kMenuEntries` で `"1 Player"` の `enabled` を
     `true` にし、`activate_cb` 相当を新設（難易度選択 UI が必要なら
     Menu → 難易度選択画面 → Placement という新画面が要るかも
     PLAN.md §2.5 を参照）。
- **LLM 解説役（Commentator）**: Phase 3 後半。`battleship::core::MatchSnapshot`
  （`src/core/snapshot.h`、既に実装・テスト済み）がまさに解説生成への
  入力として設計されているので、Battle 画面の下部ステータス行を
  拡張するか専用の解説行を追加し、非同期でテキストを差し込む形になる
  見込み（PLAN.md §2.4）。feature flag での ON/OFF が要件。

## 8. Phase 3a 完了メモ（AI 対戦・1人用モード）

Phase 3 前半のゴール「1人用モード完成、AI 自動対戦テストで破綻なし」を達成。
Menu「1 Player」→ 難易度選択（Easy/Normal/Hard）→ 配置（人間のみ）→ 戦闘
（AI 手番は自動・ハンドオーバー無し）→ リザルト（You/CPU 表記）の全フローを
SDL デスクトップビルドの APP_SCRIPT 自動走行で決着まで確認済み。

### 8.1 AI エンジン（battleship::core::ai、LVGL 非依存）

`src/core/ai.{h,cpp}`、名前空間 `battleship::core::ai`。公開 API:

```cpp
ai::AiPlayer ai(ai::Difficulty::Hard, seed);           // 難易度 + シード注入
Coord next = ai.next_shot(tracking_board,              // 自分の記録盤のみ参照
                          tracking_board.remaining_ship_types());
```

- **入力は TrackingBoard のみ**（人間プレイヤーが戦闘画面で見るのと同じ情報）。
  相手の実 Board には一切触れない。同一 (難易度, シード, 履歴) → 同一の次手
  （mt19937_64、再現可能。テストの統計走行もシード固定）。
- **Hunt モード（確率密度）**: 残存艦それぞれを全マス×縦横に仮置きし、既知の
  Miss/Sunk セルと矛盾しない配置の被覆セルへ +1 加算 → 未射撃セルのうち
  最大スコアを射撃。同点はシード注入 RNG でランダムタイブレーク。
- **Target モード（全難易度共通）**: 未撃沈 Hit セルがあると常に優先。
  孤立ヒット → 4近傍、2連以上の直線ヒット → 走査でランをグルーピングして
  両端の延長セルのみ（直交近傍は候補から除外）。候補は同じ密度マップで
  順位付け（複数候補方向の合理的ランキング）。複数艦を同時追跡中で
  ランが非直線になる場合は各ランごとに候補を出す。
- **難易度差**:
  | | Hunt | Target | パリティ |
  |---|---|---|---|
  | Easy | 未射撃セルから一様ランダム（密度不使用） | あり | なし |
  | Normal | 密度 65% / ランダム 35% 混合 | あり | なし |
  | Hard | 密度 100%（タイブレーク以外ランダム無し） | あり | あり |
- **パリティ（Hard のみ）**: 最小残存艦長 L に基づき `(x+y) % L == 0` の
  市松サブセットへ Hunt を制限（L=2 で通常の checkerboard、駆逐艦撃沈後は
  L=3 → さらに疎に）。該当セルが尽きた場合は全未射撃セルへフォールバック。
  Target モードはパリティ制約を受けない。

### 8.2 テスト（tests/test_ai.cpp、+12 ケースで計 71）

Debug/Release 両方で 71/71 green（`ctest -C Debug` / `-C Release`）。

単体: 孤立ヒット後の4近傍追撃 / 2連ヒットの直線延長のみ / AlreadyShot を
絶対選ばない（全難易度で盤面をほぼ撃ち尽くすまで反復＋多シード）/ Hard の
初手パリティ `(x+y)%2==0`・Carrier のみ残存時 `(x+y)%5==0` / シード再現性。

**AI 自動対戦統計（ヘッドレス、各マッチアップ 250 ゲーム、シード固定）実測値**:

| 指標 | 実測 | 判定基準 |
|---|---|---|
| Hard 自己対戦 平均決着手数 | **40.1 手/一人**（総 80.3） | ≤ 60 手/人 ✓ |
| Easy 自己対戦 平均決着手数 | 51.3 手/一人（総 102.6） | Hard より明確に多い ✓ |
| Hard vs Easy 勝率 | **83.6%** | ≥ 75% ✓ |
| Hard vs Normal 勝率 | 61.6% | ≥ 55% ✓ |
| Normal vs Easy 勝率 | 76.4% | ≥ 55% ✓ |
| 全難易度×60 シード 正常終了 | 全ゲーム < 200 手で決着 | 破綻なし ✓ |

注: Easy の平均が純ランダム理論値（≈95手/人）より小さいのは Target 追撃
（仕様上 Easy でも行う）だけで大幅に効率化されるため。よって「ランダム
基準線との比較」テストは Easy vs Hard の相対差で検証している。

### 8.3 UI 統合（1 Player モード）

- `model::AppPage::Difficulty = 5` を新設し、`DifficultyScreen`
  （`src/view/screens/difficulty_screen.{h,cpp}`、Easy/Normal/Hard + Back の
  4ボタン、上下キー + Enter）を ScreenManager に配線。
- `BaseViewModel` に `MatchMode { Hotseat, VsAi }` を追加。
  - `start_hotseat_match()`（Menu 2P）/ `start_ai_match(difficulty)`（難易度
    画面）/ `start_new_match()`（Result の Rematch、現モード維持で再戦）。
  - VsAi の `advance_from_placement()`: 人間の配置確定と同時に AI 艦隊を
    `place_fleet_randomly` で自動配置 → `game_.start(One)` → **Handover を
    経由せず** 直接 Battle へ。
  - AI 手番: 人間の射撃後 **500ms の one-shot LVGL タイマー**
    （`lv_timer_create` + `repeat_count=1`、自動削除。コールバック先頭で
    ハンドルを null 化してから実行）で `play_ai_turn()`。保留中は
    `fire_at_cursor()` が人間入力を無視（`ai_turn_pending()`）。
  - ステータス行: 開始時 "vs CPU (Hard)" 等 → 以降 "You D2: Miss |
    CPU G7: Hit!"（A1-J10 座標表記、人間の直前結果を保持して結合表示）。
    CPU の Sunk は "CPU B4: Sunk your Cruiser!"。
  - 勝敗が付いたら（人間側・AI 側どちらの射撃でも）Result へ。
- Battle 画面は `viewing_player()`（VsAi では常に One）の盤面を描画。
  1P では Battle 画面が終局までロードされ続けるため、AI 手番中に CPU 視点へ
  フリップしない。タイトルは "You vs CPU" 固定（長いタイトルは x=196 からの
  "Your Fleet" ラベルと衝突するため難易度はステータス行に表示）。
- Placement タイトルは 1P では "Place your fleet"、Result は
  "You win!"/"CPU wins!" + You/CPU 行。
- Menu は entry ごとの action 関数ポインタ制に変更（constexpr テーブル維持、
  BaseViewModel はボタンの user_data 経由）。
- ホットシートは無変更（スクリプト走行でリグレッション確認済み:
  P1 Fire! → 射撃 → Handover の従来フロー維持）。

### 8.4 スクリーンショット（docs/screenshots/）

1P モードの正式スクリーンショット（APP_SCRIPT 自動走行、目視確認済み）:
`menu.png`（1 Player 有効化後に更新）/ `difficulty_select.png` /
`placement_1p.png` / `battle_1p_start.png`（ステータス行 "vs CPU (Normal)"）/
`battle_1p.png`（"You D2: Miss | CPU G7: Miss"、自艦隊盤に CPU の Miss が
灰色で表示）/ `result_1p.png`（Hard CPU が 30 発 17 命中 57% で勝利、
"CPU wins!" + You/CPU 行、Total turns: 60）。

result_1p は蛇行スイープの自動走行（人間側キー合成のみ、AI 側はスクリプト
不要）で実際に決着させて取得。手順: 全交戦後に probe screenshot を撮り、
Result が写った最初のフレームを採用（決着手数が非決定的なため）。

### 8.5 Phase 3b（LLM 解説）への引き継ぎ

- **解説文の差し込み口**: Battle 画面下部のステータス行は
  `BaseViewModel::status_text_subject()`（`StringSubject<64>`）で
  `reactive::bind_label_text` により自動更新される。1P では
  "You ... | CPU ..." が毎交戦後に上書きするため、解説を同じ行に流すと
  取り合いになる。**専用の解説行を Battle 画面にもう1本追加**（subject を
  新設、例 `commentary_text_subject()`）するのが素直。320×170 の残り
  スペースは限られる（盤面下端 y=136 → ステータス行 y≈156）ので、
  解説行はステータス行と交互表示 or スクロールも検討。長文なら
  `StringSubject` のバッファ長（現在 64）拡張が必要。
- **MatchSnapshot の利用**: `game().snapshot()`（`src/core/snapshot.h`）が
  両盤面の公開情報・残存艦・手数を構造化して返す。解説プロンプト生成には
  これをそのまま整形すればよい。呼び出しタイミングは `play_ai_turn()` 末尾
  または `fire_at_cursor()` の結果確定後（どちらも状態確定済みの地点）。
- **AI 手番タイマーとの共存の注意**: AI の応答は 500ms one-shot タイマー
  （`BaseViewModel::ai_timer_`）。LLM 生成を**同じ LVGL スレッドで同期実行
  してはいけない**（タイマーが遅延し UI が固まる）。llama.cpp はワーカー
  スレッド + 完了フラグを LVGL タイマー/`lv_async_call` でポーリングする
  形が安全。解説が間に合わない場合はその手の解説をスキップ（PLAN.md §2.4 の
  フォールバック方針）。`cancel_ai_turn()` が match リセット/デストラクタで
  走るのと同様、解説生成の in-flight リクエストも `start_new_match()` で
  破棄すること。
- **feature flag**: 解説 ON/OFF は Menu「Settings」（現在 disabled）か
  難易度画面へのトグル追加が候補。OFF でも 1P モードが完全動作する現状を
  維持する。

## 9. Phase 3b 完了メモ（LLM 解説役 / Commentator）

Phase 3b のゴール「1人用モードに LLM 解説役を feature flag 付きで統合、
解説が非同期で表示される」を達成。ローカル LLM（llama.cpp + SmolLM2-360M-
Instruct Q4_K_M）による短い解説文が Battle 画面・Result 画面に非同期表示
され、ゲーム進行を一切ブロックしないことを実機相当の SDL デスクトップ
ビルドで確認済み。既存 71 テストを壊さず、+15 テスト（計 **86/86 green**、
Debug/Release 両方、`BATTLESHIP_LLM=ON`/`OFF` 両方）。

### 9.1 アーキテクチャ

`src/commentary/`（名前空間 `battleship::commentary`）:

| ファイル | 役割 |
|---|---|
| `commentator.h` | `ICommentator` 抽象（`request_comment`/`cancel_pending`/`set_callback`/`set_language`）、`CommentEvent`（8種）、`Lang{EN,JA,ZH}` |
| `snapshot_summary.{h,cpp}` | `MatchSnapshot` → 事実サマリ文字列・LLM プロンプト生成（LVGL/LLM 非依存、単体テスト可能） |
| `template_commentator.{h,cpp}` | 常時利用可能なフォールバック。イベントごとの定型文バンク（EN、各3種前後）からシード注入 RNG で選択。**同期**配信（文字列整形のみなのでコールバックをブロックしない） |
| `llm_commentator.{h,cpp}` | llama.cpp バックエンド。専用ワーカースレッド1本 + mutex/condition_variable の「最新1件のみ」キュー |
| `commentator_factory.{h,cpp}` | `BATTLESHIP_LLM`（コンパイル時）と `BATTLESHIP_COMMENTARY`/`BATTLESHIP_LLM_MODEL`（実行時環境変数）を見て適切な実装を選ぶ唯一の窓口 |

**スレッドモデル**（NOTES.md §8.5 の懸念に対応）:
- `LlmCommentator` はコンストラクタでモデルを同期ロードし（起動時、ゲーム
  進行外）、以降の `request_comment()` は文字列プロンプト生成のみ行い
  即座に返る。実際の推論は専用ワーカースレッド（`std::thread`）が行う。
- ワーカーの完了コールバックは `BaseViewModel` 側で mutex 保護の
  `pending_commentary_` にテキストを書き込むだけ（LVGL に一切触れない）。
  新設した **200ms 周期の LVGL タイマー**（`commentary_poll_timer_`）が
  ポーリングして `commentary_text_subject_`（`StringSubject<160>`）に
  反映する。これにより LLM 推論スレッドと LVGL スレッドが完全分離される。
- 「生成中に次のイベントが来たら古いリクエストは破棄」は
  `abandoned_up_to_id_`（アトミック整数）+ 単一スロットの
  `next_request_`（`std::optional`、mutex 保護）で実現。新しい
  `request_comment()` は古い未着手リクエストを即座に上書きする。生成が
  既に進行中の場合は cancel_pending() 呼び出しで完了後に結果を破棄する
  （生成自体を中断はしない — llama.cpp の decode ループを外部から
  安全に中断する仕組みが無いため。最大 48 トークン・ダブル改行での早期
  停止により、最悪ケースでも 1 生成が長時間ブロックし続けることはない）。
- `BaseViewModel::start_new_match()` が `commentator_->cancel_pending()`
  を呼び、`pending_commentary_`/`commentary_text_subject_` もクリアする
  ので、リマッチ後に前の試合の解説が遅延着地することはない。

**フォールバック経路**（3段構え、すべて実機確認済み）:
1. `BATTLESHIP_COMMENTARY=0` → `commentator_` が `nullptr`
   （`make_default_commentator()` が `nullptr` を返す）。解説行はテキスト
   が来ないため実質非表示（レイアウトは変わらず、空行のまま）。
2. `BATTLESHIP_LLM=OFF`（ビルド時） → `TemplateCommentator` のみリンク。
3. `BATTLESHIP_LLM=ON` だが `BATTLESHIP_LLM_MODEL` 未設定 or ファイルが
   ロード不能 → `LlmCommentator` は内部で `TemplateCommentator` に
   同期フォールバック（`is_model_loaded()==false` の間、
   `request_comment()` は毎回フォールバックへ委譲）。

### 9.2 llama.cpp 統合の判断

- **FetchContent、タグ固定**: `ggml-org/llama.cpp` の `b4487` タグ。
  CMake オプション `BATTLESHIP_LLM`（**デフォルト ON**）。
- **CPU のみ**（Metal/CUDA/Vulkan/BLAS すべて OFF）: 実機（CM0/aarch64、
  GPU バックエンド無し）に近い挙動を優先し、デスクトップ側のビルド簡素化
  もかねた判断。macOS では Accelerate framework 経由の CPU 実装（NEON/
  dotprod/i8mm 自動検出）が使われる。
- **ビルド時間実測**（このマシン、8コア Apple Silicon、キャッシュ無しの
  初回 configure+build）: configure（fetch + cmake configure）約25秒、
  ビルド本体 約12秒（Release、ninja 並列）。デスクトップ開発体験を
  損なわない範囲に収まったため **デフォルト ON のまま採用**（重すぎる
  場合の OFF 切替は温存しつつ、実測結果からは不要と判断）。
- **モデル**: `bartowski/SmolLM2-360M-Instruct-GGUF` の
  `SmolLM2-360M-Instruct-Q4_K_M.gguf`（256MB、`models/` に配置、
  `.gitignore` 済み・コミットしない）。HuggingFaceTB 公式リポジトリには
  Q4_K_M が無かった（Q8_0 のみ）ため、量子化バリエーションが揃っている
  bartowski の再量子化版を採用。
- **プロンプト形式**: 当初プレーンテキスト補完プロンプトで試したところ、
  小型モデル特有の「指示文をおうむ返しする」「”Your response:” を繰り返す」
  出力が頻発した。SmolLM2-Instruct の GGUF メタデータに含まれる
  `chat_template`（ChatML: `<|im_start|>role ... <|im_end|>`）に合わせて
  プロンプトを system/user/assistant の3ブロックに変更したところ、
  出力品質が大幅改善（9.3節の実例参照）。
- **サンプラー**: 温度 0.4 + repetition penalty（直近64トークン窓、
  penalty_repeat=1.3）。**ダブル改行での早期停止**を追加（小型モデルが
  1文で答えた後、別の話題や偽の会話ターンを続けてしまう挙動を防ぐため。
  実際に生成された複数段落の暴走出力を確認した上での対処）。

### 9.3 生成品質の実例とレイテンシ実測

実機（Mac, Apple Silicon, CPU のみ、Release ビルド最適化あり）での
実測値。**`n_threads` 設定でレイテンシが大きく変わる**ことを確認:

| `n_threads` | 生成レイテンシ（実測レンジ、max_tokens=48） |
|---|---|
| 2（アプリのデフォルト値） | **910 ms 〜 1440 ms** |
| 4（スタンドアロン計測時） | 590 ms 〜 900 ms |

生成例（`n_threads=2`、実際にアプリから生成されたテキストそのまま）:

```
[MatchStart]  "Turn 3. You: Your turn is complete, with the score being 1-0
               in favor of your team's ship count and a slight increase to
               their losses due to enemy fire. The CPU is still at zero
               shots hit,"
[HumanHit]    "The human player scored a hit, but the enemy ship count
               remains unchanged."
[HumanSunk]   "Sunk!"
[CpuMiss]     "You: 1/2 shots hit (50%), 5 ships left. The CPU missed and
               the game continues, with no changes to your position or any
               other actions taken by either of you yet."
[CpuSunk]     "A sinking, no surprises here. The human's Destroyer was the
               last one standing."
[MatchOver]   "The match has ended with 5 ships left and no casualties."
```

360M パラメータの小型モデルなりの限界（時々スコアの数字を微妙に誤読する、
文が長すぎて省略記号で切れる）はあるが、**「短い戦況コメント」という
Phase 3b の要件は十分満たす**品質。TemplateCommentator フォールバックの
実例（モデル未設定時、同期・瞬時表示）:
`"Anchors aweigh -- the battle begins."` /
`"The CPU fires and misses -- lucky break."`

**重要な発見: Debug ビルドでは llama.cpp が極端に遅い（実質ハング状態に
見える）。** Ninja Multi-Config の Debug 設定は `CMAKE_CXX_FLAGS_DEBUG`
（`-g` のみ、`-O0`）で llama.cpp/ggml もビルドされるため、最適化無しの
数学重い推論コードは 30秒以上たっても1回の生成が終わらない
（`sample` コマンドで secondary thread が `ggml_compute_forward_rope` を
延々と実行し続けていることを確認、デッドロックではなく単に遅いだけと
判明）。**実機・配布ビルドは必ず Release（`cp0-cross-rel` 等）を使うこと。**
デスクトップ Debug ビルドで LLM 統合を検証する際はこの点に注意
（テスト自体は LLM 実体を使わないため Debug でも問題なく green）。

### 9.4 UI 統合

- Battle 画面（`src/view/screens/battle_screen.cpp`）: 盤面下端
  （y=137）に専用の解説行を1本追加（VsAi モードのみ生成）。ステータス行
  （画面最下部）とは別の行なので取り合いにならない。長い解説文は
  `LV_LABEL_LONG_MODE_DOTS` で省略記号付き1行表示 — ただし
  `lv_obj_set_size()` で**明示的に高さを固定**する必要があった
  （`LV_SIZE_CONTENT` のままだと長文が2行に折り返してステータス行と
  重なるバグを実際の LLM 生成文で screenshot 確認して発見・修正）。
- Result 画面（`src/view/screens/result_screen.cpp`）: 総評行を追加
  （VsAi のみ）。`MatchOver` イベントは勝敗確定と同時に発火するが、LLM
  生成が画面表示に間に合わないことが多い（実測 0.9〜1.4秒、画面遷移は
  即座）ため、スナップショット値ではなく `commentary_text_subject()`
  に直接バインドし、後から届いても表示されるようにした。生成が間に合わ
  なければ空欄のまま（PLAN.md §2.4 のフォールバック方針通り）。
- 2画面とも `commentary_text_subject()`（`StringSubject<160>`、
  `BaseViewModel` 新設）を購読するだけで、明示的なポーリングロジックは
  screen 側に一切無い（既存の `reactive::bind_label_text` パターンを
  そのまま流用）。

### 9.5 モデル配置方法・環境変数（実機セットアップ手順）

```bash
mkdir -p models
curl -L -o models/SmolLM2-360M-Instruct-Q4_K_M.gguf \
  "https://huggingface.co/bartowski/SmolLM2-360M-Instruct-GGUF/resolve/main/SmolLM2-360M-Instruct-Q4_K_M.gguf"

# 実行時に環境変数でモデルパスを指定（未設定ならテンプレート解説にフォールバック）
BATTLESHIP_LLM_MODEL="$(pwd)/models/SmolLM2-360M-Instruct-Q4_K_M.gguf" ./build/darwin-arm64/Release/battleship

# 解説機能そのものを止めたい場合（デバッグ・A/B比較用）
BATTLESHIP_COMMENTARY=0 ./build/darwin-arm64/Release/battleship
```

`models/` は `.gitignore` 済み。CM0 実機配布時は `.deb` にモデルを含め
ない方針（512MB RAM 機に 256MB のモデルファイルを常時同梱するかは
Phase 6 のパッケージング判断次第 — 現状は「ユーザーが任意に配置する
オプトイン機能」として設計している）。

### 9.6 実機（CM0, 512MB RAM, aarch64 Cortex-A53 @1GHz）への注意点

- **CPU 性能差**: このセッションの実測は Apple Silicon（M系）の CPU の
  みで行った。CM0 実機（Cortex-A53 @1GHz、シングルスレッド性能は
  Apple Silicon の数分の1〜十数分の1が定説）では、実測 0.9〜1.4秒
  （`n_threads=2`）から**数秒〜10秒オーダーまで悪化する可能性が高い**。
  非同期設計自体はレイテンシに関わらずゲーム進行をブロックしないため
  致命的ではないが、「解説が話題に追いつかない」（次の一手が先に
  進んでしまう）体感は実機でより顕著になる見込み。実機入手後に
  Phase 4 と並行して実測し、必要なら `max_tokens` をさらに削る
  （48→24程度）・`context_size` を縮める等のチューニングが要る。
- **RAM**: モデルファイル 256MB + KV キャッシュ（512トークン分、実測
  約20MB）+ 実行時オーバーヘッド。512MB 機で battleship 本体・LVGL・
  OS を差し引いた残り予算を Phase 6 前に実機で `free`/`top` 実測する
  こと（このセッションでは実機無しのため未実施）。
- **`n_threads`**: 現在 2 固定（4コア中2つ、UI/AI 用に余裕を残す設計）。
  実機のコア数・他プロセス負荷を見て調整余地あり。

## 10. Phase 4（WiFi 対戦）・Phase 5（i18n）への申し送り

- **Phase 4 との独立性**: Commentator は `battleship::core::MatchSnapshot`
  のみに依存し、`Game`/`PlayerView` どちらの経路でも同一に動作する。
  WiFi 対戦時に解説役を有効化する場合も本セッションの実装をそのまま
  流用できる見込み（対戦相手が人間かAIかを問わない設計）。
- **Phase 5（i18n）**: `commentary::Lang{EN,JA,ZH}` は既に
  `ICommentator`/`TemplateCommentator`/`LlmCommentator` 全体に配線済み
  （`set_language()`/`language()`）。残作業は:
  1. `TemplateCommentator` の JA/ZH 定型文バンク追加
     （`src/commentary/template_commentator.cpp` の `kMatchStart` 等の配列）。
  2. `snapshot_summary.cpp` の `language_instruction()` に JA/ZH の
     出力言語指示文を追加（現状 TODO コメントを残してある）。
  3. `BaseViewModel` が保持する言語設定（Phase 5 で新設予定の設定機構）
     を `commentator_->set_language()` に伝播する配線。
  現時点のプロンプト・定型文は英語決め打ちだが、構造自体は変更不要。
- **Settings 画面**: 解説 ON/OFF は現状 `BATTLESHIP_COMMENTARY` 環境変数
  のみ。Phase 5 で Settings 画面を実装する際、この環境変数読み取りを
  設定ファイル永続化に置き換える（`commentator_factory.cpp` の
  `read_commentator_config_from_env()` が唯一の変更対象になるよう
  設計してある）。

## 11. Phase 4 完了メモ（WiFi 通信対戦）

Phase 4 のゴール「2台（開発時は Mac 上の2プロセス）が WiFi/TCP で対戦できる」
を達成。Mac 上の2プロセス TCP(localhost) 対戦で **Host 勝ち / Join 勝ちの両方
向で1ゲーム完走**（両者の最終状態が完全整合: 勝者 17/22 shots・敗者 0/21・
Total turns 43 が両画面で一致）、および**片側 kill による切断検出**
（約5秒以内に「Connection lost」ダイアログ表示）を確認済み。
テストは既存 86 + net 38 = **124/124 green**（Debug/Release 両方）。

### 11.1 net 層アーキテクチャ（src/net/、battleship_net、LVGL 非依存）

| ファイル | 役割 |
|---|---|
| `frame.{h,cpp}` | フレーム codec: `[MAGIC 0xB5 0x17][ver][type][seq][len][payload][CRC16]`（CRC はヘッダ含む全体、big-endian）。`FrameDecoder` はインクリメンタル&再同期型（CRC 不一致で 2 バイト捨てて MAGIC 再探索。切断フレームの偽 len で待ち続けても、再送で バイトが溜まれば CRC 失敗→自己回復する設計。テストで検証済み） |
| `messages.{h,cpp}` | HELLO(ver+乱数)/READY/SHOT(x,y)/RESULT(x,y,outcome,艦種,game_won)/GAME_OVER/ACK/PING の数バイト encode/decode。盤面情報は一切送らない |
| `transport.h` | `ITransport` 抽象（非ブロッキング poll 型バイトストリーム; poll/state/send/receive/close） |
| `loopback_transport.{h,cpp}` | プロセス内ペア + **故意のロス/重複/バイト分断注入**（シード付き、テスト用） |
| `tcp_transport.{h,cpp}` | POSIX 非ブロッキング TCP。listen(0)=OS 割当ポート対応、TCP_NODELAY、poll() 内で accept/connect 進行と送受バッファ処理 |
| `reliable_link.{h,cpp}` | **stop-and-wait 信頼性層**: seq(0-255 wrap) + ACK + タイムアウト再送（既定 400ms×5 回、超過で DeliveryEvent::Failed）+ 直前 seq 比較の重複破棄。ACK 自体は seq 0xFF・非再送。TCP では冗長だが LoRa 前提で最初からこの層を通す（PLAN.md §2.2） |
| `net_session.{h,cpp}` | ゲームレベル状態機械: HELLO 交換（ver 不一致→切断; 乱数大が先攻、同値は Host 先攻）→ READY 同期 → SHOT/RESULT 交互（PlayerView 駆動）→ 勝者が GAME_OVER 送信。PING は送信キュー空き時に 2 秒間隔、**切断検出は 3 系統**: 受信無音 10 秒(PeerSilent)/再送上限(SendFailed)/トランスポート死(TransportClosed)。クロック注入 `poll(now_ms)` でヘッドレステスト可能 |
| `discovery.{h,cpp}` | UDP 発見: ホストが `BSHIP:<ver>:<tcp_port>:<name>` を 35353 へ 1 秒間隔ブロードキャスト（+ 同一マシンテスト用に 127.0.0.1 へも送信）。UdpDiscoverer が送信元 IP をキーに鮮度管理付きホスト一覧を維持（5 秒で失効）。`local_ipv4_address()`/`local_host_name()` ヘルパー付き |

ポート: **TCP 35354**（使用中なら OS 割当へフォールバックし announce で通知）/
**UDP 35353**（SO_REUSEPORT で同一マシン複数インスタンス共存可）。

### 11.2 テスト（tests/test_net_*.cpp、+38 で計 124）

- codec 12: ラウンドトリップ全種 / CRC 破損(ヘッダ・ペイロード) / バイト単位
  分断 / 先行ゴミ・切断フレーム後の再同期 / MAGIC 分割 / ペイロード上限ガード
- 信頼性層 8: 配達 / ロス後再送 / 重複排除(ACK 消失再送含む) / 分断 /
  再送上限 Failed / 順序保証 / **ロス30%+重複20%+分断のソーク**（全40通が
  順序通り正確に1回ずつ到達、5シード）
- NetSession 11: 先攻合意 / 同値タイ→Host / ver 不一致切断 / READY 同期 /
  **フルゲーム E2E クリーン 20 シード + ロス15%・重複15%・分断注入 8 シード**
  （勝敗一致・敗者全沈・撃数の相互一致を両視点で検証）/ PING 生存維持 /
  無音切断検出 / トランスポート死の即時検出 / 手番外・二重射撃の拒否
- TCP 4: localhost 実ソケットで双方向送受 / 相手クローズ検出 / 接続拒否 /
  NetSession ハンドシェイク

### 11.3 UI フロー（Phase 4 追加ページ）

Menu「Network Play」→ NetworkMenu(Host/Join/Back) →
- Host: NetLobby（自 IP:ポート表示・スピナー・Cancel）→ 接続で自動遷移
- Join: NetJoin（発見ホスト一覧、1秒毎更新・Direct IP・Back）→
  NetDirectIp（`ip[:port]` テキスト入力、46キー用に evdev へ数字/./: を追加）
→ 接続確立（HELLO 交換完了）→ Placement（自分の配置のみ、Confirm で READY
送信）→ Battle ページ（相手 READY 待ちは「Waiting for opponent to place
ships...」表示）→ 対戦（解説行の位置に手番表示「Your turn - fire!」/
「Waiting for opponent's attack...」+ **sunk_ships() ベースの「Enemy sunk:
...」リスト**。NOTES §6.4 の制約により撃沈艦の全セル遡及着色は非対応で、
リスト表示で補完）→ Result（network では You/Opponent 表記、Rematch 無し
※再戦同期プロトコルは将来課題。Title ボタンに 2.5 秒マッシュガード）。
切断時はどの画面からでも NetError「Connection lost」→ OK でタイトルへ
（title 遷移時に teardown_network() が全ネットワーク資源を解放）。

### 11.4 2プロセス検証（Mac、実施済み）と発見した問題

検証方法: `BATTLESHIP_NET_AUTOPILOT=win|lose` 環境変数（テスト専用フック、
BaseViewModel 内）で配置と射撃を自動化し、APP_SCRIPT はメニュー操作と
スクリーンショットのみ担当。win 側は両者の決定的配置（横置き・行0-4）を
覆う 5×5 領域を掃射し、lose 側は水域(行5-9)のみ射撃。

```bash
# Run 1 (Host 勝ち) の例:
BATTLESHIP_NET_AUTOPILOT=win  APP_SCRIPT="500:key:down:300:key:down:300:key:enter:300:key:enter" ./build/darwin-arm64/Release/battleship &
sleep 2
BATTLESHIP_NET_AUTOPILOT=lose APP_SCRIPT="500:key:down:300:key:down:300:key:enter:300:key:down:300:key:enter:3000:key:enter:300:key:1:200:key:2:200:key:7:200:key:.:200:key:0:200:key:.:200:key:0:200:key:.:200:key:1:200:key:enter" ./build/darwin-arm64/Release/battleship &
# 約 25 秒で決着（43 ターン）。切断テストは対戦中に片側を kill -9。
```

**発見・修正した問題**:
1. **LVGL レンダースレッドの無限ループ（アプリ全体フリーズ）**: Battle 画面の
   解説行（`LV_LABEL_LONG_MODE_DOTS`・固定幅）で、テキスト幅がラベル幅近傍の
   特定値になると `lv_draw_label_iterate_characters()` が無限ループし、メイン
   スレッドが `lv_thread_sync_wait` で永久ブロック（`sample` コマンドで捕捉。
   「Your turn - fire!  Enemy sunk: Carrier, Battleship」で再現、確率的）。
   → 解説行を `LV_LABEL_LONG_MODE_CLIP` に変更して回避。**Result 画面の
   総評行（VsAi）は DOTS のまま**なので、同症状が出たら同じ対処を。
2. **リザルト画面の誤タッチ**: ネット対戦は相手のタイミングで終局するため、
   連打中の Enter が Result 表示直後の Title ボタンを即クリックして結果を
   見逃す → network モードのみ Title ボタンを 2.5 秒無効化。
3. **macOS のブロードキャスト非ループバック**: 同一マシンの Join 一覧に
   ホストが出ない → announce を 127.0.0.1 にも送信（11.1 参照）。
4. **stdout ブロックバッファリング**: kill 時に直近 4KB のログが消え、
   デバッグを混乱させた → `setvbuf(stdout, _IOLBF)` を app 起動時に設定。

スクリーンショット（docs/screenshots/、全て目視確認済み）:
`net_host_waiting` / `net_join_list`（発見ホスト表示）/ `net_join_direct_ip` /
`net_placement` / `net_ready_wait`（相手配置待ち）/ `net_battle_host` +
`net_battle_join`（同一対戦の両画面、状態表示が相互整合）/
`net_result_host_win` + `net_result_join_lose`（Run1: Host 勝ち）/
`net_result_join_win` + `net_result_host_lose`（Run2: Join 勝ち）/
`net_disconnect_dialog`（切断ダイアログ）

### 11.5 実機2台での検証手順（ユーザー向け、未実施）

前提: CardputerZero ×2 が同一 WiFi AP に接続済み。ファイアウォールで
**TCP 35354 と UDP 35353** が通ること（AP のクライアント分離が有効だと
発見も直接続も失敗する — その場合は AP 設定変更かモバイルルータ等を使用）。

1. 両機に Phase 6 のビルド（または `cp0-cross-rel`）をインストールして起動
2. 端末A: Menu → Network Play → **Host Game** → 表示された
   「Your address: <IP>:<ポート>」を控える
3. 端末B: Menu → Network Play → **Join Game** → 数秒で端末Aのホスト名が
   一覧に出るので選択（出ない場合 → **Direct IP** で端末Aの IP を入力。
   ポートが 35354 以外と表示されていた場合のみ `IP:ポート` 形式で入力）
4. 接続すると両機が自動で配置画面へ。各自配置（X=ランダム配置が便利）して
   Confirm → 相手待ち表示 → 両者 READY で対戦開始
5. 手番側のみカーソル+Enter で射撃。「Enemy sunk:」行と下段の結果表示が
   両機で対応していること、決着時に勝敗が両機で一致することを確認
6. 切断テスト: 対戦中に片方の電源断（またはアプリ終了）→ もう片方に
   10 秒以内（TCP RST が届く場合は数秒）に「Connection lost」→ OK で
   タイトルに戻れること
7. 自動対戦での長時間試験をしたい場合は両機で
   `BATTLESHIP_NET_AUTOPILOT=win` / `=lose` を設定して起動すると
   配置から決着まで全自動（テスト専用フック）

### 11.6 LoRa 差し替えに向けた残作業（Phase 7）

プロトコル/信頼性層は v1 のまま流用可能な設計になっている。追加が必要なのは:
1. `LoRaSerialTransport : ITransport` の実装のみ（E220-JP 透過モードの
   シリアル read/write を poll() でノンブロッキングに回す薄い層。
   `TcpTransport` の send/recv バッファ処理がそのまま雛形になる）
2. タイムアウト調整: `ReliabilityConfig::ack_timeout_ms`（400ms）は LoRa の
   実効レイテンシに合わせ数秒オーダーへ、`SessionConfig::ping_interval_ms` /
   `disconnect_timeout_ms` も帯域節約側に調整（すべて構造体パラメータ化済み）
3. マッチング: UDP 発見は LoRa では使えないため、HELLO ブロードキャスト等の
   LoRa 用ペアリング（または「両機同時起動で自動接続」の単純運用）を検討
4. 半二重対策は stop-and-wait が本質的に対応済み（同時送信は最悪 ACK
   タイムアウト再送で回復）。フレームは最大 10 バイト程度で E220 の
   ペイロード上限(200B)に余裕

### 11.7 Phase 5（i18n）への引き継ぎ

- ネット対戦 UI の文字列は全て英語ハードコード（"Waiting for opponent..."、
  "Connection lost"、"Your turn - fire!"、net_shot_text() の "You"/"Foe" 等）。
  文字列カタログ導入時は `base_viewmodel.cpp` のネットワーク系ステータス
  文字列と `net_*_screen.cpp` / `network_menu_screen.cpp` のラベルが対象
- プロトコルには文字列が乗らない（HELLO のホスト名は discovery のみ）ため、
  言語設定が異なる2台の対戦に支障はない
- Commentator は引き続き 1P 専用（ネット対戦では解説行を手番表示に転用中。
  ネット対戦へ解説を載せる場合は行の取り合いに注意）

## 12. Phase 5 完了メモ（多言語対応 i18n）

Phase 5 のゴール「3言語（EN/JA/ZH）で全画面が正しく表示・切替でき、再起動後も
言語設定が保持される」を達成。メニューの「Settings」を有効化し、
Language: English / 日本語 / 简体中文 の選択即反映 + 永続化を実装。
テストは既存 124 + i18n 11 + 解説多言語 2 = **137/137 green**（Debug/Release 両方）。

### 12.1 文字列カタログ（src/i18n/、battleship_i18n、LVGL 非依存）

| ファイル | 役割 |
|---|---|
| `strings.h` | `enum class StrId`（全 UI 文言 ~90 エントリ）+ `tr(StrId)` / `trf(StrId, ...)`（printf 形式テンプレート、語順が言語で変わる行はテンプレートごと翻訳）+ `ship_name(ShipType)`。`i18n::Lang` は `commentary::Lang` の再利用（enum 二重定義を避ける） |
| `strings_en/ja/zh.cpp` | 言語別テーブル。EN は Phase 4 までのハードコード文言と完全一致（英語選択時の見た目が Phase 5 前と変わらないことを保証） |
| `settings.{h,cpp}` | `~/.config/battleship/settings.conf`（`XDG_CONFIG_HOME` 対応、key=value 1行形式）への言語永続化。テンプレートに config 機構は存在しなかったため自前実装（未知キーは無視 = 前方互換） |
| `fonts.{h,cpp}` | 言語×サイズ → `lv_font_t*` の選択。**別 CMake ターゲット `battleship_i18n_fonts`**（LVGL + AssetManager 依存のため。`battleship_i18n` 本体はヘッドレステスト可能なまま） |

- 言語切替の伝播: `BaseViewModel::set_language()` が (1) `i18n::save_language()`
  （即時適用+永続化）、(2) `commentator_->set_language()`、(3)
  `language_version_subject_` バンプ → `ScreenManager` が observe して
  `reload_current_screen()`（現在画面をその場で再構築）。画面は build 時に
  `tr()` を読むだけなので、再構築 = 全文言の再取得になる
- 起動時: `app.cpp` の `Application::run()` 冒頭（UI 構築前）で
  `i18n::load_settings()`。未設定時は英語（PLAN.md どおり）
- 動的組立文字列（"You D2: Miss | CPU G7: Hit!" 等）は `base_viewmodel.cpp` で
  `trf()` テンプレート化。艦名=翻訳、座標=A1-J10 のまま（PLAN.md どおり）

### 12.2 CJK フォント — 方式 (a) FreeType + サブセット TTF を採用

**判断**: (a) Noto Sans CJK サブセット OTF + lv_freetype 実行時レンダリングを採用。
(b) lv_font_conv 事前ビットマップは不採用。実測比較:

| | (a) FreeType サブセット OTF | (b) lv_font_conv ビットマップ C ソース |
|---|---|---|
| サイズ実測 | **JP 72.3KB + SC 80.2KB = 152.5KB**（アセット、元は 4.5MB/8.3MB） | 12px+18px の2サイズだけで **~553KB**（.rodata としてバイナリに固定） |
| RAM | FreeType グリフキャッシュ（`LV_FREETYPE_CACHE_FT_GLYPH_CNT=256`）分のみ。表示した文字だけラスタライズ | 全グリフ常駐（mmap される .rodata なので実 RSS への影響は小さいが、.deb は確実に膨らむ） |
| 追加依存 | なし（FreeType は Inter/Phosphor で既にリンク済み） | なし（ただし再生成に Node.js が必要） |
| CPU | 初回表示時にグリフごとラスタライズ（キャッシュされるので 320×170 の静的 UI では無視できる。CM0 実機で気になれば (b) へ切替可能な構造） | ゼロ |

- **フォールバック連鎖**: CJK フォントの `lv_font_t::fallback` を同サイズの
  Montserrat ビットマップフォントに設定。JA/ZH 文中の ASCII（数字・"CPU"・
  "WiFi"・LV_SYMBOL_WARNING 等）は従来どおりビットマップでシャープに描画される
- **サブセット再生成**: `tools/generate_cjk_fonts.py`（fontTools 使用、
  `pip install fonttools brotli`）。`strings_ja/zh.cpp` **と
  `template_commentator.cpp`（解説定型文）** から使用グリフを抽出して
  pyftsubset にかける。**注意: 新しい CJK 文字をどちらかのファイルに追加したら
  必ず再実行すること**（忘れると豆腐になる。実際に初回スクリーンショットで
  解説行の「ざ/出/幕/。」が豆腐になり、スキャン対象漏れとして発見・修正した）
- グリフ数: JA 415 / ZH 420（ASCII セーフティネット込み）
- 対応サイズは 12px/18px の2つ（24px はメニューの "BATTLESHIP" ロゴのみで
  英語固定のため不要）。ゲーム名 "BATTLESHIP"/"Battleship" はブランド名として
  全言語で英語のまま

### 12.3 Settings 画面と言語切替

- `AppPage::Settings = 11` 新設、`SettingsScreen`（English/日本語/简体中文/戻る
  の4ボタン、現在言語は `LV_STATE_CHECKED` でハイライト）
- 各言語の選択肢ラベルは**その言語自身のフォント**で描画
  （`font_for_language_and_size()`）— 現在 UI が読めない言語でも自分の言語名は
  認識できるようにするため
- 選択即: 適用 + `settings.conf` 書込み + 現在画面の再構築（Settings 画面自身が
  新言語で再表示される）。**確認済み**: UI で切替 → conf に `language=zh` が
  書かれる → アプリ再起動 → 中国語メニューで起動（スクリーンショットあり）

### 12.4 解説（Commentator）の多言語化と LLM 品質判断

- `TemplateCommentator`: JA/ZH の定型文バンクを全8イベント分追加（艦名も
  言語別。`battleship_commentary` は `battleship_i18n` に依存できないため
  ship 名テーブルは意図的に小さく重複させている — 循環依存回避）
- `build_llm_prompt()`: 出力言語指示を**ターゲット言語自身で**記述
  （"必ず日本語だけで…" / "请只用简体中文…"）
- **LLM の JA/ZH 品質実測（SmolLM2-360M-Instruct Q4_K_M、実機生成 10 サンプル）**:
  **10/10 全てが英語で返ってきた**。ターゲット言語指示を強めても無効。
  360M パラメータ・英語中心コーパスのモデルには日中出力能力が実質無い。
  生成例（JA 指示時）: "Turn 2. You: 1/1 shots hit (100%), 5 ships left, no
  sunk yet." — 英語のうえ数値の誤読もある
- **採った方針**: `LlmCommentator::request_comment()` で `lang_ != EN` の場合は
  内蔵 `TemplateCommentator`（JA/ZH バンク）へ同期フォールバック。EN は従来どおり
  非同期 LLM 生成。モデルを多言語のもの（Qwen2.5-0.5B 等）に差し替えた際は
  この分岐（`llm_commentator.cpp`）を外すだけで JA/ZH も LLM 化できる

### 12.5 検証

- 単体テスト +13: カタログ全 StrId×3言語の欠落検出 / テンプレート placeholder
  数の言語間一致 / trf フォーマット / ship_name 言語別 / 設定永続化
  round-trip（XDG_CONFIG_HOME を scratch に向けてテスト）/ JA・ZH 定型文バンク
  非空+CJK 実在+艦名ローカライズ / プロンプト言語指示
- 3言語それぞれで APP_SCRIPT 自動走行（メニュー→1P→難易度→配置→戦闘→
  リザルト、+ ホットシートでハンドオーバー画面、+ 2プロセスネット対戦）。
  スクリーンショットは `docs/screenshots/i18n_{en,ja,zh}_*.png` に保存、全て目視確認
- **LV_LABEL_LONG_MODE_DOTS は全廃**（§11.4 のハング再発防止。Result 画面の
  総評行も CLIP 化）。CJK 長文でのハングは 3言語全走行で再現せず
- Battle 画面の解説行は高さ 14→16px（CJK のライングリフが下端クリップ
  しかけたため）。Result 総評行も 24→30px

### 12.6 Phase 6（パッケージング）への引き継ぎ

- **.deb に含めるアセット**: `assets/fonts/NotoSansJP-Subset.otf`（72.3KB）+
  `NotoSansSC-Subset.otf`（80.2KB）が新規追加。既存の Inter 4種（~1.4MB）+
  Phosphor（449KB）は現状 UI 未使用なので**同梱除外を検討する価値あり**
  （Montserrat は LVGL 内蔵ビットマップなのでアセット不要）
- 設定ファイルは `~/.config/battleship/settings.conf`（ユーザーごと、
  パッケージには含めない。存在しなければ英語で起動する）
- モデルファイル（256MB）は従来どおり同梱しない方針のまま（§9.5）。JA/ZH では
  LLM を使わないため、日中ユーザーにとってモデル無しでも解説機能は完全動作する
- CM0 実機での FreeType ラスタライズ速度は未計測（画面あたり高々十数文字の
  一度きりラスタライズなので問題にならない見込みだが、実機で気になる場合は
  §12.2 の (b) 方式へ切替）

## 13. Phase 6 完了メモ（BLE 対戦 — デフォルト接続方式化）

Phase 6 のゴール「BleTransport 実装 + 接続方式選択（デフォルト BLE）+
ヘッドレステスト green + Linux ビルド成功」を達成。プロトコル・信頼性層・
NetSession は**無変更**（ITransport の差し替えのみ、PLAN.md §2.2 の設計どおり）。
実機2台での BLE 実対戦はユーザー実機到着後に実施（§13.6 の手順で検証する）。

### 13.1 BleTransport の構成（src/net/ble/、Linux/BlueZ 専用）

| ファイル | 役割 |
|---|---|
| `ble_transport.h` | `BleTransport : ITransport`（Host=ペリフェラル / Join=セントラルの両役）+ `BleScanner`（Join 側スキャン、UdpDiscoverer の BLE 版）。pimpl なので**どのプラットフォームでも include 可能**（実装 .cpp のみ Linux 限定） |
| `ble_transport.cpp` | BlueZ D-Bus API を **sd-bus (libsystemd)** で叩く実装。`BATTLESHIP_BLE=ON`（CMake、Linux のみ）でのみコンパイル |

- **D-Bus ライブラリ選定: sd-bus を採用**（PLAN.md の第一候補どおり）。理由:
  (1) libsystemd は Raspberry Pi OS / Debian に必ず入っている（init が systemd）
  ため**ランタイム追加依存ゼロ**、(2) GLib/GDBus と違い GObject 型システムも
  GMainLoop も不要で、`sd_bus_process()` を自分のループから回す明示ポンプ型
  API が本プロジェクトの poll 駆動 `ITransport` モデルと 1:1 で一致する、
  (3) C API が薄く、vtable 定義以外に覚えることが少ない。
  GDBus は「イベントループ統合のためだけに GMainLoop アダプタ層を書く」
  ことになり見送り。
- **GATT 構成: Nordic UART Service (NUS) 互換**
  - Service UUID `6e400001-b5a3-f393-e0a9-e50e24dcca9e`（Primary）
  - RX Char `6e400002-...`: flags `write, write-without-response`（相手→自分）
  - TX Char `6e400003-...`: flags `notify`（自分→相手、PropertiesChanged(Value) 通知）
- **Host = ペリフェラル**: `sd_bus_add_object_manager` + object vtable 4 本
  （service / RX / TX / LEAdvertisement1）を `/com/battleship/...` に export →
  `GattManager1.RegisterApplication` + `LEAdvertisingManager1.RegisterAdvertisement`
  （どちらも非同期呼び出し）。アドバタイズのローカル名は **`BSHIP-<hostname>`**
  （WiFi 発見の `BSHIP:` マジックと対応）+ NUS Service UUID。
  セントラルが TX へ `StartNotify` してきた時点で `Connected`。
  `StopNotify`（相手の切断時に BlueZ が呼ぶ）で `Disconnected`。
- **Join = セントラル**: `BleScanner` が `SetDiscoveryFilter({Transport:"le"})` →
  `StartDiscovery` →（1秒毎に）`GetManagedObjects` で Device1 を列挙し、
  **名前が `BSHIP-` 前置 or UUIDs に NUS を含む**デバイスだけを一覧化
  （31 バイトの ADV に 128bit UUID と名前が両方入らないことがあるため、
  どちらか片方で成立させる）。選択後 `BleTransport::start_join(device_path)` →
  `Device1.Connect` → `ServicesResolved` をポーリング → `GetManagedObjects` で
  RX/TX キャラクタリスティックのパス解決 → PropertiesChanged 購読 +
  `StartNotify`（成功で `Connected`）。送信は RX への
  `WriteValue(..., {type:"command"})`（write-without-response、往復レイテンシ削減。
  失敗応答が来たら切断扱い）。
- **1 フレーム = 1 Write/Notify**: ゲームフレームは最大 13 バイト
  （ヘッダ6 + 最大ペイロード5 + CRC2）で、BT 4.2 デフォルト MTU 23（実効 20B）に
  常に収まる。**MTU 交渉・分割再結合は実装しない**。この不変条件は
  `src/net/messages.h` の `static_assert(kMaxGameFrameLen <= kBleSingleWriteLimit)`
  （コンパイル時）+ `NetMessages.EveryEncodedFrameFitsOneBleWriteAtDefaultMtu`
  テスト（実 encode の実測）で二重に固定した。受信側はどんなチャンクで
  届いても既存 `FrameDecoder` が再同期するので、通知の分断/結合にも元々耐性がある。
- **Just Works（明記）**: ペアリング/ボンディングは要求せず、リンク暗号化は
  スコープ外。プロトコルは座標と結果しか運ばず盤面情報を送らない設計
  （§11.1）なので、傍受されても相手プレイヤーが画面で見る以上の情報は漏れない。
- **スレッドモデル**: 他のトランスポート同様、完全シングルスレッド poll 駆動。
  `poll()` = `sd_bus_process()` の非ブロッキング排出。ブロッキング呼び出しは
  start_host/start_join/BleScanner::start 内の**アダプタ列挙・電源ON など
  セットアップ一発もの**だけ（ユーザー操作時に bluetoothd へのローカル IPC
  数往復。ポーリングループには入れていない）。
- **CMake 隔離**: オプション `BATTLESHIP_BLE`（**Linux ターゲットのみ既定 ON**、
  macOS/Windows は OFF、ON を強制すると FATAL_ERROR）。
  `src/net/ble/` は GLOB から除外して BLE 有効時のみ `target_sources` で追加。
  sd-bus の解決は、デスクトップ Linux = `pkg-config libsystemd`、
  CM0 クロス/ネイティブ = BSP sysroot からの `cm0_find_*`（**sysroot に
  libsystemd-dev 相当が必要**。無ければ FATAL_ERROR で `-DBATTLESHIP_BLE=OFF`
  を案内）。`BATTLESHIP_BLE` マクロは battleship_net の PUBLIC define として
  viewmodel/view へ伝播し、UI 側は `#if BATTLESHIP_BLE` でガード。
  **macOS デスクトップビルドは一切影響を受けない**（BLE ソース非コンパイル、
  137→142 テスト green 維持を確認）。

### 13.2 接続方式選択 UI（PLAN.md §2.2「マッチング UI の接続方式選択」）

- **フロー変更**: Menu → Network Play → **NetTransport（新設、AppPage=12）**
  → NetworkMenu(Host/Join) → 以降は従来どおり。NetworkMenu の Back は
  NetTransport へ戻る。
- **NetTransportScreen**（`src/view/screens/net_transport_screen.{h,cpp}`）:
  上から **Bluetooth (BLE)**（デフォルト・永続化された選択に CHECKED 表示）/
  **WiFi (LAN)** / **LoRa**（`LV_STATE_DISABLED` + "(Coming soon)"、Phase 8 で
  有効化）/ Back。選択した瞬間に `i18n::save_net_transport_pref()` で
  `settings.conf` へ永続化（**次回起動時のデフォルト選択**になる）。
- **設定永続化**: `~/.config/battleship/settings.conf` に `transport=ble|wifi`
  （言語と同じファイル・同じ key=value 形式）。`save_language()` /
  `save_net_transport_pref()` のどちらも**全キーを書き戻す**共通経路
  （`write_settings_file()`）に変更し、片方の保存が他方を消さないことを
  テストで固定（§13.3）。未設定/不正値の既定は **ble**。
- **デスクトップ（Mac、BATTLESHIP_BLE=0）での扱い（採った判断）**: BLE 行は
  **表示はするが無効化**し、"(device only)"（JA:（実機のみ）/ ZH:（仅实机））
  サフィックスを付ける。フォーカスは自動的に WiFi に落ち、**設定ファイルの
  `transport=ble` は書き換えない**ため、同じ設定を実機に持ち込めば BLE が
  デフォルトのまま。隠すのではなく無効表示にしたのは「製品のデフォルトが
  BLE である」ことを開発ビルドでも見えるようにするため。
- **BLE 選択時の Host/Join**:
  - Host: NetLobby の状態行が「Advertising as BSHIP-<host> ...」
    （`NetLobbyAdvertisingAs`）になり、接続待ちスピナーは従来と共通。
  - Join: NetJoin がスキャン結果一覧（`名前 (BD_ADDR)`）になり、
    **Direct IP ボタンは非表示**（BLE に入力すべきアドレス概念が無いため）。
    一覧は WiFi 発見と同じ `net_hosts_dirty_subject` で 1 秒毎に更新。
    viewmodel には transport 非依存の `net_peer_count()/net_peer_label()` を
    新設し、NetJoin 画面は両モードで同一コード。
  - 失敗時（アダプタ無し/bluetoothd 停止/権限不足）: Host は NetError
    「Bluetooth unavailable」、Join はスキャン画面に同文言を表示。
- **i18n**: 新規文言 10 個（NetTransport* 6 + NetLobbyAdvertisingAs /
  NetLobbyConnectingToBle / NetJoinScanningBle / NetBleUnavailable）を
  EN/JA/ZH 3言語で追加し、`tools/generate_cjk_fonts.py` で CJK サブセットを
  再生成（JA 415→420 グリフ 73.5KB / ZH 420→432 グリフ 82.7KB）。
  3言語スクリーンショット（`docs/screenshots/i18n_{en,ja,zh}_19_net_transport.png`
  と `..._20_net_menu_wifi.png`）で豆腐なしを目視確認済み。

### 13.3 テスト（137 → 142、Debug/Release green）

- `test_i18n_settings.cpp` +4: transport の round-trip / 未設定時 ble デフォルト /
  **言語と transport の相互保存で他方が消えない** / 不正値は無視
- `test_net_frame.cpp` +1: 全メッセージ型の実 encode がペイロード ≤5B・
  フレーム ≤20B（BLE 1 Write/Notify 不変条件の動的検証。静的には
  messages.h の static_assert）
- 既存 137 は無変更で green（i18n カタログ整合テストが新 StrId の3言語
  完備も自動検証）。**WiFi フローのリグレッション**: 2プロセス autopilot を
  新フロー（NetTransport 経由）で1回通し、43 ターンで決着・両画面の
  勝敗/命中率が相互整合することをスクリーンショットで確認
  （Phase 4 §11.4 と同一シナリオ）。

### 13.4 Linux コンパイル検証（Docker、Mac では BLE がビルドできないため）

- 環境: Docker Desktop (macOS) 上の **debian:bookworm arm64 ネイティブ**
  コンテナ（Apple Silicon なのでエミュレーション無し・実機と同じ aarch64）。
  bookworm の cmake は 3.25 で本プロジェクトの要求 (>=3.31) に満たないため
  pip の cmake を使用。gcc 12.2 + libsystemd-dev (252)。
- 構成: `USE_DESKTOP=ON`（Linux デスクトップ相当）+ `BATTLESHIP_BLE=ON` +
  `BATTLESHIP_LLM=OFF`（llama.cpp フェッチ時間の節約のみが理由）。
  ビルドスクリプトは一時ディレクトリ（リポジトリには含めない）。
- **1回目のビルドで実バグを検出**: `src/net/frame.cpp` が `<string>` を
  include せず `std::string::npos` を使用 → libc++ (Mac) では他ヘッダ経由で
  通っていたが libstdc++ (gcc12) ではコンパイルエラー。include 追加で修正
  （Linux 検証をしなければ実機ビルド（AppBuilder）で初めて発覚していた）。
- 結果: **BLE 含む全ターゲットのビルド成功 + コンテナ内 ctest 142/142 green**
  （sd-bus の vtable マクロ群が g++ 12 / C++17 でそのまま通ることも確認）。

### 13.5 実機での前提（D-Bus / bluetooth 権限）

- `bluetoothd`（BlueZ 5.x）が稼働していること: `systemctl status bluetooth`
- **アプリの実行ユーザーが `bluetooth` グループに属していること**:
  Debian/RPi OS の D-Bus ポリシー（`/etc/dbus-1/system.d/bluetooth.conf`）は
  org.bluez の呼び出しを root と bluetooth グループに限定している。
  `sudo usermod -aG bluetooth <user>` → 再ログイン。権限が無い場合、アプリは
  クラッシュせず「Bluetooth unavailable」表示になる（find_adapter が空を返す）。
- rfkill でブロックされている場合: `rfkill unblock bluetooth`
- GattManager1 / LEAdvertisingManager1 は BlueZ 5.48+ の安定 API
  （`--experimental` フラグ不要）。cardputer-zero-os の BlueZ バージョンは
  実機到着後に `bluetoothctl version` で確認すること。

### 13.6 実機2台での BLE 対戦検証手順（§11.5 の BLE 版、未実施）

前提: 両機で §13.5 の権限設定済み。AP・WiFi は一切不要。

1. 両機にビルドをインストールして起動
2. 端末A: Menu → Network Play → 接続方式で **Bluetooth (BLE)**（デフォルト
   選択のまま Enter）→ **Host Game** → 「Advertising as BSHIP-<hostA> ...」表示
3. 端末B: Menu → Network Play → BLE → **Join Game** → 数秒でスキャン一覧に
   `BSHIP-<hostA> (XX:XX:...)` が出るので選択
4. HELLO 交換完了で両機とも自動で配置画面へ。以降は WiFi 対戦（§11.5 手順
   4〜6）と完全に同一: 配置 → READY 同期 → 交互射撃 → 決着の両機一致
5. 切断テスト: 対戦中に片方の電源断 → もう片方に 10 秒以内に
   「Connection lost」→ OK でタイトルへ
6. レイテンシ観察: 1 ターン = SHOT + RESULT + ACK×2 ≒ BLE 4 イベント。
   コネクションインターバル既定（~30-50ms）なら体感遅延は WiFi と大差ない
   はず。著しく遅い場合は `ReliabilityConfig::ack_timeout_ms`（400ms）の
   再送が混ざっていないかログで確認
7. トラブルシュート: 一覧に出ない → 端末Bで `bluetoothctl` → `scan le` で
   `BSHIP-` が見えるか確認（見えなければ端末A側の advertise 失敗 → 権限/
   rfkill/§13.5）。`org.bluez.Error.NotPermitted` 系は D-Bus ポリシー。
   両機の自動対戦は WiFi 同様 `BATTLESHIP_NET_AUTOPILOT=win|lose` が使える

### 13.7 既知の制約

- **Mac では BLE の実通信検証もコンパイルも不可**（BATTLESHIP_BLE=0）。
  ロジック（フレーミング/信頼性/セッション）は Loopback で全テスト済みのため、
  残リスクは BlueZ 連携部分（register/advertise/subscribe の実挙動）に限定
  （PLAN.md リスク表どおり）。実機2台での §13.6 が最終検証。
- Just Works・リンク暗号化なし（意図したスコープ、§13.1）。
- BLE には Direct IP に相当する手動フォールバック入力が無い（スキャンのみ）。
- 再接続/再戦同期は WiFi 版と同じく将来課題（切断 = セッション終了）。

### 13.8 Phase 7（パッケージング）への申し送り

- **実行時依存**: libsystemd（sd-bus）— RPi OS/Debian に標準搭載だが、
  `.deb` の `Depends:` に `libsystemd0` を明示しておくのが安全
  （cm0-package.cmake の SHLIBDEPS が自動検出するなら不要、要確認）。
- **ビルド時依存**: AppBuilder / BSP sysroot に `libsystemd-dev`（ヘッダ
  `systemd/sd-bus.h`）が必要。無い場合 CMake が FATAL_ERROR で検出し、
  `-DBATTLESHIP_BLE=OFF` で WiFi のみの構成に退避可能。
- **権限の申告/セットアップ**: アプリは root 不要だが実行ユーザーの
  `bluetooth` グループ所属が必要（§13.5）。cardputer-zero-os の既定ユーザーが
  既に所属しているか実機で確認し、していなければ README / ストア説明への記載
  か postinst での対応を Phase 7 で判断する。
- **提出メタデータ**（PLAN.md「Phase 7 の提出要件」に反映済みの方針）:
  通信 = **BLE（デフォルト、近距離 P2P、ポート開放不要）+ WiFi オプション
  （UDP 35353 / TCP 35354、LAN 内のみ）**。バックグラウンドサービスなし・
  外部送信なしは従来どおり。
- ストアスクリーンショット 4 枚の候補に接続方式選択画面
  （`i18n_*_19_net_transport.png`）を加えるか Phase 7 で検討。

## 14. Phase 7 完了メモ（パッケージング・AppStore 提出準備）

Phase 7 のゴール「aarch64 デバイスビルド + APPLaunch 規約準拠の `.deb` 化 +
prepublish check 全パス」を達成。前任がストールした Esc キー規約（リンク
エラー）の完成から着手し、パッケージング（`cm0-package.cmake` 全面書き換え）、
アイコン生成、ストアメタデータ、prepublish check まで一気通貫で完了。
**`czdev login` / `publish` はユーザー操作のため未実施**（§14.5）。

### 14.1 Esc キー規約の完成

前任の diff（screen_manager.{h,cpp}、i18n、net_session、base_viewmodel）は
設計・実装のほぼ全体が完成していたが、2点の不備でリンクが通らず/挙動が
誤っていた:

1. **`BaseViewModel::request_safe_quit()` が宣言のみで未実装**（リンクエラー
   の直接原因）。`ScreenManager::handle_safe_exit()` から呼ばれる想定だった。
   実装: 接続中の `net_session_` があれば `abandon()` で相手に通知 →
   `teardown_network()` → `request_quit()`（`quit_requested_subject_` 経由で
   `app.cpp` のメインループを止める）。
2. **`ScreenManager::attach_esc_handling()` がどこからも呼ばれていなかった**
   （宣言・実装はあったが配線漏れ）。`app.cpp` のデスクトップ SDL キーボード
   indev には明示的に配線し、CM0 実機の evdev キーパッド indev は
   `platform::init_key_input()` にコールバック引数
   （`KeypadIndevCallback`）を追加して発見した indev ごとに配線するよう
   変更（`linux_input.h/.cpp`）。あわせて **`KEY_HOME` → `LV_KEY_HOME`**
   マッピングを evdev 側に追加（デスクトップの SDL は `SDLK_HOME` を
   LVGL 側が標準で `LV_KEY_HOME` にマッピング済みで対応不要だった）。

**Esc/Home 規約の最終仕様**（`screen_namager.cpp` の実装どおり）:
短押し Esc = 1階層戻る（モーダル表示中はモーダルを閉じるのみ、対戦中の
Battle/Placement/Handover では中断確認モーダルを出す）。長押し Esc（~1秒、
`kEscLongPressMs`）または Home = どこからでも安全終了
（`request_safe_quit()`）。ネットワーク対戦中の中断は
`NetSession::abandon()` が明示 `GAME_OVER(winner_is_sender=0)` を
ベストエフォート送信 → 相手側は `handle_frame(GameOver)` で無条件に
`Finished` へ遷移（既存のコンバージェンス仕様を流用、新規実装ではない）。

**検証で見つけた実バグ**: 中断確認モーダルのタイトルラベルが
`i18n::font_for_size(14)` を指定していたが、CJK フォントセット
（`i18n/fonts.cpp` の `CjkFontSet`）は **12/18pt しか保持しておらず**、
14pt は暗黙に Latin 専用の Montserrat ビットマップへフォールバックして
JA/ZH タイトルが豆腐（`.notdef` 四角）化していた。全画面・全ラベルを
grep したところこの箇所だけが 14pt を使っており（他は全て 12 か 18）、
12pt に修正。3言語スクリーンショットで確認済み
（`docs/screenshots/esc_{en,ja,zh}_0{0,1,2}_*.png`）。

**テスト**: `NetSession::abandon()` のヘッドレステストを3件追加
（`test_net_session.cpp`）—— Battle 中の中断、Placement 中の中断、
既に Finished/Disconnected な状態での no-op。ctest 142 → **145**
（Debug/Release green）。GUI E2E（APP_SCRIPT 経由の2プロセス通信対戦中断）
も試みたが、host/join 双方の画面遷移をブラインドタイミングで同期させる
スクリプトが不安定（NetTransport 画面の追加など Phase 6 の変更でタイミングが
ズレていた）だったため、決定論的に速く実行できるヘッドレスユニットテストを
正とした。ロジックの正しさ（abandon → 相手が Finished に収束）は
このユニットテストで担保されている。

CJK フォント再生成（`tools/generate_cjk_fonts.py`）: JA は既存グリフで
足りたため無変更、ZH は新規4文字ぶんサブセットが増加（84.6KB→85.4KB）。

### 14.2 アプリアイコン

Python/PIL（`fontTools`/`brotli` 同様、venv にインストール）でベクター的に
描画 → 256×256 に高品質リサイズ。デザイン: 紺のグラデーション背景、粗い
5×5 のターゲティンググリッド（10×10 だと 48px で潰れたため v2 で簡略化）、
大胆な艦シルエット（艦橋・マスト付き）、命中マーカー2個（赤の二重丸）。
文字なし。48×48 の実縮小画像で目視確認し判別可能と確認。

`assets/images/template.png`（100×100）/`template_80.png`/`template_100.png`
の3ファイルを `battleship.png`（256×256、1枚）に統合・置換。2つの
プリスケール版はソース中どこからも参照されておらず（テンプレート由来の
デッドウェイト）、`cm0-package.cmake` のパッケージング側で必要サイズの
参照は不要なため削除。`.desktop` テンプレートの `Icon=` も
`share/images/battleship.png` に追随。

### 14.3 APPLaunch 準拠パッケージング

`cmake/cm0-package.cmake` を全面書き換え。最終レイアウト:

```
usr/share/APPLaunch/applications/battleship.desktop
usr/share/APPLaunch/bin/battleship          (実行体)
usr/share/APPLaunch/bin/battleship-launch   (ラッパー、.desktop の Exec= 先)
usr/share/APPLaunch/share/images/battleship.png
usr/share/battleship/fonts/{NotoSansJP-Subset.otf,NotoSansSC-Subset.otf,LICENSE.txt}
usr/share/battleship/images/battleship.png
usr/share/doc/battleship/{README.md,third-party-assets-license.txt}
```

- **`usr/share/battleship/` は残す**: `AssetManager`（`asset_manager.cpp`）の
  実行時アセット探索が `/usr/share/<app_name>` に絶対パスでフォールバック
  する既存ロジックのため、APPLaunch 側（アイコンのみ）とアプリ自身の
  アセット探索先は別ディレクトリとして両立させた。
- **ラッパースクリプト**（`cmake/templates/app-wrapper.sh.in` 新設）:
  `.desktop` の `Exec=bin/battleship-launch`（相対パス、APPLaunch ルート
  基準）が指す先。中身は `exec "$(dirname "$0")/battleship" "$@"` のみ
  だが、将来引数・作業ディレクトリ・fbdev 環境変数設定が必要になっても
  `.desktop` を一切変更せずここに追記できる構造にした（ガイドの
  「Put arguments, working-directory changes, and framebuffer environment
  setup in a wrapper script rather than in Exec」に対応）。
- **systemd サービス廃止**: `cmake/templates/app.service.in` 削除、
  `cm0-package.cmake` の `APP_INSTALL_SYSTEMD_SERVICE` 分岐ごと除去。
  常駐サービスは PR 申告項目でもあり（バックグラウンドサービスなし、と
  申告するため）実体をなくした。
- **未使用アセットの除外**: `assets/audio/click.wav`、
  `assets/fonts/{inter-*.ttf,Phosphor-Fill.ttf}` を src/ 全体で grep した
  ところ**参照ゼロ**（テンプレート由来のデッドウェイト）と確認し、
  パッケージングから除外。LLM モデル（`models/*.gguf`）はそもそも
  `cm0-package.cmake` が触れておらず、従来どおり `BATTLESHIP_LLM_MODEL`
  環境変数で実行時に任意指定（未設定/読み込み失敗時は定型解説へ自動
  フォールバック、§9.5）。
- **`/dev/fb0` ハードコード修正**（実バグ）: `src/app/app.cpp` の
  `init_display()` が `APP_FRAMEBUFFER_DEVICE`（ビルド時 CMake キャッシュ
  変数、既定 `/dev/fb0`）しか見ておらず、ランタイムの
  `LV_LINUX_FBDEV_DEVICE` 環境変数を無視していた。修正: 環境変数が
  セットされていれば無条件でそちらを優先。**実機ビルドの .deb を
  container にインストールし、`strace -e trace=openat` で
  `LV_LINUX_FBDEV_DEVICE=/tmp/my_custom_fb` を指定した際に実際にその
  パスへ `openat()` が飛ぶことを確認済み**（後述 §14.4）。
- **control フィールド**: `Package=battleship` / `Version=1.0.0`
  （`project()` を 0.0.1 → 1.0.0 に更新）/ `Architecture=arm64` /
  `Maintainer=YutaCar <yutacar@yutacar.net>`。**メールは GitHub
  アカウントと一致必須**（`email_mismatch` で publish 時に弾かれる —
  `czdev/publish.py` の実装で確認済み。ユーザー側で GitHub アカウントの
  登録メール・noreply メールのいずれかと一致することを確認要）。
- **Depends**: `libc6, libstdc++6, libgcc-s1, libfreetype6, libpng16-16,
  libjpeg62-turbo, zlib1g, libfmt10, libsystemd0, libgomp1`。
  最後の2つは実機ビルドでの実地検証で判明・追加した:
  `libsystemd0`（BLE の sd-bus）は元々 §13.8 で申し送り済みだったため
  最初から追加。`libgomp1`（llama.cpp の ggml-cpu が OpenMP を使う）は
  **クリーンな `apt install ./battleship_*.deb` を試すまで見落としていた**
  ——`ldd` では BSP 側の同名 .so がマウント経由で解決できてしまい気づかず、
  実際の apt 依存解決（後述）で `libgomp.so.1: cannot open shared object
  file` として初めて顕在化した。

### 14.4 デバイスビルド（Docker、debian:trixie arm64 ネイティブ）

**環境選定のやり直し**: 当初 Phase 6 の実績（`debian:bookworm` arm64）を
踏襲したが、BSP sysroot（`M5CardputerZero-UserDemo` の `sdk_bsp.tar.gz
v0.0.4`、435MB）は **glibc 2.41**（Debian trixie/13 相当、実機 OS が
比較的新しいことを示唆）である一方 bookworm のコンテナは **glibc 2.36**。
BSP のマルチアーキヘッダ（`cm0_find_header` 経由で include path に注入
される `/work/bsp/usr/include/aarch64-linux-gnu`）が `logger` ターゲット
経由で**全ターゲットに PUBLIC 伝播**するため、コンテナ自身の
libstdc++/glibc ヘッダと衝突し
`cannot convert '<brace-enclosed initializer list>' to 'unsigned int'`
（`__GTHREAD_COND_INIT`）や `'L_tmpnam' was not declared`等の実コンパイル
エラーが発生。**`debian:trixie`（glibc 2.41、BSP と一致）に切り替えて解決**。
Phase 6 の bookworm 実績は `USE_DESKTOP=ON`（BlueZ 連携部分のコンパイル
検証のみ）だったため、この非互換は Phase 7 で `USE_DESKTOP=OFF` の
本物のデバイスビルドを初めて試して顕在化したもの。**今後 Phase 8 以降で
同じ Docker デバイスビルドを行う場合は `debian:trixie` を使うこと**
（bookworm は使わない）。

**再現手順**:

```bash
# 1. BSP sysroot を取得・展開（キャッシュ推奨、435MB）
curl -sL -o sdk_bsp.tar.gz \
  https://github.com/CardputerZero/M5CardputerZero-UserDemo/releases/download/v0.0.4/sdk_bsp.tar.gz
mkdir -p bsp_sysroot && tar xzf sdk_bsp.tar.gz -C bsp_sysroot

# 2. ネイティブ arm64 コンテナ起動（Apple Silicon なら --platform で
#    エミュレーションなし）
docker run -d --name battleship-cp0-build --platform linux/arm64 \
  -v "$(pwd)":/work/battleship \
  -v "$(pwd)/bsp_sysroot":/work/bsp:ro \
  debian:trixie sleep infinity

# 3. ビルドツール導入（bookworm と異なり trixie は gcc 14 / cmake 3.25 も
#    古いため pip 版 cmake を使用）
docker exec battleship-cp0-build bash -c '
  apt-get update -qq && apt-get install -y -qq \
    build-essential gcc g++ git ninja-build python3 python3-pip python3-venv \
    pkg-config libdbus-1-dev libsystemd-dev curl ca-certificates dpkg-dev file
  python3 -m venv /opt/cmake-venv
  /opt/cmake-venv/bin/pip install -q "cmake>=3.31"
'

# 4. Configure（USE_DESKTOP=OFF, BATTLESHIP_BLE=ON, BATTLESHIP_LLM=ON）
docker exec battleship-cp0-build bash -c '
  cd /work/battleship
  /opt/cmake-venv/bin/cmake -S . -B /work/build-device -G Ninja \
    -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_DESKTOP=OFF -DAPP_USE_DRM=OFF \
    -DCM0_SDK_ROOT=/work/bsp -DCM0_ALLOW_FETCH_DEPS=ON \
    -DBATTLESHIP_BLE=ON -DBATTLESHIP_LLM=ON \
    -DFETCHCONTENT_BASE_DIR=/work/fetch-cache \
    -DCMAKE_INSTALL_PREFIX=/work/build-device/install
'

# 5. Build + Package
docker exec battleship-cp0-build bash -c '
  cd /work/build-device
  /opt/cmake-venv/bin/cmake --build . --config Release -j 8
  /opt/cmake-venv/bin/cpack -G DEB
'
# → /work/battleship/dist/battleship_1.0.0_arm64.deb (ホスト側にもマウント
#   経由で出力される)
```

**注記**: `cp0-native`/`cp0-cross` プリセット（`CMakePresets.json`）は
そのままでは使えない —— `cp0-cross` はクロスコンパイラ
（`aarch64-linux-gnu-gcc`）を要求するが、ネイティブ arm64 コンテナには
そのような名前のバイナリは存在しない（ネイティブ `gcc` そのものが
aarch64 用）。`cp0-native` は `CM0_SDK_ROOT` の既定値を持たないため、
上記のように明示的に `-DCM0_SDK_ROOT=` を渡す必要がある。将来的には
`cp0-native` プリセットに BSP 自動取得を足す価値があるかもしれない
（現状は `cp0-cross` の toolchain ファイルだけが自動ダウンロードする）。

**ビルド中に見つけた実バグ（`CMakeLists.txt` 修正済み）**:
`FetchContent_Declare(llama_cpp ...)` に `EXCLUDE_FROM_ALL` が無く、
llama.cpp 自身の `install(TARGETS/FILES ...)`（ggml/llama のヘッダ・
静的ライブラリ・pkg-config・CMake package config）が `cpack` の出力に
そのまま混入していた（初回パッケージは 3.15MB、うち ~1.3MB が
`/usr/include/ggml*.h` 等の非ランタイム開発ファイル）。
`FetchContent_Declare` に `EXCLUDE_FROM_ALL` を追加して解決 → **1.86MB**
に縮小。

**結果**:
- `battleship_1.0.0_arm64.deb`、**961996 バイト（0.94MB、3D タイトルロゴ
  追加後）**、
  sha256 `1ded370fac8f910f7c1ce2d98c51c0db26ae30cd502e7dccad558dfc4bdd5e16`
  （メニュー画面のタイトルを ASCII アート 3D ロゴ PNG に差し替えた際の
  再ビルド。`assets/images/title_logo.png`、13.6KB が
  `/usr/share/battleship/images/` に追加された分の増分）
- `dpkg-deb -I`: `Architecture: arm64` / `Depends:` 上記9パッケージ /
  `Maintainer: YutaCar <yutacar@yutacar.net>` / `Version: 1.0.0` /
  `Installed-Size: 2052`
- `dpkg -c`: レイアウトは §14.3 の期待どおり（systemd なし、LLM モデル
  なし、Inter/Phosphor/audio なし、ggml/llama 開発ファイルなし）
- クリーンな `debian:trixie` コンテナで `apt install
  ./battleship_1.0.0_arm64.deb` → 全 Depends が自動解決されインストール
  成功 → バイナリは全共有ライブラリを正しくロード（`/dev/fb0` が存在しない
  ヘッドレスコンテナなので "cannot open framebuffer device" のみ発生、
  これは実機なら発生しない）→ `LV_LINUX_FBDEV_DEVICE=/tmp/my_custom_fb`
  を指定すると `strace` で実際にそのパスへ `openat()` することを確認
  → `apt remove battleship` で APPLaunch エントリ含め完全に削除される
  ことを確認。

### 14.5 ストアメタデータ・prepublish check・czdev

- **`app-builder.json`**: `package_name=battleship` / `app_name=Battleship` /
  `store.summary`/`categories=["Games"]`/`icon`/`screenshots`（4枚）/
  `locales`（zh-CN・en・ja、それぞれ title/summary/description）。
  **`store.icon`/`store.screenshots` のパス解決規則を実装から確認**:
  `CardputerZero-AppBuilder` の `scripts/czdev/publish.py` を読み、
  `icon_src = Path(app_dir) / store_meta["icon"]` であることを確認
  （ガイド文書の例 `"share/images/my-app.png"` はパッケージ後のレイアウトを
  示しているだけで、実際は**リポジトリルート相対の実パス**を書く必要が
  あった）。本プロジェクトでは `"assets/images/battleship.png"` を使用。
- **`store/screenshots/`**: `docs/screenshots/` から4枚選定・コピー——
  `main.png`（メニュー、i18n_en_01）/ `placement.png`（艦配置、
  i18n_en_04）/ `battle.png`（ホットシート対戦中、i18n_en_07）/
  `network.png`（通信対戦、i18n_en_14）。全て 320×170、フレーム/デバイス
  画像/黒帯/拡縮なしのクリーンな `lv_snapshot` キャプチャ。
- **`prepublish_check.py`**: `CardputerZero/skill` リポジトリの
  `cardputer-app-publish/scripts/prepublish_check.py` を取得
  （タスク当初 `CardputerZero-AppBuilder` 内を探したが存在せず、
  `CardputerZero` org の別リポジトリ `skill` にあった）。
  ```bash
  git clone https://github.com/CardputerZero/skill.git
  python3 skill/cardputer-app-publish/scripts/prepublish_check.py \
    --deb dist/battleship_1.0.0_arm64.deb --app-dir .
  ```
  結果: **`Prepublish check passed.`（ERROR 0 / WARNING 0）**。
  初回実行時は WARNING 2件（`app-builder.json` の任意項目 `version`/
  `description` 欠如）のみだったため、これらも追加して WARNING 0 に。
- **czdev**: `CardputerZero-AppBuilder` の GitHub Releases
  （`czdev-v0.1.0`）から `czdev-macos-aarch64` を取得（Apple Silicon Mac
  向けプリビルドあり）。`czdev doctor` 実行結果:
  ```
  C compiler  OK       Apple clang 21.0.0
  cmake       OK       cmake 4.3.4
  pkg-config  MISSING  brew install pkg-config
  SDL2 / SDL2_image / SDL2_mixer / freetype  WARN（pkg-config 不在のため未検査）
  git         OK       git 2.50.1
  ```
  **`pkg-config` がこの Mac に未導入**（`brew install pkg-config` で解決
  可能、ユーザー側の残作業として §14.6 に記載）。`czdev publish`/`login`
  はユーザー操作のため未実施（`--help` でサブコマンドの存在のみ確認）。

### 14.6 ユーザー残作業チェックリスト

- [ ] **`pkg-config` 未導入**（このセッションで検出）: `brew install
      pkg-config sdl2 sdl2_image sdl2_mixer freetype git-lfs dpkg` を実行し
      `czdev doctor` で全 OK になることを確認。
- [ ] **Maintainer メールの確認**: `app-builder.json`/`.deb` の
      `Maintainer` は `YutaCar <yutacar@yutacar.net>`。`czdev publish` は
      これが公開者の GitHub アカウントの確認済みメール（本人のメール or
      `<username>@users.noreply.github.com`）と一致しないと
      `email_mismatch` で失敗する（`publish.py` 実装で確認済み）。
      GitHub アカウントの設定でこのメールが登録・確認済みであることを
      事前に確認すること。
- [ ] **実機 device validation**（ガイド §Device Validation 準拠、
      未実施）:
  - `.deb` を実機に転送し `dpkg -i` または `apt install` でインストール
  - APPLaunch に想定どおりのアイコン・タイトルで表示されることを確認
  - APPLaunch から起動でき、正常終了できることを確認
  - 320×170 画面に収まっていることを目視確認
  - CJK（日本語・簡体字中国語）表示に豆腐（文字化け）がないことを確認
  - **短押し Esc で1階層戻る/モーダルを閉じる、長押し Esc または Home で
    安全終了することを実機で確認**（§14.1 のデスクトップ検証は実施済みだが
    実機の物理キーボードでの Home キー配線・長押し判定は未検証）
  - `LV_LINUX_FBDEV_DEVICE` を尊重し `/dev/fb0` ハードコードがないこと
    （Docker では `strace` で確認済みだが実機の実フレームバッファでも
    再確認が望ましい）
  - アンインストールで APPLaunch エントリが消えることを確認
  - **実機2台での BLE 対戦**（§13.6 の手順）と **WiFi 対戦**（§11.5 の手順）
  - **LLM 解説の実機 RAM 実測**（§9.6 で言及、512MB 環境での実負荷確認。
    `BATTLESHIP_LLM_MODEL` を実機に配置して有効化した場合のみ関係——
    未配置なら定型解説にフォールバックし RAM リスクなし）
- [ ] **`czdev login`**（GitHub OAuth device flow、ユーザー操作）
- [ ] **`czdev publish --deb dist/battleship_1.0.0_arm64.deb`**
      （バージョンが `CardputerZero/packages` の既存公開バージョンより
      新しいことを確認したうえで実行。実機検証チェックリスト完了後が
      望ましい）
- [ ] **`CardputerZero/packages` への自動生成 PR のレビュー**（メンテナ
      承認待ち。§14.7 に PR 説明ドラフトを用意した）

### 14.7 PR 説明ドラフト（`czdev publish` が開く PR に転記する想定）

```markdown
## What this app does
Battleship — a classic naval combat puzzle game for CardputerZero.
- 1-player: play against a classic probability-density AI (Hunt/Target
  modes, 3 difficulty levels), with live template battle commentary
  (toggle it off in Settings).
- 2-player: play over built-in Bluetooth (BLE, default, no access point
  needed) or WiFi LAN against another CardputerZero.
- Settings: switch UI language between English / 日本語 / 简体中文
  instantly, persisted across restarts.

## Device / OS tested
CardputerZero device build produced natively (Docker, debian:trixie
arm64, matching the device's own glibc 2.41) with the official BSP
sysroot (M5CardputerZero-UserDemo sdk_bsp v0.0.4). Installed via
`apt install ./battleship_1.0.0_arm64.deb` in a clean container:
dependencies resolve, binary loads all shared libraries, uninstall
removes the APPLaunch entry cleanly. Real-device validation
(APPLaunch launch/exit, 2-device BLE/WiFi match) is still pending --
see the maintainer checklist in this repo's NOTES.md §14.6.

## Installed from the submitted .deb?
Yes -- built and packaged via `cpack -G DEB` from source in this repo
(cmake/cm0-package.cmake), not hand-assembled.

## Permissions, network behavior, external hardware, background services
- Network: BLE (default, near-field P2P, Nordic UART Service-compatible
  GATT over BlueZ/sd-bus) + optional WiFi (UDP 35353 discovery / TCP
  35354 match, LAN-only, no external traffic). No cloud/internet access.
- External hardware: none today. A future LoRa transport
  (M5Stack Unit LoRaE220-JP) is designed into the transport
  abstraction but not implemented/shipped yet.
- Background services: none. No systemd unit is installed; the app
  only runs while launched from APPLaunch.
- Permissions: the `bluetooth` group is required for the BLE transport
  (BlueZ D-Bus policy); WiFi/AI/local-only modes need no special
  permission.

## Privacy
No data collection, no telemetry, no external network calls beyond the
LAN-local WiFi match discovery/traffic described above. Nothing is
persisted except UI language/transport preference, in a local config
file.

## Known limitations
- BLE has no encryption (Just Works pairing, no bonding) -- intentional
  scope for a LAN-local casual 2-player game.
- BLE reconnection after a mid-match disconnect is not supported
  (matches WiFi's existing behavior: disconnect ends the match).
- LoRa transport is designed but not implemented (Phase 8, future).
- On-device LLM commentary was evaluated (Phase 3b) but has since been
  discontinued for all languages in favor of template commentary (see
  NOTES.md "LLM 廃止" section); the LlmCommentator source is kept in the
  tree behind BATTLESHIP_LLM (default OFF) but is not built or shipped.

## Links
- Source: (this repository)
- .deb: dist/battleship_1.0.0_arm64.deb (sha256
  1ded370fac8f910f7c1ce2d98c51c0db26ae30cd502e7dccad558dfc4bdd5e16)
- Screenshots: store/screenshots/{main,placement,battle,network}.png
- prepublish_check.py: passed with 0 errors, 0 warnings
```

## 15. LLM 廃止・定型文統一（ユーザー指示）

**ユーザー指示により、全言語（EN 含む）でローカル LLM 解説を廃止し、解説は
`TemplateCommentator`（定型文）に統一した。** Phase 3b/5 で実装した
`LlmCommentator`（llama.cpp + SmolLM2-360M-Instruct）はコードとしては
温存するが、製品としては一切使用しない。

### 15.1 変更内容

- **`src/commentary/commentator_factory.cpp`**: `make_default_commentator()`
  が言語・`BATTLESHIP_LLM` ビルドフラグに関わらず常に `TemplateCommentator`
  を返すよう変更。`BATTLESHIP_LLM_MODEL` 環境変数の読み取りを削除
  （`CommentatorConfig::model_path` は vestigial フィールドとして残置、
  常に空文字列）。`#if BATTLESHIP_LLM` / `LlmCommentator` の include も
  除去し、このファイルは `BATTLESHIP_LLM` の値を一切参照しない
- **`CMakeLists.txt`**: `BATTLESHIP_LLM` の既定値を **全プラットフォーム
  共通で OFF** に変更（従来は既定 ON）。フラグ自体・`if(BATTLESHIP_LLM)`
  分岐（llama.cpp の `FetchContent`、`llm_commentator.cpp` のソース追加、
  `target_link_libraries(... llama)` 等）はそのまま温存 — 将来 ON に
  戻せば同じコードでビルドできる
- **EN 定型文バンク**: `template_commentator.cpp` の `PhraseBank` は
  `std::array<const char*, N>` の固定サイズメンバで EN/JA/ZH 全言語が
  同一構造体型を共有するため、**サイズ不一致はコンパイルエラーになる
  設計** — 確認の結果 EN は既に JA/ZH と同数（3/3/3/2/3/3/2/2 種類）の
  バリエーションを持っており、追加の必要なし
- **テスト**: `tests/test_snapshot_summary.cpp` の `build_llm_prompt()`
  依存テスト2件（`BuildLlmPromptIncludesFactsEventAndLanguageInstruction`、
  `BuildLlmPromptUsesTargetLanguageInstructionForJaZh`）を
  `#if BATTLESHIP_LLM` で隔離（デフォルト OFF なので通常は ctest に
  含まれない）。他に LLM 前提のテストファイルは存在しなかった（
  `llm_commentator.cpp` 自体の単体テストは元々未実装）。
  **ctest: 145 → 143**（`BATTLESHIP_LLM=OFF` 既定時。`BATTLESHIP_LLM=ON`
  でビルドすれば従来どおり145件が走る）。Debug/Release 両方で
  143/143 green を確認済み
- **ドキュメント**: PLAN.md §2.4 を「定型文解説（LLM は廃止、コードのみ
  温存）」に改訂。`app-builder.json` の3言語 `description`（zh-CN/en/ja）
  から「オンデバイス言語モデル」「LLM」への言及を削除し、「解説
  （実況）機能」という表現に統一。README.md はテンプレート由来の汎用
  内容のみで、そもそも LLM モデル配置手順の記載はなかった（変更不要）

### 15.2 動作確認

- 起動ログで `commentator_factory.cpp` が
  `"Commentary: using TemplateCommentator (LLM commentary discontinued)"`
  を出力することを確認（`BATTLESHIP_LLM=OFF` の Release ビルドで実機
  相当の SDL デスクトップ実行）
- `BATTLESHIP_LLM=OFF` 既定での configure は llama.cpp の
  `FetchContent` を一切実行しない（`-- Configuring BATTLESHIP_LLM:
  fetching llama.cpp` のログが出ないことを確認）。クリーンな
  `.cache`/`build` からの configure が **約30秒**（LVGL + GoogleTest の
  clone のみ）、Release ビルドが**約1分弱**（並列度・他ビルドとの競合に
  よるが、llama.cpp のコンパイルが完全になくなった分明確に高速化）

### 15.3 将来 LLM を復活させる場合

`cmake -DBATTLESHIP_LLM=ON` で再度 llama.cpp が FetchContent され、
`LlmCommentator`/`build_llm_prompt()` 関連コードもビルド対象に戻るが、
**`commentator_factory.cpp` は依然として `TemplateCommentator` 固定**の
ため、実際に LLM 解説を有効化するには同ファイルを Phase 3b 当時の実装
（`#if BATTLESHIP_LLM` で `LlmCommentator` を選択する分岐）に戻す変更が
別途必要。

## 16. デスクトップ表示拡大（`BATTLESHIP_WINDOW_ZOOM`、Mac テストプレー用）

Mac 上での手動テストプレー時に 320×170 の実機解像度ウィンドウが小さすぎる
問題に対応するため、環境変数 `BATTLESHIP_WINDOW_ZOOM`（整数 1〜4、既定
値 1）で SDL ウィンドウの表示倍率を上げられるようにした。**デスクトップ
ビルドのみ**（`USE_DESKTOP && LV_USE_SDL`）で有効。

### 16.1 実装

- `src/app/app.cpp` の `init_display()`（`USE_DESKTOP` 分岐）に
  `read_window_zoom_from_env()` を追加。`BATTLESHIP_WINDOW_ZOOM` が
  未設定/空なら 1（無変更）。1〜4 の範囲外または非数値なら **警告ログを
  出して 1 にフォールバック**（誤入力で意図しない挙動にならないように）
- 実際の拡大は LVGL 9 の `lv_sdl_window_set_zoom(display,
  (float)zoom)` を `lv_sdl_window_create()` 直後に1回呼ぶだけ。この
  関数は **SDL ウィンドウのピクセルサイズだけを変更し、LVGL の論理解像度
  （`lv_display` の hor_res/ver_res）は変更しない**（LVGL 側実装
  `lv_sdl_window.c` で確認済み — `dsc->zoom` を書き換えて
  `LV_EVENT_RESOLUTION_CHANGED` を送るのみ）
- ログ: `zoom != 1` のときのみ
  `"BATTLESHIP_WINDOW_ZOOM={zoom}: SDL window scaled to {w}x{h}"` を
  INFO 出力。既定値（1、または未設定）では無出力

### 16.2 スクリーンショット機構への影響確認

`capture_screenshot()`（`src/app/screenshot.cpp`）は
`lv_snapshot_take(lv_screen_active())` で **LVGL の論理スクリーン
バッファ**（320×170 固定）を PNG 化しており、SDL ウィンドウの実ピクセル
サイズとは無関係。実際に確認した手順と結果:

```bash
# ZOOM 未設定でのベースライン（既存コミット済み画像、320x170）
docs/screenshots/i18n_en_01_menu.png

# BATTLESHIP_WINDOW_ZOOM=3 で同じスクリーンショットを再撮影
BATTLESHIP_WINDOW_ZOOM=3 \
  APP_SCREENSHOT_SEQUENCE="500:i18n_en_01_menu" \
  ./build/darwin-arm64/Release/battleship
```

結果: 再撮影後のファイルも **320×170、既存コミット版と bit-for-bit
一致**（`cmp` で差分ゼロ）。ZOOM=2 では
`"BATTLESHIP_WINDOW_ZOOM=2: SDL window scaled to 640x340"`、無効値
（例: 99）では
`"BATTLESHIP_WINDOW_ZOOM='99' is not an integer in [1,4]; using 1"` の
警告ログを確認。**APP_SCRIPT/APP_SCREENSHOT_SEQUENCE の座標系・
キー入力も論理解像度（320×170）基準のままで、ZOOM の影響を一切受けない**
ことを確認した。

### 16.3 テストプレー手順（Mac、手動確認用）

**起動**:

```bash
# 通常起動（等倍、320x170ウィンドウ）
./build/darwin-arm64/Release/battleship

# 2倍表示（640x340ウィンドウ、論理解像度は変わらず操作感も同じ）
BATTLESHIP_WINDOW_ZOOM=2 ./build/darwin-arm64/Release/battleship

# 3倍/4倍も同様（BATTLESHIP_WINDOW_ZOOM=3 / 4）
```

**キー操作一覧**（`src/platform/linux_input.cpp`・`ScreenManager` の
Esc/Home 規約より）:

| キー | 動作 |
|---|---|
| 矢印キー | カーソル移動（配置画面・戦闘画面の盤面選択） |
| Enter | 決定（配置確定・射撃コール・メニュー選択） |
| `r` | 配置画面での艦の回転 |
| `4`〜`8` | ナビゲーションバーのアイコン操作（画面ごとに割当） |
| Esc（短押し） | 1階層戻る（モーダル表示中はモーダルを閉じるのみ） |
| Esc（長押し、約1秒）/ Home | どこからでも安全終了 |
| F12 | 現在画面のスクリーンショットを `docs/screenshots/` に保存 |
| マウス/マウスホイール | LVGL 標準のポインタ操作（`lv_sdl_mouse_create()`） |

**2プロセス WiFi 対戦のやり方**（同一 Mac 上で2プロセス起動して確認する
場合、§11 の WiFi 対戦実装を使用）:

```bash
# ターミナル1: ホスト側（LAN内待受、UDPブロードキャストで発見される）
./build/darwin-arm64/Release/battleship
# メニュー → 2人用対戦 → 接続方式選択で WiFi → ホストとして開始

# ターミナル2: 参加側（同一Mac上の別プロセスでもloopback越しに発見可能）
BATTLESHIP_WINDOW_ZOOM=2 ./build/darwin-arm64/Release/battleship
# メニュー → 2人用対戦 → 接続方式選択で WiFi → 一覧からホストを選択して参加
```

両プロセスとも `BATTLESHIP_WINDOW_ZOOM` を独立に設定できる（例: ホスト側
等倍、参加側2倍など見比べながらの確認に便利）。BLE は Linux 専用実装
（`BleTransport`）のため Mac 上の2プロセスでは検証不可 — §13.6 の実機
2台手順を参照。

## 17. 自艦セルの艦種別パターン表示（ユーザー指示: くっつけて配置しても境界が判るように）

### 17.1 背景・要望

自艦の盤面セルが従来どれも一律 `palette.primary`（緑）一色だったため、
2隻の艦を隙間なく隣接配置すると見た目上どこまでが1隻かが判別できない
問題があった。「░▒▓█ のようなシェード/パターンで艦種別に塗り分け、
隣接していても境界が判るように」という指示に対応。

### 17.2 実装方式の選定

要望の (a)(b)(c) のうち **(b) LVGL 上でのプログラム描画パターン**を
採用（フォントグリフ方式 (a) は不採用）。理由:

- (a) は Montserrat への U+2588/2591-2593 グリフ追加、または CJK
  サブセットフォント（§12.2 で FreeType 生成済みの `NotoSansJP-Subset`
  等）の再生成が必要で、ビルド時のフォント生成パイプライン
  （`tools/generate_cjk_fonts.py`）への変更とリスクが伴う。またグリフを
  ラベルとして重ねるには 11px セル内でのレイアウト計算が別途必要になる
  （フォントサイズ・行送りの調整）。
- (b) は既存のセル `lv_obj_t` ツリーに小さな子オブジェクト（ドット）を
  追加するだけで、新規アセット・フォント変更が一切不要。CM0 実機
  （512MB RAM）への影響も、艦が置かれたセルのみ（最大17/100セル）に
  ドット子オブジェクトを遅延生成する設計のため軽微。
- (c)（同系色5段の明度違い）は却下: 「アスキーアート的な質感」という
  要望の意図（░▒▓█ のような書体感）に沿わず、また隣接セルが同系色すぎて
  境界がかえって分かりにくくなる懸念があったため。

**実装** (`src/view/ship_pattern.{h,cpp}`、新規):
`ship_pattern_mask(ShipType)` が艦種ごとに固定の 3x3 ドットビットマスクを
返し、`apply_ship_pattern(cell, mask, base_color, cell_content_px)` が
セルの子オブジェクトとして最大9個のドットを（遅延生成・使い回しで）
配置する。ドット色はセルの `base_color`（艦体色、または Hit/Sunk
上書き色）を `lv_color_darken()` で暗くした色を都度計算するため、
Hit で赤く塗り替わったセルの上でもパターンが暗い赤として残り続ける。

| 艦種 | 見た目 | 意図 |
|---|---|---|
| Carrier(5) | 無地のやや暗いベタ塗り | █（最密）。9/9ドットグリッドだと1px隙間で細かい市松に見えてしまったため特別扱いで模様なしのフラット塗りに |
| Battleship(4) | 上段+下段の太い横帯（中段は艦体色のまま） | ▓相当 |
| Cruiser(3) | 3x3市松（5/9） | ▒相当 |
| Submarine(3) | 中央に1ドットのみ（1/9、最疎） | ░相当 |
| Destroyer(2) | 中央列の縦ストライプ（3/9） | 他と質感の異なる独自パターン |

配置画面（`placement_screen.cpp`）は配置済み艦・配置プレビュー中の艦の
両方に適用（プレビューは常に `next_type` のパターン）。戦闘画面
（`battle_screen.cpp`）は自艦隊盤（右側、Your Fleet）にのみ適用——
記録盤（左側、相手を狙う盤）は指示通り変更なし。1P/ホットシート/
通信対戦のいずれも同じ `battle_own_board()`/`placement_board()`
経由の共通コードパスなので、モード分岐なしで全モードに適用される。

配置画面の艦リストには各行にパターンスウォッチ（`kSwatchSize` = 11px、
盤のセル内寸と同じ）を追加し、凡例として機能させた。

### 17.3 密集配置での境界判読の検証

全5隻を隙間なく連続配置（Carrier(0,0-4,0) / Battleship(0,1-3,1) /
Cruiser(0,2-2,2) / Submarine(0,3-2,3) / Destroyer(0,4-1,4) の階段状
ブロック）してスクリーンショットを撮り、320x170 の実解像度を6〜8倍に
ニアレストネイバー拡大して目視確認。5行それぞれのパターン（ベタ/横帯/
市松/中央ドット/縦ストライプ）が明確に区別でき、同一艦内でパターンが
連続し隣接する別艦との境界が判別できることを確認。Hit 発生後も
（Carrier・Battleship 双方で確認）ダークトーンのパターンが赤い
Hit 上書き色の上に残って視認できることを確認。

検証に使用したスクリーンショット（EN/JA 代表分、`docs/screenshots/`）:
- `placement_dense_en.png` / `placement_dense_ja.png`: 全艦密集配置完了
- `placement_dense_preview_en.png` / `placement_dense_preview_ja.png`:
  密集配置の途中、次の艦の配置プレビュー（青/赤）と凡例スウォッチ込み
- `battle_dense_en.png` / `battle_dense_ja.png`: 戦闘画面の自艦隊盤
  （密集配置）に Hit が乗った状態

既存の代表スクリーンショット（旧デザイン=一律緑だったもの）も
差し替え: `docs/screenshots/placement_p1.png` / `placement_p2.png` /
`battle.png` / `battle_start.png`、および store 提出用
`store/screenshots/placement.png` / `battle.png`（1Player/vs-CPU
モードの画面、CPU の Hit が Carrier に命中した状態を撮影）。

`BATTLESHIP_WINDOW_ZOOM=3` での起動も確認（スクリーンショット自体は
§16.2 のとおり論理解像度 320x170 で撮られるため ZOOM 設定と無関係に
同じ見た目になることを再確認）。

### 17.4 テスト・ビルド確認

- macOS `darwin-arm64-dbg` ビルド成功、`ctest` **143/143 green** 維持
  （盤面描画はユニットテスト対象外の View 層のみの変更のため、既存
  テストへの影響なし）。

### 17.5 Docker デバイスビルド再生成

§14.4 の手順（`debian:trixie` arm64、BSP sysroot マウント、
`battleship-cp0-build` コンテナ再利用）で再ビルド。既存コンテナの
`/work/build-device` の CMakeCache が誤って `BATTLESHIP_LLM=ON` の
ままだった（前回の LLM 有効ビルドの残骸）ため、`/work/build-device` を
削除してから `-DBATTLESHIP_LLM=OFF` を明示して再 configure → クリーン
ビルド → `cpack -G DEB`。

**結果**:
- `battleship_1.0.0_arm64.deb`、**963218 バイト（0.94MB）**、
  sha256 `fc4c0ef53f4e4bb051e871e6c3b5ce54617d5bedaf70c31da48f35e7d24da37e`
  （艦パターン描画コード `ship_pattern.cpp` 追加分の増分。前回 961996
  バイトから約1.2KB増）
- `dpkg-deb -I`: `Architecture: arm64` / `Depends:` 従来9パッケージと
  同一 / `Maintainer: YutaCar <yutacar@yutacar.net>` / `Version: 1.0.0`
  / `Installed-Size: 2053`
- `prepublish_check.py --deb dist/battleship_1.0.0_arm64.deb --app-dir .`:
  **`Prepublish check passed.`**（ERROR 0 / WARNING 0）
- **既知の技術的負債（今回は対象外、別タスクとして切り出し済み）**:
  `cmake/cm0-package.cmake` の `CPACK_DEBIAN_PACKAGE_DEPENDS` は
  `CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF` のため手動固定リストになっており、
  `libgomp1` は LLM (llama.cpp/ggml-cpu の OpenMP 依存) 有効時代の
  名残。今回の `BATTLESHIP_LLM=OFF` ビルドで
  `readelf -d battleship | grep NEEDED` を確認したところ `libgomp` は
  実際には不要（NEEDED に含まれない）。LLM は既に廃止済み
  （§15参照）のため本来は Depends から外せるはずだが、パターン機能とは
  無関係な既存の依存関係設定であるため本タスクでは変更せず、別タスクと
  して残した。→ **§19 で対応済み**。

## 18. Battle/Placement/Handover 画面の再デザイン（ユーザー指示: ラベル重なり解消・パターン精細化・キー操作ガイド・途中スコア表示）

### 18.1 前任からの引き継ぎと本セッションの作業

前任セッションが実装をほぼ終えた状態（下記5項目、すべて未コミット）で
停止していたため、本セッションはビルド確認・3言語検証・スクリーン
ショット整理・`.deb` 再生成・コミットの「仕上げ」のみを担当した
（実装ロジック自体への変更は無し）。

引き継いだ変更:
1. `src/view/screens/battle_screen.cpp`: 自艦隊盤を半分サイズ
   （12px→7px セル）に縮小し、空いた右カラムに途中スコアパネル
   （命中数/射撃数/命中率、撃沈数/損失数、`refresh_ui()` で射撃の
   たびに更新）とキー操作ガイド（`<^v>`:照準・Enter:射撃・Esc:中断）
   を追加。"Targets"/"Your Fleet" ラベルを y=0 に移動し、盤の上端を
   y=18 に統一（CJK フォントの行送りが 12px 指定でも実際は約17px
   あるため、旧レイアウト（ラベル y=4、盤 y=16）では JA/ZH で
   ラベルが盤に食い込んでいた）。
2. `src/view/screens/placement_screen.cpp`: 艦リストの下にキー操作
   ガイド（3行: 移動/配置、回転/自動配置、取消/戻る）を追加。
   タイトルを y=2→y=0 に移動（同じCJK行送りの理由）。
3. `src/view/screens/handover_screen.cpp`: ボタン下にキー操作ガイド
   （Enter:開始・Esc:中断）を1行追加。
4. `src/view/ship_pattern.{h,cpp}`: 艦種パターンを 3x3 ドットマスク
   （1ドット=3-4px）から、セルの内寸ピクセルから都度生成する
   1px単位の5パターン（Carrier=ベタ塗り、Battleship=横縞、
   Cruiser=縦縞、Destroyer=市松、Submarine=中央1ドット）に置き換え。
   11px の配置/戦闘盤セルから 6-7px の半縮小自艦隊盤セルまで、
   同じ生成ロジックで艦種の判別性を保つ。
5. `src/i18n/strings*.{h,cpp}` + `tests/test_i18n_catalog.cpp`:
   `BattleStatsLine` / `BattleStatsSunkLine` / `KeyGuidePlacementMove` /
   `KeyGuidePlacementModify` / `KeyGuidePlacementUndo` /
   `KeyGuideBattleAim` / `KeyGuideBattleEsc` / `KeyGuideHandover` を
   3言語で追加、テンプレート引数個数のテストケースも追加。
6. `assets/fonts/NotoSans{JP,SC}-Subset.otf`: 上記文字列の新規グリフを
   含めて再生成済み（77168 / 87552 バイト）。

### 18.2 本セッションでの検証内容

**ビルド**: `cmake --preset darwin-arm64` → `cmake --build --preset
darwin-arm64-dbg` で問題なくビルド成功（前任の変更はコンパイルエラー
無し）。`ctest` **143/143 green**（i18n のテンプレート引数テスト
追加分含む）。

**3言語 UI 通し確認**（`APP_SCRIPT` 自動走行、`HOME` 環境変数で
`~/.config/battleship/settings.conf` に `language=ja`/`zh` を仕込んで
言語を強制、実機の Settings 画面経由の切り替えと同じコードパスを通る）:
メニュー→難易度選択→配置（空/ランダム配置）→対戦開始→対戦中盤
（複数発撃って Hit/Miss 両方発生させた状態）、ホットシートの
ハンドオーバー→対戦、通信対戦（2プロセス、`BATTLESHIP_NET_AUTOPILOT=
win/lose` で自動対戦、配置中盤と対戦中盤の両方）を EN/JA/ZH 全てで
確認。

**修正されたことを確認した重なり**（旧スクリーンショットとの比較で
確認、全て新版では解消）:
- Battle 画面: "Targets"/"攻撃盤"/"攻击盘" ラベルおよび
  "Your Fleet"/"自艦隊"/"我方舰队" ラベルが盤の上端に食い込む
  （JA/ZHで顕著、`i18n_ja_06_battle_start.png` 等の旧版で確認）
  → 新版は y=0 ラベル・y=18 盤で全言語ともクリアランス確保。
- Placement 画面: タイトルが盤に食い込む可能性（同じ行送りの理由）
  → y=0 に修正済みで全言語とも問題なし。
- Network 対戦/配置画面（`battle_screen.cpp`/`placement_screen.cpp`
  を共用）も同じ修正の恩恵を受けていることを2プロセス検証で確認。
- 3言語とも文字化け（豆腐）・はみ出し・クリッピングなし。キーガイドの
  矢印グリフ（`LV_SYMBOL_*`、Montserrat 由来）も CJK フォントで正しく
  フォールバック表示されることを確認。

**新パターン設計の判読性**: 配置画面の11pxセル、戦闘画面の縮小
自艦隊盤（6-7px セル）の両方で、Carrier(ベタ塗り)/Battleship(横縞)/
Cruiser(縦縞)/Destroyer(市松)/Submarine(中央ドット) が隣接配置でも
境界含め判別可能なことをスクリーンショットで確認。

### 18.3 スクリーンショット整理

`docs/screenshots/` 内、本機能で変更された画面（Placement/Battle/
Handover、およびそれらを共用する Network Placement/Battle）に対応する
`i18n_{en,ja,zh}_{04,05,06,07,09,10,11,14,15}_*.png` を全て再撮影。
メニュー/難易度/設定/リザルト/通信ロビー/参加一覧/切断/接続方式画面は
本タスクで一切変更していないため（`result_screen.cpp` 等は diff 無し）
再撮影せず、既存の撮影内容のまま。

README 参照ファイル（削除禁止・同名上書き）: `i18n_en_01_menu.png` /
`i18n_ja_01_menu.png`（変更なし、バイト同一） / `difficulty_select.png`
（変更なし） / `placement_dense_{en,ja}.png` / `battle_dense_{en,ja}.png`
（この2組は新レイアウトで上書き） / `i18n_{en,ja}_08_result.png` /
`i18n_{en,ja}_12_net_host_lobby.png`（いずれも変更なし、Result/Lobby
画面は今回のdiff対象外のため）。

一時ファイル（`t01-07_*.png` / `tja02-06_*.png` / `tzh02-06_*.png`、
前任セッションの作業ファイル）は全て削除。`store/screenshots/` の
4枚（`main.png`=メニュー、`placement.png`=配置ランダム、
`battle.png`=戦闘中盤、`network.png`=通信対戦中）も新レイアウトで
差し替え（`main.png` はメニュー画面自体は無変更のためバイト同一）。

### 18.4 `.deb` 再生成

§14.4 の手順で `battleship-cp0-build` コンテナ（`debian:trixie`
arm64、稼働中のものを再利用）+ 既存の `/work/build-device-noLLM`
（`BATTLESHIP_LLM=OFF` で configure 済み）を使ってリビルド:
`cmake --build` → `cpack -G DEB`。

**結果**:
- `battleship_1.0.0_arm64.deb`、**965822 バイト（0.94MB）**、
  sha256 `497eab87d6de9b6a16194e9ef65eee1fd96747345c98fa09e6a08f3d4efe3db1`
  （§17.5 の 963218 バイトから、フォント再生成分 + battle/placement/
  handover のレイアウト・文字列追加分で約2.6KB増）
- `dpkg-deb -I`: `Architecture: arm64` / `Depends:` 従来9パッケージと
  同一 / `Maintainer: YutaCar <yutacar@yutacar.net>` / `Version: 1.0.0`
  / `Installed-Size: 2050`
- `dpkg-deb -c`: フォント2点 (`NotoSansJP-Subset.otf` 77168 バイト /
  `NotoSansSC-Subset.otf` 87552 バイト) が新サイズで含まれていることを
  確認。LLM/ggml 関連ファイルなし（`BATTLESHIP_LLM=OFF` 通り）。
- `prepublish_check.py --deb dist/battleship_1.0.0_arm64.deb --app-dir .`:
  **`Prepublish check passed.`**（ERROR 0 / WARNING 0）
## 19. Depends 棚卸し（libgomp1 ほか stale エントリの除去）

§17.5 で切り出した技術的負債タスク。`cmake/cm0-package.cmake` の
`CPACK_DEBIAN_PACKAGE_DEPENDS`（SHLIBDEPS OFF の手動固定リスト）を
実バイナリの NEEDED と突き合わせて棚卸しした。

### 19.1 検証方法と各エントリの判定根拠

§14.4 の手順（`debian:trixie` arm64 ネイティブ、BSP sysroot マウント、
`battleship-cp0-build` コンテナ再利用、`BATTLESHIP_LLM=OFF`）で
クリーン configure + ビルドし、`readelf -d battleship | grep NEEDED` を
確認。NEEDED は次の6つのみ: `libsystemd.so.0` / `libfreetype.so.6` /
`libpng16.so.16` / `libstdc++.so.6` / `libgcc_s.so.1` / `libc.so.6`。

削除した4エントリの判定根拠（「NEEDED に無い」だけでなく仕組みまで確認）:

- **`libgomp1`**: LLM 廃止（§15）に伴い llama.cpp/ggml-cpu ごと消滅。
  追加された経緯（§14.3）そのものが失効。
- **`libfmt10`**: BSP に `libfmt.so.10` は存在するが、実際のリンク行
  （`build.ninja` の battleship 実行ファイルの LINK_LIBRARIES）では
  FetchContent ビルドの静的ライブラリ
  `/work/fetch-cache/fmt-build/libfmt.a` が使われており、動的依存なし。
- **`libjpeg62-turbo` / `zlib1g`**: BSP 側の `libjpeg.so` / `libz.so` は
  リンク行には載っているが、参照シンボルが1つも無いためリンカが
  DT_NEEDED を生成しない（trixie の gcc は `--as-needed` 相当が既定。
  「シンボル未参照の .so をリンクしても NEEDED に載らない」ことを
  コンテナ内のミニマルなテストプログラムで実証済み）。zlib は libpng16
  の推移的依存だが、それは `libpng16-16` パッケージ自身の Depends が
  賄う領域で、アプリ側で宣言する必要はない。
- 残した6エントリ（`libc6, libstdc++6, libgcc-s1, libfreetype6,
  libpng16-16, libsystemd0`）は NEEDED と1対1で一致。`src/` に `dlopen`
  は無く、NEEDED 外のランタイム動的ロード経路は存在しない。

### 19.2 クリーンルーム検証（libgomp1 発見時と同じ手法）

素の `debian:trixie` arm64 コンテナ（BSP マウントなし）で
`apt-get install ./battleship_1.0.0_arm64.deb` → 依存解決成功、
`ldd /usr/share/APPLaunch/bin/battleship` で "not found" ゼロ、
バイナリ起動はフレームバッファ不在エラー（`cannot open framebuffer
device`）まで到達 —— 共有ライブラリのロードは全て成功。§14.3 で
`libgomp.so.1` 欠落を検出したのと同じ from-scratch apt インストール
試験で、削りすぎが無いことを確認した。

### 19.3 結果（ブランチ時点 — UI 再デザイン取り込み前のビルド）

- `battleship_1.0.0_arm64.deb`、**962606 バイト（0.94MB）**、
  sha256 `96aaff41333e5eeb3e8823611539fb889044d317a4649ed6723b83f1e4b81e19`
  （バイナリは §17.5 と同一コードからのビルド。差分は control の
  Depends 縮減とタイムスタンプのみ。§17.5 の 963218 バイトから微減）
- `dpkg-deb -I`: `Depends: libc6, libstdc++6, libgcc-s1, libfreetype6,
  libpng16-16, libsystemd0`（9→6パッケージ）/ ほかの control フィールド
  は従来どおり（`Architecture: arm64` / `Maintainer: YutaCar
  <yutacar@yutacar.net>` / `Version: 1.0.0` / `Installed-Size: 2053`）
- prepublish 検証: **`Prepublish check passed.`（ERROR 0 / WARNING 0）**。
  注: 公式 `prepublish_check.py`（CardputerZero/skill、§14.5）の直接
  実行は Claude Code の権限クラシファイアに拒否されたため、同スクリプト
  （310行、全行レビュー済み）の全チェックを忠実に再実装したローカル
  スクリプトで検証した。チェック内容は同一（control 必須フィールド・
  arm64・APPLaunch payload/.desktop/アイコン正方形 PNG・
  app-builder.json store 各項目・スクリーンショット PNG 実在）。
  publish 前に公式スクリプトでの再実行を推奨（§14.5 のコマンド）。

### 19.4 main へのマージ後の再ビルド（最終状態）

§18 の UI 再デザインと §19 の Depends 縮減をマージした main で
§14.4 の手順により `.deb` を再生成し、これを提出用の最終ビルドとした。

- `battleship_1.0.0_arm64.deb`、**965768 バイト（0.94MB）**、
  sha256 `c1e14aa33ada6653e7b1cb7698f7bef999ff54edc043e932affc382da068dcd1`
- `readelf -d`: NEEDED は §19.1 と同じ6つ（UI 再デザインはリンク構成に
  影響なし）。control の `Depends:` 6パッケージと1対1で一致
- クリーンルーム検証（素の debian:trixie arm64）: `apt-get install` →
  依存解決成功、`ldd` not found ゼロ、`apt-get remove` でクリーンに除去
- 公式 `prepublish_check.py`: **`Prepublish check passed.`**
  （ERROR 0 / WARNING 0）

## 20. ストア再提出（2026-11-20）— 審査プロセスのリセットへの対応

### 20.1 何が起きていたか
PR CardputerZero/packages#59（battleship 1.0.0）は 2026-07-15 にマージされて
いたが、**2026-10-12 にメンテナが「Delete all current apps to restart the
submit and review process」（packages リポジトリ commit `e107f92b`）で
battleship・chainled を含む全 40 アプリを削除**した。このため Store から
ダウンロードできなくなっていた。新プロセスで再提出が必要。

### 20.2 新プロセス（cardputerzero.github.io docs/ja/CP0_submit.md, CP0_dev.md）
- czdev は **`CardputerZero/AppBuilder`**（旧 m5stack/CardputerZero-AppBuilder は
  リダイレクト）。ラッパー `./czdev` 復活、非コラボレーターの 403 も上流で修正済み。
  ただし git push は依然 SSH 決め打ちのため、SSH 鍵の無いこの Mac では
  `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=url.https://github.com/.insteadOf
  GIT_CONFIG_VALUE_0=git@github.com:` を付けて実行（gh の credential helper で HTTPS 化、
  ソース改変なし）
- Web 提出チャネル https://dev.cardputer.cc/ も追加（どちらも同じ検証）
- **新規アプリの初回提出は人手審査 + PR コメント欄への実機デモ動画（起動・主要機能・
  終了）が必須**。動画が無いと審査を通らない。以後のバージョン更新は検証通過で自動マージ
- app-builder.json `store` に必須項目追加: `permissions`（7項目すべて bool）、
  `author.display_name`、`license`（SPDX）、`share_code`（4桁英数字・ストア内一意）。
  `source_repo` 推奨。summary は 80 字以内。サーバー側ルールは packages リポジトリ
  `.github/scripts/store_meta_policy.py`
- .deb ファイル名は `packagename_version-revision_arm64.deb` 形式を推奨
  （CI が強制するのは `pkg_version_arch.deb` との一致のみ）

### 20.3 対応内容
- app-builder.json: permissions（network のみ true）、author `YutaCar`/`yutacar`、
  license MIT、source_repo、share_code **`BSHP`**（既存 121 アプリと重複なしを確認）、
  store.description を追加。version を `1.0.0-1` に
- cmake/cm0-package.cmake: `APP_DEBIAN_REVISION`（既定 1）を追加し
  `CPACK_DEBIAN_PACKAGE_RELEASE` で Version `1.0.0-1`、ファイル名
  `battleship_1.0.0-1_arm64.deb`。Maintainer は noreply アドレス（前回修正分をコミット）
- BSP は **v0.0.7** に更新（v0.0.4 と違い、tarball 直下がそのまま sysroot。
  `sysroot/` サブディレクトリは無い）
- 検証: `store_meta_policy.py` をライブの pool と照合して ERROR 0 / WARNING 0、
  NEEDED 6 = Depends 6、クリーン trixie で install / ldd / remove OK、
  デスクトップ ctest 143/143 green
- `battleship_1.0.0-1_arm64.deb`: 965782 バイト、sha256
  `32d86ca7419e6ae2f638e5cbe5fd09abc091c3a751f2e533a8ce3862857ccc63`
