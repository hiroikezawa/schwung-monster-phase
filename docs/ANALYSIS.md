# BITRAZER_MONSTER_PHASE 解析記録

> **0.2.0 以降、モジュールは元パッチの再現をやめ、独自の設計になった**（README 参照）。
> この文書は 0.1.0 で再現を試みたときの解析記録として残している。DSP 節（§6、§7）の数値は 0.1.0 のもの。

この文書は、`patch/BITRAZER_MONSTER_PHASE.bin`（AIRA Modular Customizer で保存されたパッチ）と
`reference/MONSTER_PHASE_reference.png`（Customizer のスクリーンショット）から何が分かり、何が分からないかを
区別して記録する。実装の数値はすべてここに出典区分を付けてある。

区分の意味:

| 区分 | 意味 |
|---|---|
| **確定** | ファイルのバイト、または公開資料で直接裏付けられる |
| **推定** | スクリーンショット等の状況証拠から導いた。反証はないが証明もない |
| **暫定** | 根拠がなく、実装のためにここで選んだ値。原機の値ではない |

参照資料:

- パッチファイル: 1212 バイト、SHA-256 `f27c95ff684e9e155f7c71ea75f53fe5d62701623b0ee663f847b30584d23843`
- 非公式 MIDI 実装 v1.2（Mugenkidou）: https://github.com/mugenkidou/AIRA_Modular_Effects
  — モジュール種別 ID、パラメータ順、パッチ端子 ID（`10 20 ss dd`）
- Roland 製品ページ（GRF = GRIFFER。GRF1–4 はノブ、GRF5/6 はボタン。既定割り当ては
  GRF1 SAMPLE RATE、GRF2 FILTER CUTOFF、GRF3 BIT DOWN、GRF4 RESONANCE、GRF5 LPF/HPF、GRF6 BYPASS。
  GRF の設定値は CV/GATE 入力に加算される）: https://www.roland.com/de/products/bitrazer/

## 1. ファイル構造

| オフセット | 内容 | 区分 |
|---|---|---|
| `0x00–0x0F` | `HabPatchFile0001` | 確定 |
| `0x10–0x27` | 名前 24 バイト、空白詰め（`AiraEfx Patch`） | 確定 |
| `0x28–0x37` | メインモジュール（BITRAZER）16 バイト `01 00 00 00 00 32 00 64 64 32 64 00 00 00 00 00` | 中身の対応は推定（§3） |
| `0x38–0x67` | サブモジュール 6 個 × 8 バイト `[種別, P1, P2, P3, P4, 0, 0, 0]` | 確定（種別バイトが公開 ID と一致し、並びがスクリーンショットと一致） |
| `0x68–0x7F` | すべて 0 | 不明 |
| `0x80–0x47F` | 1024 バイト、うち 18 バイトが非 0 | **未解読**（§5） |
| `0x480–0x4BB` | すべて 0 | 不明 |

`tools/aira_patch.py dump` がこの表のとおりに読み、JSON で出力する。

## 2. サブモジュール

| Slot | 種別 ID | モジュール | 生値 | パラメータ順（公開資料） |
|---|---:|---|---|---|
| 1 | 16 | SHORT DELAY | 3, 61, 48 | TIME, FEEDBACK, DRY/WET |
| 2 | 16 | SHORT DELAY | 0, 61, 48 | TIME, FEEDBACK, DRY/WET |
| 3 | 28 | FORMANT FILTER | 0, 1, 0 | FORMANT 1, FORMANT 2, BALANCE |
| 4 | 17 | TUBE CLIP | 27, 75 | GAIN, OUTPUT LEVEL |
| 5 | 20 | 3 BAND EQ | 50, 50, 50 | HI, MID, LOW |
| 6 | 18 | COMPRESSOR | 0, 0, 10, 10 | THRESHOLD, RATIO, ATTACK, RELEASE |

