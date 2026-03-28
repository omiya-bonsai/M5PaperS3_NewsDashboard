# Architecture

## 概要

このスケッチは、M5PaperS3 を常時待機型のニュースビューアとして使う構成です。

- Raspberry Pi 側が `index.png` と `page1.png` から `page6.png`、`index.version` を配信
- M5PaperS3 側が SD キャッシュを持つ
- 通信は更新時だけ行い、通常の閲覧はキャッシュ優先

## 大きな責務

### 配信側

- RSS から記事情報を集める
- 一覧画像と詳細画像を生成する
- `index.version` を生成する
- HTTP で配信する

### M5PaperS3 側

- `index.version` を見て更新有無を判断する
- 画像を SD にキャッシュする
- タッチ / スワイプでページ遷移する
- バッテリー状態に応じて更新頻度を変える
- 既読 / 未読を `index.version` 単位で管理する

## 実行フロー

### 起動時

1. SD と保存済み状態を初期化
2. バッテリー状態を読む
3. `index` キャッシュがあれば先に表示
4. 必要なら `index.version` を確認して `index.png` を更新
5. `index.version` が変わったら詳細キャッシュを無効化
6. 通常バッテリー時は優先詳細ページを先読み

### 待機中

1. ループでタッチ入力を読む
2. 一定間隔でバッテリー状態を再サンプリング
3. 更新間隔を過ぎたら `index.version` を再確認
4. 差分があれば `index.png` と優先詳細を更新

### 操作時

1. タップまたはスワイプを認識
2. 対象ページの有効キャッシュがあれば即表示
3. キャッシュが無い場合だけ HTTP 取得
4. 有効操作が発生したら現在の `index.version` を既読化

## 状態の持ち方

### キャッシュ状態

- 各ページごとに SD 上の PNG を持つ
- 取得時刻を RAM で持ち、TTL を超えたら無効扱い

### 更新世代

- `lastKnownIndexVersion`
  - 現在配信されている最後の `index.version`
- `lastReadIndexVersion`
  - 端末が既読にした最後の `index.version`

この 2 つの比較で `NEW / READ` を判定します。

## 主要な関数

- `runWakeFlow()`
  - 起動直後の初期表示と更新確認
- `loadPage()`
  - ページ表示の中核
- `runPeriodicIndexRefreshIfDue()`
  - 待機中の定期 `index` 更新
- `prefetchPriorityPages()`
  - 優先詳細ページの先読み
- `handleTouchInput()`
  - タップ / スワイプ判定
- `drawOverlayStatusBar()`
  - フッターと右上ラベルの描画

## 設計上の注意

- deep sleep は使っていない
- 操作時に待たせないため、通信はできるだけ待機中に寄せている
- 通信失敗時は白画面へ落ちず、既存画面を維持する方針
- 既読 / 未読は記事単位ではなく更新世代単位
