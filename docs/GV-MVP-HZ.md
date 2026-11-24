# I-O DATA GV-MVP/HZ

## 1. 概要

| 項目 | 内容 |
|---|---|
| USB ID | `04bb:0522`（USB 2.0 High-Speed、バスパワー最大 500mA。DFU 状態でも同じ ID） |
| 放送波 | 地デジ |
| チューナー数 | 1 |
| B-CAS | 本体のスロット（本体で復号するので、外付けのカードリーダー・`B25Decoder` は不要） |
| 対応 Windows | 10・11（x64・x86） |
| Windows | [BonDriver_GVMVPHZ](../README.md) |
| Linux | [recgvmvp](https://github.com/waya0125/recgvmvp) |
| 公式ページ | [製品ページ](https://www.iodata.jp/product/av/tidegi/gv-mvphz/)・[サポートライブラリ](https://www.iodata.jp/lib/product/g/2095.htm) |

## 2. 準備

1. Zadig で `04bb:0522` に WinUSB を割り当てます。
2. 純正ドライバーから `NXP7164F_052204BB.bin` を取り出し、`BonDriver_GVMVPHZ.dll` と同じフォルダーに置きます。
3. DLL を TVTest・EDCB の BonDriver のフォルダーにコピーします。

手順の詳細は [README の「準備」](../README.md#準備) にあります。

## 3. 使い方

- チャンネル空間は `UHF`（13ch〜62ch）の 1 つです。
- TVTest・EDCB 側の B-CAS カードリーダーの設定は不要です。
- 付属の `bon_test.exe` で、TVTest を使わずに受信を試せます。

```powershell
# DLL のパス、UHF のチャンネル、秒数、出力先（省略できる）
bon_test.exe BonDriver_GVMVPHZ.dll 25 5 out.ts
```

## 4. 動作確認の記録

| 確かめたこと | 環境 | 結果 |
|---|---|---|
| TVTest での視聴 | 【未記入】 | 【未記入】 |
| `bon_test.exe` での受信 | 【未記入】 | 【未記入】 |

## 5. 注意事項

- 地デジ専用です。BS・CS は受けられません。
- 初回のファームウェア転送のあとに、またドライバーが割り当てられていない状態になった場合は、通常の状態でも WinUSB を割り当ててください。

## 6. 技術メモ

機器の中身（エンドポイント、DFU 状態の見分け方、チューナー・復調器の初期化と選局）は、recgvmvp の [docs/GV-MVP-HZ.md](https://github.com/waya0125/recgvmvp/blob/main/docs/GV-MVP-HZ.md) にまとめています。
