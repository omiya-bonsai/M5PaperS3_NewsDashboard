# M5PaperS3 News Dashboard

M5Stack M5PaperS3 で、HTTP 配信されるニュース PNG を表示する常時待機型ビューアです。

このリポジトリは M5PaperS3 側の実装です。全体像は統合ハブ [m5papers3-news-system](https://github.com/omiya-bonsai/m5papers3-news-system)、Raspberry Pi 側は [news-png-generator](https://github.com/omiya-bonsai/news-png-generator) を参照してください。

## ジャンプリンク

- 統合ハブ:
  - [omiya-bonsai/m5papers3-news-system](https://github.com/omiya-bonsai/m5papers3-news-system)
- Raspberry Pi 側:
  - [omiya-bonsai/news-png-generator](https://github.com/omiya-bonsai/news-png-generator)

![M5PaperS3 device photo](img/01.jpeg)

一覧ページ `index.png` と詳細ページ `page1.png` から `page6.png` を SD にキャッシュし、`index.version` の差分確認で不要な再取得を避けます。端末は常時起動のまま待機し、Wi-Fi は更新時だけ有効化します。

## 特徴

- `index.version` による差分確認
- SD キャッシュを使った高速ページ表示
- スワイプとタップによるページ遷移
- 待機中の定期 `index` 更新
- 詳細ページの優先先読み
- `index.version` 単位の `NEW / READ` 表示
- USB 給電時 / バッテリー時での更新ポリシー切替
- バッテリー残量に応じた省電力制御
- 操作時の通信を減らすバックグラウンド更新構成

詳細仕様は [`docs/architecture.md`](docs/architecture.md) と [`docs/cache-and-refresh.md`](docs/cache-and-refresh.md) を参照してください。

## 関連リポジトリ

- 統合ハブ:
  - [omiya-bonsai/m5papers3-news-system](https://github.com/omiya-bonsai/m5papers3-news-system)
- Raspberry Pi 側:
  - [omiya-bonsai/news-png-generator](https://github.com/omiya-bonsai/news-png-generator)

## 想定構成

配信側:

- ニュース画像を生成
- `index.version` を生成
- HTTP で配信

M5PaperS3 側:

- 常時待機
- USB 給電 / バッテリー状態を評価
- 一定間隔で `index.version` を確認
- 変更時のみ `index.png` を更新
- 必要に応じて優先詳細ページを先読み
- ユーザー操作時はキャッシュ優先で表示

## 配信ファイル

取得対象は次の 8 ファイルです。

```text
index.png
page1.png
page2.png
page3.png
page4.png
page5.png
page6.png
index.version
```

コード中の既定 URL は以下です。

```text
http://192.168.3.82:8010/index.png
```

## 差分更新の流れ

画像より先に `index.version` を確認します。

1. `index.version` を取得
2. 前回値と比較
3. 同じなら `index.png` の再取得を省略
4. 変化していれば `index.png` を更新
5. 詳細ページキャッシュを無効化

これにより通信量、消費電力、e-paper の更新回数を抑えます。

## 更新アーキテクチャ

本体は「常時待機しつつ、待機中にキャッシュを育てる」構造です。

基本フロー:

1. 起動直後に `index` キャッシュを表示
2. 必要なら `index.version` を確認して `index.png` を更新
3. `index.version` が変わった時だけ詳細キャッシュを無効化
4. 通常バッテリー時は `page1` と直近詳細ページを優先先読み
5. 待機中は更新間隔ごとに `index.version` を再確認
6. ユーザー操作時はキャッシュ優先で即表示
7. Wi-Fi は更新時だけ ON/OFF

現在の実装ポリシー:

- USB 給電中は高頻度更新
- 通常バッテリー時は中間頻度更新
- 低残量時は更新間隔を延長
- クリティカル時は自動更新を止める
- 詳細ページ先読みは通常バッテリー時のみ有効
- 操作直後 20 秒間はバックグラウンド更新を走らせない

## 操作方法

一覧画面:

- ヘッドラインをタップすると対応する詳細ページへ移動

全画面共通:

- 左スワイプで次ページ
- 右スワイプで前ページ
- 画面最下部から上スワイプで一覧へ戻る
- 右上タップで強制更新

詳細画面:

- タイトル付近をタップすると一覧へ戻る

操作判定の詳細は [`docs/touch-gesture.md`](docs/touch-gesture.md) を参照してください。

## キャッシュ

画像は SD カード直下に保存します。

```text
/index.png
/page1.png
/page2.png
/page3.png
/page4.png
/page5.png
/page6.png
/index.version
```

同一セッション中の再表示では、キャッシュが有効ならネットワークを使わずに描画します。

詳細ページについては、`index.version` 更新時に通常バッテリー時のみ次を優先先読みします。

- `page1.png`
- 前回最後に見た詳細ページ

これにより、ニュース閲覧中のページ遷移待ちを減らしつつ、全ページ常時先読みは避けています。

また、待機中は更新間隔ごとに `index.version` を再確認し、変化があれば `index.png` を更新します。現在 `index` 表示中ならそのまま再描画し、詳細ページ表示中ならキャッシュだけ更新して画面は維持します。

## 画面下部の表示

フッターには以下を表示します。

- `LAST`: 最終成功更新時刻
- 中央ステータス: 現在の状態コードと更新間隔
- `Pn/6`: 現在ページ
- `BAT xx%`: バッテリー残量
- `USB`: USB 給電検出時に付与

低残量時は右側表示を強調します。

## 既読 / 未読表示

既読状態は記事単位ではなく、現在の `index.version` 単位で管理します。

- 新しい `index.version` を取り込むと右上ラベルが `NEW`
- タップ、スワイプ、手動更新などの有効操作が一度でも入ると、その世代は `READ`
- 次の新しい `index.version` が来たら再び `NEW`

右上ラベルは `index NEW` や `page1 READ` のように表示され、未読時は黒地白文字で強調します。

表示まわりの詳細は [`docs/display-ui.md`](docs/display-ui.md) を参照してください。

## Wi-Fi と時刻同期

Wi-Fi は更新時のみ有効化し、処理後に切断します。

NTP サーバ:

- `ntp.nict.jp`
- `time.cloudflare.com`
- `pool.ntp.org`

用途:

- `LAST` 表示用の時刻取得

## セットアップ

### 1. 設定ファイルを作成

[`config.example.h`](config.example.h) を参考に、同じ内容で `config.h` を作成します。

```cpp
#pragma once

static const char* WIFI_SSID     = "YOUR_WIFI_SSID";
static const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

`config.h` は `.gitignore` に入っており、Git には含めません。

### 2. ライブラリ / ボード

- Arduino IDE または `arduino-cli`
- M5PaperS3 ボード定義
- `M5Unified`

### 3. ビルド例

```sh
arduino-cli compile -b m5stack:esp32:m5stack_papers3 .
```

## リポジトリ構成

```text
M5PaperS3_NewsDashboard.ino
config.example.h
README.md
```

## 備考

- 現在の URL、更新間隔、しきい値はコード内定数です
- 優先先読みは通常バッテリー時のみ有効です
- 端末は deep sleep を使わず、常時待機のまま動作します
- バッテリー表示は定期サンプリング値を使うため、USB 挿抜の反映に少し遅れます
- 電源制御の詳細は [`docs/power-policy.md`](docs/power-policy.md) を参照してください
- README は現行スケッチ実装に合わせて更新しています

## License

このリポジトリは [MIT License](LICENSE) です。
