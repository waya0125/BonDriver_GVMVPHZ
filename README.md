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
4. **（必要なら）INI を置く**
   - 複数台つなぐ場合や転送の設定を変える場合は、[`BonDriver_GVMVPHZ.ini.sample`](./BonDriver_GVMVPHZ.ini.sample) を DLL と同じ名前の `.ini`（例: `BonDriver_GVMVPHZ.ini`）にして、DLL と同じフォルダーに置きます。  
     INI が無い場合は 1 台目の機器を使います（これまでと同じ動き）。

## 使い方

- チャンネル空間は `UHF`（13ch〜62ch）の 1 つです。  
  TVTest でチャンネルスキャンをしてから使ってください。
- TVTest・EDCB 側の B-CAS カードリーダーの設定は不要です。

### 複数台をつなぐ場合

INI の `[GVMVPHZ]` セクションの `DeviceIndex` で、使う機器を選びます。

| 値 | 動き |
| --- | --- |
| `0` 以上 | Windows が列挙した順番で、その番号の機器を開きます（0 始まり）。 |
| `-1` | ほかのアプリが使っていない機器を、列挙順に探して開きます。 |

- EDCB などで複数台を同時に使う場合は、DLL を `BonDriver_GVMVPHZ-0.dll`・`BonDriver_GVMVPHZ-1.dll` のようにコピーし、それぞれに同じ名前の INI を置きます。全部 `DeviceIndex=-1` にしておけば、開いた順に空いている機器が割り当てられます。
- 「使っているかどうか」は、WinUSB が同時に 1 つのアプリからしか開けないことを利用して判定しています（開けなかった機器は使用中として飛ばします）。
- 列挙の順番は、つなぐ USB ポートを変えると入れ替わることがあります。決まった機器を使いたい場合は、ポートを固定してください。

### 動作ログ（USB 速度・転送パラメータの確認）

DLL は、開いた機器の情報を `OutputDebugString` で出力します。  
[DebugView](https://learn.microsoft.com/sysinternals/downloads/debugview)（Sysinternals）で `[BonDriver_GVMVPHZ]` の行を見ると、次のことを確認できます。

- どの機器（何番目）を開いたか、開けなかった機器のエラー番号
- USB の接続速度（`WinUsb_QueryDeviceInformation(DEVICE_SPEED)`）。`HighSpeed` 以外の場合は警告が出ます（TS 約 17〜23Mbps が入らないため）。
- TS エンドポイント `0x83` の種類・`wMaxPacketSize` など（Windows 8.1 以降は `WinUsb_QueryPipeEx`、それより前は `WinUsb_QueryPipe`）
- 実際に使う Bulk 転送の数とサイズ（`UrbSize` が `wMaxPacketSize` の倍数でない場合は切り上げます）

## ビルド（Windows / Visual Studio）

```powershell
# x64
cmake -B build-x64 -A x64
cmake --build build-x64 --config Release

# x86
cmake -B build-x86 -A Win32
cmake --build build-x86 --config Release
```

単体テスト（機器が無くても動く部分）は、Linux でも Windows でも `ctest` で実行できます。

```sh
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
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
