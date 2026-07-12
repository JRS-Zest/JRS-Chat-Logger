# item.dat 更新対応サマリ

目的:
- item.dat のアイテムデータブロック長が 426 バイトから 618 バイトに変更され、
  ファイル全体一括復号から“ブロック（レコード）ごと”の復号方式に切り替わったため、
  C++ 側の読み込みロジックを修正した。

参照:
- 参考実装 (Python): `src/参考ファイル/packet_crypt.py`, `src/参考ファイル/extract_5blocks.py`
  - `packet_crypt.py` の `generate_scenario_decode_key` / `decode_scenario_buffer` ロジックを参照
  - `extract_5blocks.py` は 618 バイト単位で復号して連結するサンプル

行った変更（コード）:
- 定数変更
  - `ITEM_BASE_INFO_SIZE`: 426 -> 618 （`src/item_dat_loader.h`）
- 復号方式変更（`src/item_dat_loader.cpp`）
  - `LoadItemDat`:
    - これまでのファイル全体一括復号から、各レコード（最大 618 バイト）ごとに
      `PacketCrypt::DecodeScenarioBuffer` を呼び出して復号し、連結してから `ParseItems` を実行する実装に変更。
  - `GetAllBlocksOrdered`:
    - ファイル内出現順を保持するため、ファイル読み込み後に各レコードごとに復号して
      順番どおりに `ItemInfo` を生成するよう変更。
  - `ParseOptions`:
    - 参考実装と照合した結果、既存のチャンク単位復号実装を維持（OP ブロックの処理は変更なし）

ビルド/検証:
- `cmake --build build --config Release` により Release ビルドを実行済み。
- 出力: `build\Release\JRSChatCPP.exe` を生成（ビルド成功）

テスト手順（推奨）:
1. 実際の `item.dat` を配置して、アプリ起動または該当機能を操作し、アイテム名/型/価格が期待どおりに表示されるか確認。
2. 参考の Python スクリプトでデコード確認する場合:

```bash
python src/参考ファイル/extract_5blocks.py
```

今後の注意・改善案:
- `PacketCrypt` の C++ 実装（`packet_crypt.cpp/.h`）のテストカバレッジを増やして、復号互換性を自動検証するテストを追加すると安全。
- item.dat に新しいフィールドや構造が追加される可能性があるため、`ItemInfo` のフィールド仕様を参照元と随時突合する。

変更ファイル一覧:
- 修正: `src/item_dat_loader.h`
- 修正: `src/item_dat_loader.cpp`
- 参照: `src/参考ファイル/packet_crypt.py`
- 参照: `src/参考ファイル/extract_5blocks.py`

必要なら、この変更をコミットしてプルリク用の説明文も作成します。ご希望を教えてください。
