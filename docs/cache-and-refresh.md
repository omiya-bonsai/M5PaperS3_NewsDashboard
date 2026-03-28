# Cache And Refresh

## 目的

このスケッチは、操作時の待ち時間を減らすために SD キャッシュを中心に動きます。

やりたいことは次の 2 つです。

- 閲覧時はできるだけ即表示する
- 通信は待機中に寄せる

## 取得対象

- `index.png`
- `page1.png` から `page6.png`
- `index.version`

## `index.version` の役割

画像そのものより先に `index.version` を見ます。

- 同じなら `index.png` を再取得しない
- 変わっていたら `index.png` を更新する
- 変わっていたら詳細キャッシュも無効化する

これにより、不要な画像ダウンロードを避けます。

## キャッシュの置き場所

SD カード直下に保存します。

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

## キャッシュ有効期限

ページごとに TTL を持っています。

- `index.png`: 短め
- `page1.png` から `page6.png`: 長め

TTL 内なら再表示時にネットワークへ行かず、SD からそのまま描画します。

## 起動直後の流れ

1. `index` キャッシュがあれば先に表示
2. そのあとで `index.version` を確認
3. 差分があれば `index.png` を更新
4. 必要なら優先詳細を先読み

つまり「まず見せて、あとで更新する」構造です。

## 待機中の定期更新

待機中は更新間隔ごとに `index.version` を確認します。

- 差分なし
  - 何もしない
- 差分あり
  - `index.png` を更新
  - 詳細キャッシュを無効化
  - 優先詳細を先読み

現在 `index` を表示中なら、そのまま再描画します。  
詳細ページ表示中なら、画面を崩さずキャッシュだけ更新します。

## 優先先読み

全ページを常に先読みするのではなく、通常バッテリー時だけ次を優先します。

- `page1.png`
- 前回最後に見た詳細ページ

これで待ち時間を減らしつつ、通信量を抑えます。

## 通信失敗時の方針

- 失敗しても既存画面は消さない
- すでに表示中の画面を維持する
- ステータスコードだけを更新する

`HTTP GET -> -1` のような失敗が起きても、白画面へ落ちにくいようにしています。

## 関連する主な関数

- `downloadIndexVersion()`
- `shouldSkipIndexDownloadByVersion()`
- `fetchPageToCacheOnly()`
- `prefetchPriorityPages()`
- `refreshIndexCacheInBackground()`
- `runPeriodicIndexRefreshIfDue()`
- `loadPage()`