種別と生値は **確定**。パラメータ順は公開資料どおりで、スクリーンショットのノブ位置とも矛盾しない
（FEEDBACK 61 ≒ 1 時半、DRY/WET 48 ≒ 12 時、GAIN 27 ≒ 10 時、OUT LEVEL 75 ≒ 2〜3 時、EQ 50 = 12 時）。
このことから **生値の範囲は 0〜100 = ノブの全回転** と **推定** した。

### 2.1 SHORT DELAY の TIME（最重要の未確定事項）

スクリーンショットでは 2 つの TIME ノブがどちらも時計回り最大付近を指している（SHORT DELAY 2 の方が
わずかに右）。一方、保存値は 3 と 0。

検討した説明:

| 仮説 | 内容 | 評価 |
|---|---|---|
| A 逆向き | 生値が小さいほど長い（位置 = 1 − 生値/100）。2 = 0 → 最大、1 = 3 → 97 % | スクリーンショットの「2 の方が右」と一致。**採用（推定）** |
| B 昇順 | 生値 3 / 0 → ほぼ 0 ms。ノブ表示だけが逆 | 名前の "PHASE"（極短ディレイ＝コム）とは合うが、ノブ表示と矛盾 |
| C 対応ずれ | TIME が別の位置に保存されている | 他の 2 パラメータが整合しており、根拠なし |

ms への換算曲線は不明。Roland の公称最大 80 ms を使い **線形（暫定）** とした:
SHORT DELAY 1 = 77.6 ms、SHORT DELAY 2 = 80.0 ms。差 2.4 ms は独立に保持する。

仮説 A では左右の経路が約 80 ms 遅れ、互いに 2.4 ms ずれる。モノラルで合わさると 1/2.4 ms ≒ 417 Hz 間隔の
コムになる（"PHASE" の名前とはこの意味でも整合し得るが、証拠ではない）。仮説 B を試したい場合は
ノブ 8（TIME）を 0〜3 % まで下げると近い状態になる（B では SHORT DELAY 1 の方が 3 % 長くなる点だけ逆）。

確定させる方法: Customizer で SHORT DELAY 1 の TIME だけを最小・中央・最大に変えて 3 回保存し、
`tools/aira_patch.py diff` で `sub1[1]` の値を比べる。

### 2.2 FORMANT FILTER

FORMANT 1/2 のノブには a・i・u・e・o の 5 つの目盛りがある（日本語の母音順）。生値は 0 と 1、ポインタは
FORMANT 1 が a 付近、FORMANT 2 が i 付近。ここから **生値 = 母音番号 0〜4** と **推定** した（生値の上限が 4 か、もっと
細かいかは不明）。CV（GRF3）は 0〜1 で 5 母音を連続的にスイープする（**暫定**）。
BALANCE 0 = FORMANT 1 のみ（**推定**、ノブが最小）。

## 3. メインモジュール（BITRAZER）

公開資料の PARAMETER 1〜10 は LPF/HPF, BYPASS, SAMPLE RATE, FILTER CUTOFF, BIT DOWN (RATE DOWN),
RESONANCE と、その 4 つの LEVEL。16 バイトの読み方は 2 通り:

| | H1: バイト 0 は見出し、1〜10 が P1〜P10 | H2: 0〜9 が P1〜P10 |
|---|---|---|
| LPF/HPF | 0 | 1 |
| SAMPLE RATE / CUTOFF / BIT DOWN / RESO | 0 / 0 / **50** / 0 | 0 / 0 / 0 / 50 |
| 各 LEVEL（SR, CUT, BD, RESO） | 100, 100, 50, 100 | **0**, 100, 100, 50 |

**H1 を採用（推定）**。理由: H2 では SAMPLE RATE LEVEL が 0 になり、スクリーンショットにある
GRF1 → SAMPLE RATE のケーブルが何もしないことになる。またスクリーンショットの LEVEL ノブは右寄り。
ただし H1 でも BIT DOWN LEVEL 50 はスクリーンショット（右寄り）と食い違う。BIT DOWN の端子は空いて
いるので、この食い違いは音には影響しない。

