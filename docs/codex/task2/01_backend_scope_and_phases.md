# Codex Task 2: Pico Backend

単独投入用実装指示書。調査日：2026-09-14 UTC。対象repo以外のworkspaceは不要。

**契約状態：C1候補版。参照マスター本文は未取得。** 実装順序と明示handoffを守り、C1を後続Taskで独自変更しない。本ファイルに共通契約を全て含む。

## Repository

| 項目 | 値 |
|---|---|
| 対象repo | [orzkwsk/PicoMotionTrackerBridge](https://github.com/orzkwsk/PicoMotionTrackerBridge) |
| default branch設定 | main |
| base branch | `refactor/pc-bridge-service-fanout` |
| 監査base SHA | `4c0321f02b56a8ce420ba40a988ca9c296f05f61` |
| 作業branch | `refactor/monaka-layer-separation` |
| C1本文SHA256 | `3a76435c8b25b3975028f9a83c4dc3d1e3848806c9608d885f5bba74a1198ed3` |

## 作業手順（必須）

このTaskが変更するworkspaceは上記repoだけ。他repoへのcommit/PR作成をしない。必要な他repo由来code・interfaceは本文に明記したversion固定kit/bundleとして受け取る。外部workspaceがある前提にしない。

1. repositoryとworktree状態、AGENTS.mdを確認する。既存の未commit変更を上書き・削除しない。
2. `git fetch --no-prune origin`でremoteをfetchする。branch削除を伴うpruneは行わない。
3. 指定base branchのremote refを確認し、実際のbase HEADを記録する。下の監査SHAと違う場合は差分を確認し、変化と採用理由を報告する。fast-forwardで責務に矛盾がないことを確認できれば新HEADを使用し、無条件にmainへ戻さない。
4. 確認したbase HEADから`refactor/monaka-layer-separation`を作成する。通常は `git switch -c refactor/monaka-layer-separation <確認済BASE_SHA>`。既存の同名local/remote branchがある場合は作り直さず、指定baseとの祖先関係・既存作業を確認して継続する。遠隔にだけある場合は追跡branchとして取得する。
5. **この作業branch上だけで変更する。** 既存branch削除、force push、resetによる履歴差し替え、amend/rebase等のhistory rewriteは禁止。mainへの直接commit/mergeはしない。
6. 小さいreview可能なcommitに分ける。各段階のbuild、test、hardware未確認を分離し、本文のDoDに照らして完了状態を報告する。

### Codex Cloud branch exception

Codex Cloud may mount the selected repository/base as a writable internal branch named `work`. In that environment, the branch name itself is not required to equal `refactor/monaka-layer-separation`. The invariant is that the checkout must descend from the required GitHub base `refactor/pc-bridge-service-fanout` (audited SHA `4c0321f02b56a8ce420ba40a988ca9c296f05f61`, or a later reviewed fast-forward such as documentation-only preparation commits).

When running under Codex Cloud:

- do not fail solely because `git branch -a` exposes only `work`;
- do not attempt to recreate/switch remote branches that the managed checkout does not expose;
- record `actual_base_sha` from the checkout ancestry and `work_branch` as the actual managed branch name;
- if the checkout does **not** descend from the required base, stop and report the mismatch rather than implementing against `main`;
- all history-rewrite and branch-deletion prohibitions still apply.

## このrepoの責務

PICO private runtime/ABI、enumeration、serial/pose/metadata取得、device clock処理、HMD publisherとPC受信shimを所有する。外部境界はC1 TrackerObservation。repo名は維持する。

起点はmainより22 commit先の`refactor/pc-bridge-service-fanout`。C receiver API branchを4 commit包含し、唯一のHMD受信serviceとconsumer別fan-outまで実装している。古いrender-model系branchは分岐しており、今回の起点に未包含の変更がある。無条件mergeやbranch削除をしない。

## 非責務

共通出力先切替、SteamVR driver、world/mount calibration、Monakaのbody assignment/Fusion/IK。最終状態ではそれらのbuild targetとruntime実装をこのrepoから除く。ただし下記Phase A/Bでconsumer成立前の機能喪失を避ける。

## 既存ファイルと扱い

| 現存箇所 | 作業 |
|---|---|
| `include/pico_ot_backend/{backend.hpp,types.hpp,pico_ot_backend.h}`、`src/backend.cpp`、`src/c_api.cpp` | private device access contractを維持 |
| `src/runtime/current_firmware_adapter.cpp`、`aarch64_call_shim.S`、`src/internal/{decoder,clock_mapper}.*` | ABI/Build ID guard、SI変換、取得手順を維持 |
| `BackendTestApp/app/src/main/cpp/backend_test_jni.cpp`、Java MainActivity、Manifest | 既存Android常駐/取得/操作手順を維持。今回NDK wire変更不要 |
| `include/pico_ot_bridge/protocol.hpp`、`src/bridge/{protocol,publisher,receiver,clock_sync,udp_socket,monotonic_clock}.cpp` | POTBをBackend内部のHMD↔PC転送として残す |
| `tools/pc_bridge_service.cpp` | Phase Aで取得cacheにC1送信shim追加。Phase BでPICO受信providerに限定 |
| `outputs/steamvr/`、`tools/steamvr_calibrator.cpp`、`tools/steamvr_control_*.cs` | Task 4向け抽出bundleへ。Phase Bでruntime/buildから除去 |
| `service_contract.hpp`、`output_route_ipc.hpp`、`world_alignment_ipc.hpp` | HMD ingress以外のrouting/calibration契約をTask 4へ移管 |
| `integrations/monaka/`、`receiver_c_api.*`、`tools/monaka_smoke_publisher.cpp` | 旧Monaka consumer向けcompatibility。Task 5切替確認後に除去/旧互換package化 |
| `Start_All.cmd`、`Start_Bridge_Service.cmd`、出力GUI/build/register/calibration scripts | Phase Bで新Bridge起動へ移行案内。Backendが共通driverを所有しない構成へ |

## Phase A：provider追加・移動素材を確定（Task 4より前）

1. Task 1の`monaka-protocol-kit-v1.0.zip`と外部hash manifestを受け取り、C1/hash一致を確認する。`third_party/monaka-protocol/`とdependency lockへ取り込む。schemaをこのrepoで再定義しない。
2. PC側に`PicoObservationAdapter`と`ObservationPublisher`を追加する。private runtime型→C1への変換はこのrepoで完結。BridgeがPOTB/receiver C APIをdecodeしなくて済むようにする。
3. `Receiver`で新しく受理されたposeだけをC1へ変換する。既存`ToTrackerState()`はmapped clock値をobserved時刻へ入れ直しているので、その値をC1のfreshnessとして再使用しない。C1.timestampは`lastPoseReceivePcNs`から一度だけ自clockへ写し、clock resyncで変更しない。
4. Backend lifetimeごとのUUID sessionと永続source_idを導入する。POTB senderSession変更を検出したら観測sessionも更新し、旧device cacheとderivative履歴を破棄する。POTB uint32 sequenceのwrapは既存receiverに処理させ、C1側はU63の独立sequenceを使う。
5. DeviceStateの更新はpose timestamp/sequenceとは独立。startup connectedSnapshotは`presence=unknown`とし、live presenceの保証にしない。positionValid、orientationSamplePresent、packet freshnessを別に扱う。
6. C1をBridgeの29810へ送信。既存29765 ingressのbind ownerを一つに保つ。Phase Aは現在のserviceが唯一の受信ownerのままC1送信を追加する。新旧serviceを二重起動させない。
7. 旧29766/29767/29768 fan-out、既存GUI、SteamVR directはcompatibility機能として残す。これを最終分離完了と報告しない。新Bridgeとの同時試験では同じtrackerを旧driverと新driverから二重出力しない。
8. Task 4へ`pico-bridge-extraction.zip`を出力する。最低限`outputs/steamvr/`全体、`tools/steamvr_calibrator.cpp`、`tools/steamvr_control_*.cs`、routing/alignmentの上記headers、関連build/register/GUI/zero/calibration scripts、CMakeの関連target、旧config例、output関連docsを含める。必要includeをclosureとしてmanifest化する。PICO runtime実装は含めない。
9. bundleには各ファイルの元repo/path/base SHA/blob SHAとSHA256、既存license/noticeの扱い、移動先候補、旧型依存の置換点を記載する。Task 4が1repo workspaceとこのbundleだけで移植できる状態にする。SDK等の既存外部依存は必要版/取得法を明記し、存在しない同梱物を仮定しない。

## PICOからC1への正確なmapping

| 取得値 | C1 |
|---|---|
| full serial | `device_id`（runtimeDeviceId禁止） |
| `positionMeters` / `positionValid` | position / validity.position。falseの数値を追跡に使わない |
| `orientationXyzw` / `orientationSamplePresent` | saneならorientation / validity.orientation=true / evidence=`sample_sanity`。vendor保証とはしない |
| 両component利用可 / orientationだけ可 / 両方不可 | tracked / degraded / lost。raw state65はmappingしない |
| `linearVelocityMetersPerSec` | 既知frameで有効なときDerivative(frame=`space`,evidence=`measured`)。無効position時はnull |
| `angularVelocityRadPerSec` | frameは候補段階のためC1ではnull。raw候補をworldとして渡さない |
| `linearAccelerationMetersPerSec2` | filter/predictor候補でgravity/frame未確定のためC1ではnull |
| `batteryBucketRaw` | battery fraction=null。充電情報に根拠があるときchargingだけ設定可。raw bucketはBackend内部診断に保持 |
| `ReferenceFrame::PicoOutputA` | opaque convention=`pico-output-a-v1`、space.idはPICO map/sessionの設定ID。座標変換は実施しない |
| origin/map同一性を確認できない再起動 | space.revisionを変更し、Bridgeへ再calibration要求。静かに旧transformを適用しない |

## Phase B：互換確認後の旧責務除去（同じTaskを再開）

開始に必要なhandoff：Task 4のBridge HEAD/hash・Direct回帰結果、Task 5のMTP consumer HEAD/hash・C ABI不要の確認、旧config移行テスト結果。ハードウェア未確認でもコードの準備は行えるが、既存経路を置換するrelease gateは未完了として示す。

- 同じ作業branchを続行し、Phase Aを作り直さない。PICO PC providerはPOTB受信/clock/metadata→Observation送信に限定する。common route/GUI/driver/calibrationの旧targetを除去する。
- StartupはPICO Backend単体を起動可能にし、共通Bridgeは独立した成果物として起動する。任意の旧launcher互換は新Bridgeへ委譲する薄いものだけにする。
- Monaka C APIを使用するactive consumerが0件と確認してから旧integrationを外す。Backend C API自体とMonaka receiver C APIを混同しない。
- 最終treeにPICO固有のSteamVR implementationが残らず、新Bridgeのソースをコピーして二重保守しない。旧branchや履歴は残す。

## Compatibilityと既知制約

- ABI profile：`libtrackingclient.pxr.so` GNU Build ID `d1b55bc0598ffff93cc01eaf089793f8`、392-byte result。未知Build IDは既存guardで拒否する。
- 禁止取得経路（既存docs由来）を維持：`GetSwiftBattery(fullSerial,...)`、fake OpenXR lifecycle、manual SHM FD reset、raw readerからpair/mode/calibration変更。
- enumeration allocationの所有権不明によるbounded leak/refresh無効、stop/startで新規tracker discovery、5台のruntime依存検証、pollとsensor rateの相違は解決済としない。
- `docs/KNOWN_LIMITATIONS.md`の「SteamVR未実装」は古いBackend段階の記述で現コードと矛盾する。履歴条件を明記して現状へ更新する。

## Testsとbuild

- Unit：既存decoder/clock/protocol/receiver testsを維持。adapterのvalidity独立、unknown bucket、candidate derivative除外、session/sequence、clock resyncでage不変を追加。
- Build：`cmake -S . -B build -DPICO_OT_BUILD_TESTS=ON` → build → ctest。Windows既存driverはPhase Aだけ既存optionで回帰。Android test APKは既存scriptでbuildしABI境界を確認する。
- Mock：POTB fixture→PC provider→C1→Task 1 decoder。旧service単一bind、5/可変台数、packet重複/逆順/停止、metadataだけ継続、C1 receiver停止で旧経路が止まらないこと。
- Hardware：既存pinned firmwareで5台、遮蔽/復帰、電源断、Backend/HMD/PC再起動、Utility共存。Phase Bは新Bridge経由DirectとMTPの両経路を別々に比較する。未実施はNOT RUN。

## Definition of Done

Phase A：共通Observation provider、protocol lock、抽出bundle、回帰結果が揃う。`Phase A complete / legacy removal pending`と報告。
Phase B：取得機能を維持し、common output/calibration/Monaka receiver責務がactive treeから分離され、移動先で代替動作が確認できる。ここまでで`Backend separation complete`。旧branch削除はしない。

<!-- BEGIN COMMON C1 -->
