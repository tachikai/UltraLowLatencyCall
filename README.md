# UltraLowLatencyCall

超低遅延音声通話アプリ（リモート音楽セッション向け）

## 特徴
- 最大20人同時接続
- VST3プラグイン + スタンドアロン両対応
- インターネット経由リモートセッション
- 片道遅延 約10〜15ms

## 技術スタック
- クライアント：JUCE（C++）/ Opus codec / UDP / Windows 11
- サーバー：Rust（tokio）/ C++（AVX2）/ Ubuntu Server 24.04 LTS

## 開発状況
| フェーズ | 内容 | 状態 |
|---|---|---|
| Phase 0 | 環境構築 | ✅ 完了 |
| Phase 1A | サーバーUDP基盤 | ✅ 完了 |
| Phase 1B | JUCE音声基盤 | ✅ 完了 |
| Phase 2 | Opusコーデック統合 | ✅ 完了 |
| Phase 3 | ネットワーク統合テスト | ✅ 完了 |
| Phase 4 | サーバー混合エンジン | ✅ 完了 |
| Phase 5 | マルチクライアントテスト | ✅ 完了 |
| Phase 6 | VST3パラ出力 | ✅ 完了 |
| Phase 7 | UI仕上げ | 🔧 開発中 |
| Phase 8 | 低遅延チューニング | ⬜ 未着手 |
| Phase 9 | デプロイ | ⬜ 未着手 |

## ライセンス
MIT License

## 作者
tachikai
