# Phase 2C-4S — PICO Admission Offline Readiness

実施日: 2026-10-15 UTC。判定:

**PASS — PICO ADMISSION OFFLINE READY**

**FINAL PICO REACQUIRE THRESHOLDS: NOT YET VALIDATED**

**PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED**

software-only の実装、検証、専用 branch への commit/push。実機、HMD、tracker、
ADB、実運用 receiver、SteamVR の操作は行っていない。Phase 2C-4A は未開始。
自然な PICO SLAM reacquire、実機 Q/P 品質、sleep/wake recovery の証明ではない。

## 1. 開始 snapshot と作業範囲

| 対象 | branch / ref | SHA / 状態 |
|---|---|---|
| Primary repository | `orzkwsk/PicoMotionTrackerBridge` | 以下の専用 branch のみ変更 |
| 開始時 local dev | `dev` | `e7d858007768b638b7b49fca18da647740ac508e` |
| 開始時 remote dev | `origin/dev` / remote live ref | `e7d858007768b638b7b49fca18da647740ac508e` |
| 凍結した phase-start SHA | 同上 | `e7d858007768b638b7b49fca18da647740ac508e` |
| 元 checkout（保持） | `fix/hmd-initial-tracker-discovery` | `66f0262a5399f15a1f3673fd204dc36553546647`; clean |
| 専用 branch | `feature/pico-reacquire-admission-offline-v2c4s` | phase-start SHA から新規 worktree 作成 |
| 実装・検証 commit | 同上 | `6e562be1ce521b1e30aa99bd04281e73613e1475` |
| MVIP P1 reference | `fix/capabilities-contract-reconciliation-v2c-p1` | `6dfa595fa308fea64880fb5f501073feed9a726a` |
| MBIP P1 reference | `fix/capabilities-contract-reconciliation-v2c-p1` | `3ea3194479cfa1a04b0fcbcf04916186efb4f9d3` |
| Core replay artifact source | `feature/phase2c3r-recovery-angular-dynamics` | `c73805da1a64d0b161775decd60e5632ea295a0f` |

`git fetch origin dev` と `git ls-remote` で actual remote を照合した。指定準備時
SHA との差はない。開始時の元 checkout は clean。作業先は workspace 配下
`task-work/phase2c4s`。元 checkout、frozen audit clone、protocol source は変更せず、
dev/main merge、history rewrite、force-push は実施していない。

添付 Library の正規取得は通常環境で失敗し、明示した同環境の取得先へ一度だけ
再試行したが Windows の `os.setxattr` 非対応で完了しなかった。回避処理や他環境の
path 流用は行わず、delegation message 内の省略なし全文を作業指示として使用した。
ローカル取得成功や存在・可読性確認済みとは報告しない。

開始記録: [phase-start.json](phase2c4s/phase-start.json)。

## 2. 実装と契約

PICO PC Observation 境界に `PicoReacquireAdmission` を追加。
`INVALID -> CANDIDATE -> ADMITTED` を実装し、raw invalid は CANDIDATE/
ADMITTED から直ちに INVALID に戻す。loss debounce は追加していない。
Core Fusion state を変更する API、Core tuning、shared schema の変更はない。

| config | default | 意味 |
|---|---|---|
| `enabled` | `false` | 明示的に true とするまで従来動作 |
| `reacquire_confirm_ms` | `0` | 連続 valid 確認時間。0 は時間条件なし |
| `reacquire_min_samples` | `0` | progressing な受信 sample 数。0 は個数条件なし |

両条件は AND。最初の progressing valid sample は elapsed=0/count=1。
invalid は timer/count を全 reset。両方 0 なら最初の sample を即時承認。
負数、非整数、overflow、型違い、未知 key は拒否する。enabled は boolean のみ。
service の `--admission-config JSON_PATH` で指定し、config を diagnostics に記録。
20 ms / 3 samples と 60 ms / 4 samples は **TEST-ONLY**、default ではない。

時間は immutable な PC receive monotonic timestamp を使用する。poll 時間・wall
clock・再同期後 device clock を使用しない。重複 timestamp は confirmation を
開始/進行させない。重複 sample sequence は receiver/replay wrapper が除外。
timestamp regression は失効し、high-water time を超える進行で再開する。
既存 staleTimeout（default 500 ms）を超える gap は候補を再開始する。
これは freshness policy の再利用であり、新たな position/Q 品質 threshold ではない。

native flags と canonical raw full validity を effective validity から分離した。
CANDIDATE では raw=true/effective=false。Gate ON の未承認 P/Q/derivatives は
protocol から除き、raw PosePacket は独立 diagnostic callback に保存する。
callback の障害はカウントし、gate を迂回しない。Gate OFF は既存の無効時 numeric
diagnostic fields を含む wire semantics と latest-per-Pump の送信単位を維持する。
Gate ON は受信 batch 内の invalid を見落とさないよう各受信 packet 後に処理する。