MOD IN 1〜6 がどのパラメータに入るかは公開資料にない。**MOD k = PARAMETER k と仮定（推定）** した。
スクリーンショットでは端子ごとに矢印で行き先が描かれており、ケーブルの行き先（SAMPLE RATE、
FILTER CUTOFF、TYPE、BYPASS）はそこから読んでいる。

## 4. 配線（スクリーンショットからの追跡、暫定）

`patch/routing_screenshot.json`。ID は公開資料の端子番号。

```
L: INPUT1 → SHORT DELAY 1 → TUBE CLIP IN1 → BITRAZER IN1 → OUT1 → OUTPUT1
R: INPUT2 → SHORT DELAY 2 → FORMANT → TUBE CLIP IN2 → BITRAZER IN2 → OUT2
          → COMPRESSOR IN1 → OUT1 → 3 BAND EQ → OUTPUT2
GRF1 → SAMPLE RATE    GRF2 → FILTER CUTOFF   GRF3 → FORMANT 1
GRF4 → EQ LOW         GRF5 → LPF/HPF         GRF6 → BYPASS
```

17 本。各ケーブルに確度（high / medium）と根拠を記録した。medium は、ケーブルがパネルの下に垂れて
いて途中が見えないもの、他のケーブルと重なっているもの。フィードバック（閉路）は見当たらない。

**ファイルの非 0 バイトは 18 個、追跡したケーブルは 17 本**。1 本見落としているか、非 0 バイトが
ケーブル 1 本 = 1 バイトではない可能性がある。これがこの配線を「原機どおり」と呼ばない理由である。

GRF の割り当ては Roland の既定（GRF3 = BIT DOWN、GRF4 = RESONANCE）から 2 つ変えてある
（GRF3 → FORMANT 1、GRF4 → EQ LOW）。GRF1/2/5/6 は既定どおり。

## 5. ケーブル領域 0x80–0x47F（未解読）

非 0 バイト（オフセット: 値）:

```
0x082:15 0x0B0:16 0x0D2:8  0x0FD:6  0x133:5  0x153:12 0x167:8  0x178:13 0x1A3:14
0x1C8:17 0x210:9  0x232:12 0x282:3  0x2DB:4  0x31A:9  0x345:15 0x36D:13 0x3DA:12
```

試して否定した読み方（どれも追跡した配線と矛盾した）:

- 値 = 接続元 ID / 接続先 ID（公開番号）: SHORT DELAY OUT2 など存在しない端子が出る。値の重複（12 が 3 回）
  が接続先の一意性と合わない。
- 位置 = 接続元 × 34 + 接続先（公開番号、22×34 = 748）: 位置 858 が範囲外。
- 位置 = 32 × 32 行列（行 = 接続元、列 = 接続先）: 行 22, 23, 26 が接続元の範囲外。列の重複 5 組が
  接続先の一意性と合わない。
- 位置 = 32 × 32（行 = 接続先、列 = 接続元）: 異なる接続元が 13 個になり、追跡の 17 個と合わない。
- 値をビット列（公開資料の「Patch Cable Condition」形式）として読む: 立つビット 39 個、ケーブル 18 本の
  倍とも合わない。

値の重複が多い（8, 9, 12, 13, 15）ことから、値はケーブルの色やたるみ等の GUI 情報で、接続は位置側に
（Customizer 独自の端子番号で）入っている可能性があるが、**証拠はない**。

確定させる方法（ハードウェアへの送信は不要）:

1. 元のパッチを残したまま、Customizer でケーブルを **1 本だけ** 外して別名保存する。
2. `python3 tools/aira_patch.py diff BITRAZER_MONSTER_PHASE.bin 新しいファイル.bin`
3. 動いたバイトがそのケーブルである。数本分やれば番号付けが決まる。
4. GRF の割り当ても同様に 1 箇所ずつ変えて比べる。

