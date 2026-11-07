# 開発エージェント向け指針

このファイルはリポジトリ全体に適用する。ユーザーの明示的な指示を優先し、既存の作業差分を保持して依頼された範囲を変更する。作業の説明・結果報告は日本語で行う。

## プロジェクトの構成

- `include/xstream.h`：XMLのReader・Writerと共有する文字処理。ライブラリはヘッダーのみ、C++17対応、外部依存なしを維持する。
- `main.cpp`、`xstream.pro`：利用例とそのqmake設定。
- `test/test*.cpp`、`test/test.h`：GoogleTestによる通常テストと補助処理。
- `test/test.pro`：通常テストのqmake設定。現在はC++20を使用する。テストの追加時はソース一覧も更新する。
- `test/no_exceptions/main.cpp`：GoogleTestに依存しない、例外無効ビルドの検証用プログラム。
- `test/fuzz/`：任意にビルドするlibFuzzerターゲット、コーパス、辞書、実行・再現手順。
- `README.md`：公開API、対応範囲、入力・戻り値の寿命、エラー、資源制限の仕様。
- `PLAN.md`：未完了の作業だけを記載する。対応・検証が完了した項目は削除し、未依頼の残項目を自動的に実装しない。

## 参照・ポインタの規約

- 参照を使ってよいのはconstの場合だけとする。非const参照の引数・戻り値・メンバー・ローカル変数を追加しない。
- 出力引数や呼び出し元の状態を更新する引数はポインタにする。例：`size_t *width`、`std::string *output`、`std::vector<char> *output`。
- 読み取り専用の引数やアクセサーにはconst参照を使用してよい。
- 呼び出し側では`&output`など、更新先のアドレスを明示する。
- 公開されている変換APIの出力ポインタは必須とし、`nullptr`なら`false`と`InvalidOperation`を返す。任意の診断ポインタは`nullptr`を許容する。
- 周辺コードのインデント・命名に合わせ、無関係な整形変更を混ぜない。

## エラー処理

- 通常の入力エラー・I/Oエラーは戻り値またはオブジェクトの状態で扱い、`throw`・`try`・`catch`を使わない。
- Readerは`ReadResult`、`state()`、`error()`を利用する。`next()`の`false`だけで正常終了と判断しない。
- Writerは操作の`bool`戻り値と`good()`・`error()`を利用する。最初の失敗を保持し、それ以降の操作で出力を続けない。
- 文字変換で成否を区別する場合は`html_decode(input, &output, &error)`などのチェック付きAPIを使う。失敗時は出力を変更せず、成功時は診断をクリアする。
- 文字列を返す簡易APIの空文字は、失敗と正常な空入力を区別できない。これを判定に使わない。
- Reader・Writerの最初の診断を、後続操作やI/Oエラーで上書きしない。
- 再入防止フラグなどの後始末はRAIIで行う。標準コンテナのメモリ確保失敗や利用者のコールバック由来の例外まで、状態APIで回復できるとは約束しない。
- 例外無効でライブラリがビルドできることを維持する。テスト内の意図的な例外検証と、通常のエラー処理は区別する。

## ストリーミングと文字処理

- 一括入力と分割入力で、イベント列・値・最終結果・エラー位置を一致させる。
- `NeedInput`とEOFを区別する。空入力やデータを補充しないコールバックはEOFではなく、`finish_input()`がEOF通知である。
- 未完成のトークンをイベントとして公開しない。入力補充や再配置をまたいで、入力バッファへのポインタ・`string_view`を保持しない。
- 借用入力は解析終了まで呼び出し元が保持する。Readerを一時的な`std::string`から構築して、参照先を破棄しない。
- 入力検証をアクセサーの呼び出し有無に依存させない。Readerのエラー位置は文書全体での0始まりのバイト位置とする。
- 有限の資源制限を維持し、上限・整数オーバーフローを確保やコピーの前に確認する。分割のたびにトークン全体を再解析する実装を避ける。
- UTF-8とXML 1.0の文字・名前規則、文字参照の検証を維持する。対応するXMLの範囲はREADMEに明記し、完全準拠を暗黙に約束しない。
- DOCTYPE・実体宣言は未対応として拒否する。コメントの寛容な解析は`allow_invalid_comments`を明示的に有効にした場合だけ許可し、既定値`false`を維持する。
- C++20の`u8`リテラルは`char8_t`になる。`std::string`へ直接代入せず、テストでは既存の`utf8_bytes()`などでUTF-8バイト列をコピーする。

## テストと検証

変更した挙動を確認するテストを追加・更新する。条件分岐内の値の比較だけにせず、イベントの到達・件数・順序・最終結果も確認する。入力の分割、途中EOF、診断の保持、資源上限、異常系を変更内容に応じて検証する。

C++の挙動や公開APIを変更した場合は、C++17・C++20の通常テストに加え、qmake、`-DNDEBUG`、ASan／UBSan、例外無効ビルドを確認する。テスト件数を固定値で判断せず、実行結果と未実施の検証を報告する。文書のみの変更では内容・リンク・差分を確認し、コードのテストを再実行する必要はない。

以下の各ブロックはリポジトリ直下から実行する。生成物は`/tmp`またはビルド用ディレクトリに置く。

### 通常テスト

```bash
for xstream_std in c++17 c++20; do
    g++ -std="$xstream_std" -O0 -Wall -Wextra -Werror \
        -Iinclude test/*.cpp -lgtest -pthread -o "/tmp/xstream-$xstream_std" &&
        "/tmp/xstream-$xstream_std" --gtest_brief=1 || exit 1
done
```

### 最適化・アサート無効

```bash
g++ -std=c++17 -DNDEBUG -O2 -Wall -Wextra -Werror \
    -Iinclude test/*.cpp -lgtest -pthread -o /tmp/xstream-release &&
    /tmp/xstream-release --gtest_brief=1
```

### ASan／UBSan

```bash
g++ -std=c++20 -g -O1 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Iinclude test/*.cpp -lgtest -pthread -o /tmp/xstream-sanitized &&
    ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
    /tmp/xstream-sanitized --gtest_brief=1
```

この開発環境ではLeakSanitizerがptraceと併用できないため、上記ではリーク検出だけを無効にしている。ASanの不正アクセス検出とUBSanは有効にする。

### qmake

```bash
xstream_repo="$(pwd)"
mkdir -p /tmp/xstream-qmake && (
    cd /tmp/xstream-qmake &&
    qmake "$xstream_repo/test/test.pro" 'CONFIG+=release' 'DEFINES+=NDEBUG' &&
    make -j2 &&
    ./test --gtest_brief=1
)
```

### 例外無効ビルド

```bash
for xstream_std in c++17 c++20; do
    g++ -std="$xstream_std" -DNDEBUG -O2 -fno-exceptions -Wall -Wextra -Werror \
        -Iinclude test/no_exceptions/main.cpp -o "/tmp/xstream-no-exceptions-$xstream_std" &&
        "/tmp/xstream-no-exceptions-$xstream_std" || exit 1
done
```

### ファジング

パーサーや共有文字処理を変更した場合は、コーパス再生と回数を指定したファジングも行う。[実行・再現手順](test/fuzz/README.md)に従い、生成コーパスや障害入力は作業用ディレクトリに保存する。回帰テストとして残す入力は、原因を確認してから追加する。通常のライブラリ利用にlibFuzzerなどの依存を追加しない。

## 文書と報告

公開API・対応構文・エラーの契約を変更したらREADMEも更新する。対応済みの作業はPLANから削除し、実装済みの仕様をPLANへ重複して蓄積しない。結果報告には変更内容、検証結果、残る制約を簡潔に記載する。
