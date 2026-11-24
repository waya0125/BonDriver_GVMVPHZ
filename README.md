# BonDriver_GVMVPHZ

I-O DATA の USB テレビチューナー GV-MVP/HZ を、Windows の TVTest・EDCB で使う BonDriver（`IBonDriver2`）です。  
WinUSB で機器を直接操作するので、純正ドライバーは不要です。  
本体のスロットに挿した B-CAS カードで復号された TS を出すので、外付けのカードリーダーや `B25Decoder` も不要です。

## 対応機種

| 機種 | USB ID | 放送波 | B-CAS | 対応 Windows | 詳細・動作確認 |
|---|---|---|---|---|---|
| I-O DATA GV-MVP/HZ | `04bb:0522` | 地デジ（1 チューナー） | 本体のスロット | 11（x64 で実機確認済み。10・x86 はビルド確認のみ） | [docs/GV-MVP-HZ.md](docs/GV-MVP-HZ.md) |

## 関連プロジェクト

- GV-MVP/VZ（`04bb:052b`）の BonDriver: nuIlpointer 氏の [BonDriver_GVMVPVZ](https://github.com/nuIlpointer/BonDriver_GVMVPVZ)
- Linux の録画コマンド（VZ・HZ 両対応）: [recgvmvp](https://github.com/waya0125/recgvmvp)
- 手元のチューナーの動作確認の一覧: [チューナー動作確認一覧](https://waya0125.github.io/tuners/)

## 必要なもの

- [Zadig](https://zadig.akeo.ie/)（WinUSB ドライバーの割り当てに使う）
- 純正ドライバーに入っているファームウェア `NXP7164F_052204BB.bin`
  - SHA-256: `162323cdb0fef9a5b0814a08a23dd8d437c2e9c0bc8803d88a9d2a4c0b91341f`
  - 純正ドライバーの入手元: [製品ページ](https://www.iodata.jp/product/av/tidegi/gv-mvphz/)・[サポートライブラリ](https://www.iodata.jp/lib/product/g/2095.htm)

ファームウェアはメーカーの著作物なので、このリポジトリには含めていません。

## 入手

[Releases](https://github.com/waya0125/BonDriver_GVMVPHZ/releases) から、x64 版と x86 版の DLL を入手できます。

## 準備

1. **WinUSB ドライバーを割り当てる**
   - Zadig を起動し、`Options` → `List All Devices` を有効にします。
   - `04bb:0522`（GV-MVP/HZ）を選び、ドライバーに **WinUSB** を選んで `Install Driver` / `Replace Driver` を実行します。
   - 電源を入れた直後の DFU 状態でも、ファームウェアを送ったあとの通常の状態でも、USB ID は `04bb:0522` です。  
     初回のファームウェア転送のあとに、またドライバーが割り当てられていない状態になった場合は、通常の状態でも同じように WinUSB を割り当ててください。
2. **ファームウェアを置く**
   - `NXP7164F_052204BB.bin` を、`BonDriver_GVMVPHZ.dll` と同じフォルダーに置きます。  
     環境変数 `GVMVPHZ_FW_PATH` / `GVMVP_FW_PATH` でも指定できます。
   - 電源を入れた直後（コールド・DFU 状態）でも、DLL が自動で転送します。
3. **TVTest・EDCB に置く**
   - `BonDriver_GVMVPHZ.dll`（使うアプリに合わせて x64 か x86）を、TVTest や EDCB の BonDriver のフォルダーにコピーします。

## 使い方

- チャンネル空間は `UHF`（13ch〜62ch）の 1 つです。  
  TVTest でチャンネルスキャンをしてから使ってください。
- TVTest・EDCB 側の B-CAS カードリーダーの設定は不要です。

## ビルド（Windows / Visual Studio）

```powershell
# x64
cmake -B build-x64 -A x64
cmake --build build-x64 --config Release

# x86
cmake -B build-x86 -A Win32
cmake --build build-x86 --config Release
```

外部のライブラリは不要です（Windows 標準の `winusb.lib` / `setupapi.lib` / `bcrypt.lib` だけを使います）。  
`v*` のタグを push すると、GitHub Actions が x64・x86 の DLL をビルドします。

## 注意事項

- GV-MVP/HZ は地デジ専用です。BS・CS は受けられません。
- バスパワーで最大 500mA を使います。  
  ハブ経由でほかの機器と同時につなぐ場合は、電源の容量に気をつけてください。

## ライセンス・出典

- MIT License（[LICENSE](./LICENSE)）
- IVDRM のセッション制御と TS の復号の手順は、nuIlpointer 氏の [BonDriver_GVMVPVZ](https://github.com/nuIlpointer/BonDriver_GVMVPVZ)（MIT）を参考にしています。