`diff` はケーブル領域の変化を 32 列・34 列の両方の分解で表示する。

## 6. DSP モデル（すべて近似）

Roland はどのモジュールの DSP も公開していない。以下は公開されている説明に沿った近似で、
定数はすべて `dsp/mp_convert.c`（換算）と `dsp/mp_dsp.c`（処理）にあり、テストで固定してある。

| ブロック | 実装 | 区分 |
|---|---|---|
| SHORT DELAY | 4 点 Hermite 補間の分数遅延、時間変更は 20 ms でグライド。フィードバック = ノブ値、ループ内は ±2 で飽和する有理 tanh（100 % でも発散しない）。DRY/WET は線形クロスフェード（48 % でコムの谷が深く出る） | 暫定 |
| FORMANT FILTER | 母音ごとに 3 フォルマント（標準のテノール表）を TPT 状態変数フィルタのバンドパスで並列。母音の間は周波数を対数補間。BALANCE で 2 つのバンクを線形に混ぜる | 暫定 |
| TUBE CLIP | 非対称ソフトクリップ `tanh(u+0.25) − tanh(0.25)`（偶数次倍音）、4 倍オーバーサンプリング（64 タップ窓付き sinc）、5 Hz の DC 除去。GAIN 0〜100 → +0〜36 dB。OUT LEVEL は保存値 75 で 0 dB になるよう較正 | 暫定 |
| 3 BAND EQ | ローシェルフ 120 Hz、ピーキング 1 kHz（Q 0.7）、ハイシェルフ 6 kHz、±15 dB、50 = フラット | 暫定 |
| COMPRESSOR | フィードフォワード、ステレオリンクのピーク検出、dB 領域のアタック/リリース。THRESHOLD 0 → 0 dBFS〜100 → −40 dBFS、RATIO 0 → 1:1〜100 → 20:1 | 暫定 |
| BITRAZER | 自走サンプル＆ホールド（アンチエイリアスなし＝折り返しが音）、保持時に量子化（16〜2 ビット）、TPT 状態変数フィルタ LPF/HPF（20 Hz〜20 kHz、Q 0.5〜25）、LPF/HPF と BYPASS は 5 ms のクロスフェード | 暫定 |

注意すべき帰結:

- **COMPRESSOR は保存値（THRESHOLD 0、RATIO 0）がこの換算では 1:1 になり、何もしない**（テストでビット
  一致を確認）。スクリーンショットの THRESHOLD ノブは最大付近で、TIME と同じ「生値 0 なのに最大」の
  形をしている。原機で効いていた可能性はある。
- BIT DOWN は保存値 50（H1）でその端子は空き。この換算では常時 9 ビット相当の量子化がかかる。

## 7. GRF の既定値

GRF1〜4 のノブ位置はペダル本体の状態で、パッチファイルには入っていない（入っていても未解読領域）。
次の値は **ここで選んだもの（暫定）**:

| GRF | 行き先 | 既定 | 理由 |
|---|---|---|---|
| GRF1 | SAMPLE RATE（基準 0 + LEVEL 100 % × GRF1） | 70 % | 約 8.4 kHz。0 だと 172 Hz で原音がほぼ残らない |
| GRF2 | FILTER CUTOFF（基準 0 + 100 % × GRF2） | 90 % | 約 10 kHz。0 だと 20 Hz の LPF で無音に近い |
| GRF3 | FORMANT 1（基準 a + GRF3 × 4 母音） | 0 % | 保存値 a のまま |
| GRF4 | EQ LOW（基準 50 + GRF4） | 0 % | 保存値フラットのまま |
| GRF5 | LPF/HPF（保存値 LPF を反転） | OFF = LPF | 保存値のまま |
| GRF6 | BYPASS | OFF | 保存値のまま |

CV の加算は「正規化したノブ値 + LEVEL × CV、0〜1 で制限」（**推定**: Roland の「GRF の設定は CV に
加算される」という記述に基づく。単極か両極かは不明で、単極とした）。
