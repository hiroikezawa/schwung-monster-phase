# MONSTER PHASE

Ableton Move 用 Schwung のオーディオエフェクト。Roland **AIRA Modular BITRAZER**（ビットクラッシャー
ペダル）用に AIRA Modular Customizer で作られたパッチ `BITRAZER_MONSTER_PHASE` を、保存された
パッチファイルから再現する。

パッチは BITRAZER 本体の周りに 6 つのサブモジュール（SHORT DELAY ×2、FORMANT FILTER、TUBE CLIP、
3 BAND EQ、COMPRESSOR）をケーブルでつないだもの。このモジュールはそれを直列エフェクトとして
書き直すのではなく、**パッチファイルのモジュール種別・生パラメータと配線表からグラフを組み立てて**
動かす（分岐・加算・フィードバックにも対応）。

> **再現の範囲について**: モジュールの種類とパラメータの生値はファイルから確定している。
> 一方、**配線はスクリーンショットから目で追跡したもの**（ファイルの配線領域は未解読）で、
> Roland の各モジュールの DSP と換算曲線は公開されていないため、**音は近似**である。
> 何が確定で何が推定・暫定かは [docs/ANALYSIS.md](docs/ANALYSIS.md) に全部書いてある。
> 原機との聴き比べはしていない。

## 信号の流れ（追跡した配線）

```
L: IN1 → SHORT DELAY 1 → TUBE CLIP 1 → BITRAZER 1 → OUT1
R: IN2 → SHORT DELAY 2 → FORMANT → TUBE CLIP 2 → BITRAZER 2 → COMPRESSOR → 3 BAND EQ → OUT2
```

左右は別々の経路で、2 つの SHORT DELAY は TIME が 3 % ずれている（77.6 ms と 80 ms、推定）。

## ノブ

| ノブ | 名前 | 中身 |
|---|---|---|
| 1 | MIX | 全体のドライ/ウェット（Schwung 側で追加。ペダルにはない）。既定 100 % |
| 2 | G1 SR | ペダルの GRF1 ノブ。パッチの配線で BITRAZER の SAMPLE RATE へ |
| 3 | G2 CUT | GRF2 ノブ → BITRAZER の FILTER CUTOFF |
| 4 | G3 FMT | GRF3 ノブ → FORMANT FILTER の FORMANT 1（a→i→u→e→o） |
| 5 | G4 LOW | GRF4 ノブ → 3 BAND EQ の LOW（右チャンネルの低域ブースト） |
| 6 | G5 TYP | GRF5 ボタン → BITRAZER の LPF/HPF 切り替え |
| 7 | G6 BYP | GRF6 ボタン → BITRAZER の BYPASS（ディレイ等のサブモジュールは動いたまま） |
| 8 | TIME | 2 つの SHORT DELAY の TIME を一緒に動かす（差は保持）。GRF ではない追加ノブ |

ノブ 2〜7 は「GRF がどこにつながっているか」をパッチの配線どおりに再現したもので、ノブ自体に
機能を割り当てたのではない。GRF のケーブル先が原機の既定（GRF3 = BIT DOWN、GRF4 = RESONANCE）と
違うのは、このパッチでそう配線されているため。GRF1〜4 の既定位置はパッチに入っていないので
ここで決めた値（SR 70 %、CUTOFF 90 %、他 0）。

## 必要なもの

- Schwung を入れた Ableton Move
- トラックスロットと Master FX のどちらでも使える（標準のノブグリッド）

## インストール

```bash
python3 scripts/install.py http://<MoveのIP>:7700
```

インストール後は Move を再起動する。

## ビルドとテスト

```bash
bash scripts/build.sh arm64     # dist/monster-phase-module.tar.gz
bash scripts/test.sh            # 全テスト
```

`scripts/test.sh` が実行するもの:

- `tests/test_patch.py` — パッチ解析ツール（ヘッダー、6 モジュールの ID と生値、差分、配線表の検証）
- `tests/test_engine.c` — 換算、配線グラフ（左右の分離、加算、分岐、閉路）、ディレイの遅延量・
  フィードバック・コム、フィルターの安定性、TUBE CLIP の倍音とオーバーサンプリング、
  コンプのアタック/リリース、BITRAZER、ノブを毎ブロック乱暴に動かした安定性、ブロック長に依らない
  出力、48 kHz、CPU、プラグイン API。AddressSanitizer + UBSan でも実行
- `tests/test_grid.mjs` — Schwung の本物のページエンジンでノブ 8 個の表示と書き込み
- arm64 ビルドの ABI 検査、ライセンス検査

Move 上の CPU 計測: `build/bench-device-arm64` を Move にコピーして `taskset 0x7` で実行する。

## パッチ解析ツール

```bash
python3 tools/aira_patch.py dump patch/BITRAZER_MONSTER_PHASE.bin
python3 tools/aira_patch.py diff A.bin B.bin
python3 tools/aira_patch.py routing patch/routing_screenshot.json patch/BITRAZER_MONSTER_PHASE.bin
```

配線領域（`0x80–0x47F`）は未解読。Customizer でケーブルを **1 本だけ** 変えたパッチを保存して
`diff` にかければ、そのケーブルが入っているバイトが分かる。手順は docs/ANALYSIS.md §5。

## ファイル

| パス | 内容 |
|---|---|
| `patch/BITRAZER_MONSTER_PHASE.bin` | 元のパッチ（ビルド時に SHA-256 を検査して C ヘッダーへ） |
| `patch/routing_screenshot.json` | スクリーンショットから追跡した配線（暫定、各ケーブルに根拠と確度） |
| `reference/` | 提供されたスクリーンショットと仕様書（パッケージには入らない） |
| `tools/aira_patch.py` | パッチ解析・差分・配線検証・ヘッダー生成 |
| `dsp/mp_convert.*` | 生値 → 物理単位の換算（版数付き、各曲線に確定/推定/暫定の区分） |
| `dsp/mp_dsp.*` | 7 つの処理ブロック |
| `dsp/mp_engine.*` | データ駆動の配線グラフ |
| `dsp/mp_plugin.c` | Schwung のプラグイン API |
| `docs/ANALYSIS.md` | 解析記録 |

## ライセンス

MIT（[LICENSE](LICENSE)）。Copyright (c) 2026 Hiro Ikezawa。第三者のコードについては
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)。Roland、AIRA、BITRAZER は Roland の商標で、
このモジュールは Roland とは関係のない独自の再現である。

## 変更履歴

- 0.1.0 — 最初の版
