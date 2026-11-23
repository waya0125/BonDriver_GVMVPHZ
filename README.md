# BonDriver_GVMVPHZ

I-O DATA **GV-MVP/HZ**（`04bb:0522`、地上デジタル専用・1T シングルチューナー）向けの Windows 用 **WinUSB（Zadig）直叩き BonDriver** です。

* **純正ドライバ不要**: Zadig で WinUSB ドライバを割り当てるだけで Windows 10 / 11（x64 / x86）上で動作します。
* **本体内蔵 B-CAS ハードウェア復号**: 本体スロットの B-CAS カードで復号された TS を出力するため、外部カードリーダー・`B25Decoder` は不要です。
* **DFU 自動ファームウェア転送**: 電源投入直後（コールド / DFU 状態）でも、DLL と同じフォルダに `NXP7164F_052204BB.bin` を置いておけば自動で転送します。

> **関連プロジェクト**
> * **GV-MVP/VZ（`04bb:052b`）向け BonDriver**: nuIlpointer 氏の [BonDriver_GVMVPVZ](https://github.com/nuIlpointer/BonDriver_GVMVPVZ) をご利用ください。
> * **Linux 向け録画コマンド（VZ / HZ 両対応）**: [recgvmvp](https://github.com/waya0125/recgvmvp)

## セットアップ

1. **Zadig で WinUSB ドライバを割り当てる**
   * [Zadig](https://zadig.akeo.ie/) を起動し、`Options` → `List All Devices` を有効にします。
   * `04bb:0522`（GV-MVP/HZ）を選択し、ドライバに **WinUSB** を選んで `Install Driver` / `Replace Driver` を実行します。
   * ※ 電源投入直後の DFU 状態と、ファームウェア転送後の通常状態の両方で USB ID は `04bb:0522` です。初回ファームウェア転送後に再度ドライバ未割り当てになる場合は、通常状態でも同様に WinUSB を割り当ててください。
2. **ファームウェアの配置**
   * 純正ドライバ等から取り出した `NXP7164F_052204BB.bin`（SHA-256: `162323cdb0fef9a5b0814a08a23dd8d437c2e9c0bc8803d88a9d2a4c0b91341f`）を、`BonDriver_GVMVPHZ.dll` と同じフォルダに配置します（環境変数 `GVMVPHZ_FW_PATH` / `GVMVP_FW_PATH` でも指定可能）。
3. **TVTest / EDCB への配置**
   * `BonDriver_GVMVPHZ.dll`（使用するアプリに合わせて x64 または x86）を TVTest や EDCB の BonDriver フォルダにコピーします。
   * チャンネル空間は `UHF`（13ch〜62ch）の 1 空間です。

## ビルド方法（Windows / Visual Studio）

```powershell
# x64
cmake -B build-x64 -A x64
cmake --build build-x64 --config Release

# x86
cmake -B build-x86 -A Win32
cmake --build build-x86 --config Release
```

外部ライブラリは不要です（Windows 標準の `winusb.lib` / `setupapi.lib` / `bcrypt.lib` のみ使用）。

## ライセンス

MIT License（[LICENSE](./LICENSE)）
IVDRM セッション制御・TS 復号手順は nuIlpointer 氏の [BonDriver_GVMVPVZ](https://github.com/nuIlpointer/BonDriver_GVMVPVZ)（MIT）を参考にしています。