capabilities は全状態で P/Q/LV を維持し、`capability != current validity` を保持。
source/device/session/clock/space/convention/revision は Gate に変更権限を持たせない。
publisher/tracker/mapping identity は既存 Bridge/Core の所有のまま。独立した
sender-session lifecycle の既存 revision policy は維持する。Gate state は保存しない。

詳細仕様と再実行方法: [pico-admission.md](../pico-admission.md)。

## 3. Deterministic replay / synthetic scenarios

`pico_admission_replay` は production と同じ `ApplyPicoAdmission` を使用する
streaming JSONL runner。timestamp、raw validity、P/Q、任意 native state/速度、
identity/session/space を受け、状態、elapsed/count、metrics、理由、effective protocol
を出力する。各 fixture は同一入力を二度実行し、出力全体の一致を確認した。

| fixture | 確認内容 | 結果 |
|---|---|---|
| stable reacquire | INVALID/CANDIDATE/ADMITTED と即時 loss | PASS |
| 1-frame valid flicker | 非承認 | PASS |
| valid/invalid chatter | timer/count 全 reset | PASS |
| chatter then stable | stable 区間後のみ承認 | PASS |
| q/-q sign flip | 偽の大角度 step なし | PASS |
| quaternion jump then settle | jump を計測、未検証 threshold では拒否しない | PASS |
| position jump then settle | 同上 | PASS |
| prediction-like linear continuation | diagnostic coverage、prediction 判定は主張しない | PASS |
| timestamp gap | stale gap 後の再確認 | PASS |
| duplicate timestamp/sample | count 不増、0 除算/NaN なし、sequence 重複除外 | PASS |
| session/space invariants | 繰り返し loss/admit でも identity/revision 固定 | PASS |
| Gate disabled | 従来 raw/effective mapping、numeric diagnostics 互換 | PASS |

追加: 時間だけ/個数だけでは不承認、ゼロ条件、regressing time、nonfinite metric の
null 化、device 分離、独立 session 変更、process/adapter recreation、capture callback
障害時の fail closed、同一 Pump 内の invalid/valid burst を検証。
[scenario 記録](phase2c4s/synthetic-results.json)。

## 4. 既存 capture inventory / raw 保存

2C-3H の `root-observations.jsonl` 16 本を inventory 化した。旧 schema で mask の
観測 field がないものは masked count を null とし、0 と断定しない。
[inventory と SHA256](phase2c4s/capture-inventory.json)。

使用元は `fixed-root-controlled-02/root-observations.jsonl`、977 frames、SHA256
`aa0e1fd2385822d38f9029727f4b9987a02c562d14eb4c3df4f29e5f45d257bc`。
mask 中の usable raw PICO pose は **139 frames**。元記録で Core pose submitted=false
かつ Core diagnostic P/Q=null を確認した。既存 Bridge canonical mapping 後の P/Q で
あり native PICO space と読み替えていない。

再現可能な importer で 139 frames を fixture 化し、原値の P/Q を全件照合。
Gate ON/OFF replay、raw 保存、未承認 P/Q の protocol 不通過、決定性を確認した。
fixture SHA256 は `50705bc8ff567e921d9cb47510478615a922614f7db2b5e7653aae9f9d2d202a`。
[capture manifest](../../tests/fixtures/admission/capture_manifest.json)。

**この外部 mask capture は natural SLAM reacquire evidence ではない。**
139 frames の再利用で final reacquire/jump/velocity threshold は選定していない。

## 5. Protocol / Core integration

固定 P1 MVIP projection、MBIP STRICT normalization、実 codec を使用。
101 Observation projections、101 Observation normalizations、101 MTP encodes、
101 MTP normalizations が PASS。supported は維持し、validity のみを切り替える。
normalization 前後の wire 内容一致を検証し、session/identity、publisher/tracker、
space revision=42、mapping revision=17 を Gate 遷移で変更していない。

MTP は明示した synthetic fixture adapter で Observation admission 結果を
P1 fixture に写し、実 codec と normalizer を通した。実運用 Bridge の calibration/
transform/UDP chain を起動した検証ではない。P1/P1V H-02/H-03 は未解決のままで、
今回の same-session/same-space admission の表現・検証には blocking ではなかった。

Core は c73805da source の既存 `candidate-final-package/lib/core.jar` を使用。
SHA256 `ef37474a804ea0c3705fe556d21c97861fc4c1fd8feb1b622c42130c8f061863` を照合。
101 frames × 2 回の standalone file replay が完全一致。CANDIDATE 6 frames は
Core raw6dofValid/sixDofValid=false、raw P/Q=null。再承認 frame 54 で通常の
`RECOVERY_DWELL` に入り、その後 `RECOVERY_BLEND -> FULL_6DOF`。

Core state command、`6dof-valid` control、実 HIL mode は使用していない。
dwell=150 ms / blend=300 ms / fallbackBlend=150 ms と既存 2C-3H/2C-3R tuning を
保持。Core の source/semantics は変更していない。
[exact artifact / integration 結果](phase2c4s/protocol-core-integration.json)。

## 6. Issue #6 の software sanity

Issue #6 を変更・close していない。別 issue のまま維持。

- U1/U2: 既存 plain JVM lifecycle suite で desired ON 復元と explicit OFF 維持が PASS。
- U3: `MainActivity.backendState` と `BridgeLifecycle.state()` の独立性は source inspection。
  実 Android Activity lifecycle の実行検証は NOT RUN。
- U4: native publisher test の 20 回の explicit Start で session 再生成、sequence reset が PASS。
- U5: adapter/replay の新規 instance で candidate は再開。Gate と transport session/revision
  の非結合も integration で確認。

Java suite: **10 tests PASS**。実 HMD sleep/wake recovery は NOT RUN。
[lifecycle 結果](phase2c4s/lifecycle-results.json)。

## 7. 次回 HIL diagnostic / performance

machine-readable `pico_admission_sample` に raw/effective validity、state、candidate
start/elapsed/count、raw P/Q、position/Q step、dt、linear/angular derived speed、
native tracking-state byte、native flags/velocity/source timestamp、session、source/device、
clock、space/convention/revision、sequence、transition と admit/reset reason を用意。
MTP publisher/tracker/mapping provenance は既存 downstream capture の所有のまま。

step/速度は diagnostic-only。q/-q は同じ回転として計算。invalid/nonfinite 計算値は
null とし、production rejection threshold は追加していない。

Gate 本体は fixed-size、per-frame heap allocation/lock/history なし。device map は
既存 maxDevices（256）で有界。replay は line 65535 bytes/device 256 上限で streaming。
diagnostics は既存の bounded async queue（1024、critical reserve 128）を再使用し、
sample drop/writer failure を可視化する。追加の診断 JSON allocation は診断有効時のみ。
HIL の実時間性能 benchmark は NOT RUN。raw capture の完全性は次回も drop counter
で確認が必要。

## 8. 実行済み regression と未実行範囲

commit `6e562be1ce521b1e30aa99bd04281e73613e1475`、clean tree、MSVC 19.51 / C++17 /
Python 3.13.15 / Windows で `scripts/test_revision.ps1 -Mode Full` を実行:

| 実行項目 | 結果 |
|---|---|
| release tooling unittest | 13 PASS |
| 同 `python -O` | 13 PASS |
| hash-pinned v2 kit / historical v1 kit verify | PASS / PASS |
| CMake configure/build（host全target） | PASS |
| host CTest（新規 admission/replay 含む） | **13/13 PASS** |
| legacy receiver C API configure/build/CTest | **1/1 PASS** |
| replay Python suite | **5 tests PASS**（12 synthetic cases と 139-frame fixture を内包） |
| Java lifecycle | **10 tests PASS** |
| P1/P1V integration / Core replay | 上記各101件、Core101 × 2 PASS |
| `git diff --check` | PASS |

[Full summary](phase2c4s/software-regression.json)、[build artifact SHA/provenance](phase2c4s/artifact-hashes.json)。
初回制限環境の MSBuild が子プロセス起動で停止したため、その build tree のみ停止し、
単一 worker で成功させた。最終 committed build の `dirty_at_build=false`、embedded SHA
と executable sidecar SHA256 の一致も CTest で確認済み。

Core 1924 / Desktop 153 / Bridge 53 / trusted producer 22 の full suites は今回は
**NOT RERUN**。過去の PASS 数を再実行結果として報告しない。Core は上記 pinned
artifact に対する admission replay のみ。Android APK build/instrumentation、実機、
release packaging、新たな実 HIL、natural reacquire characterization は NOT RUN。

CI は **NOT CONFIRMED**。認証済み combined status は空だった。利用可能な
workflow lookup が PR-triggered runs 限定で、今回 PR は作成していない。未認証 Actions
API の 404 を CI 不在の証拠にはしない。local PASS と CI PASS を区別する。
[CI 観測記録](phase2c4s/ci-observation.json)。

## 9. Delivery / 次 phase

実装 commit は専用 branch に通常 push 済みで、remote branch HEAD と local HEAD の
一致を確認済み。本 report と evidence は後続の documentation commit で同じ branch に
push する。最終 delivery SHA / remote一致 / clean status は最終応答で記録する。
dev/main merge、force-push、Issue #6 の変更は行わない。

残る実測は natural PICO tracking loss/reacquire、raw Q/P、stable duration/sample 分布、
threshold tuning、real HIL effective validity/Core recovery の確認。
**Phase 2C-4A — PICO 6DoF Reacquire Admission Gate / Natural Reacquire Characterization**
はユーザーが実機利用可能と明示するまで開始しない。

**FINAL PICO REACQUIRE THRESHOLDS: NOT YET VALIDATED**

**PICO same-tracker IMU authenticity: NOT VERIFIED / DEFERRED**

**PASS — PICO ADMISSION OFFLINE READY**
