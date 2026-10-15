# Autodrive 精简与整改方案（2026-10-15）

依据你的四项反馈与上一轮扫描，本方案的目标是**提高精简性与可读性**：减少不必要的文件与执行步骤、消除规则与现实的冲突、统一命名。
本轮仅出方案，不改动任何文件。下方带 ⚑ 的项需要你确认；执行时 git 的 add/commit/push 全部由你完成，我只做工作树改动。

## 决策记录（2026-10-15，已定稿）

| 事项 | 决定 |
|---|---|
| 执行方式 | 只出方案，等用户下令后再执行（当前：**先别执行**） |
| AGENTS.md | **精简重构**（A2 草案全文替换，规则下放 docs） |
| logs/ | **收敛为每日一份**：只留 `YYYY-MM-DD.log`；被文档引用的证据日志归档进 `outputs/reports/<task>-<date>/` 并更新引用；一次性过程日志删除 |
| 大体积备份 | **直接删除**（>5MB，共 286 MB） |
| 未使用文件 | **Layer 1 + Layer 2 + Layer 3 全部纳入删除范围**（见 H 节） |
| git | add/commit/push 由用户本人执行；我暂存与提交皆不做 |

---

## 0. 问题归因（一句话）

AGENTS.md 把太多"过程性规则"写进了静态入口，并且要求"每步复述审核 + 每步备份 + 每任务临时目录 + 每次运行都留日志"，于是**每次小改动都会生成一批备份 zip、过程日志和临时目录**；同时它又和 `tmp/build-autocar`、根目录 git 仓库两处现实冲突。精简的抓手是：**把过程规则的默认值从"留痕"改成"用完即弃，靠 git 兜底"**。

---

## A. AGENTS.md 精简重构（核心）

### A1. 现状问题
- 58 行里，第 6–22 行的"工作方式 + 必要约束"与 `docs/standards/workflow.md`、`commands.md`、`files.md` 大面积重复；
- 第 24–47 行"导航"列了 21 条链接，与 `docs/README.md` 重复，却**漏掉当前主文档 `docs/autonomous-driving.md`**；
- 第 49–58 行"仿真与多设备协作"把链接与策略混在一段；第 58 行是带日期的进展流水；
- 第 15 行"不创建根目录 Git 仓库"与现实的 `Smart-Car.git` 矛盾；第 54 行引用的 `simulation/vendor` 算法副本已不存在。

### A2. 目标草案（拟直接替换 AGENTS.md 全文，约 22 行）

```markdown
# 项目协作入口

东南大学第二十一届智能车校赛备赛，主线为逐飞 RT1064 摄像头智能车，主工程 `car/autocar.uvprojx`。
背景、规则、硬件与阶段不在本文件展开，见下方导航。

## 工作方式
- 新任务先复述目标、范围、涉及路径与验证方法，经用户批准后执行；计划内步骤连续执行、不再逐步请示，范围变更时重新提交。
- 区分已核实事实、工程选择与待验证假设；编译通过不等于实车验证，未确认引脚/极性/方向前不宣称实车可用。
- 只读来源（本目录 `参考/`、同级 `SeekFree/`）不修改；`car/` 为唯一算法维护位置。

## 目录与产物
- 构建缓存 `car/build/`（不提交）；构建入口 `car/scripts/build.ps1`。
- 任务临时区 `tmp/`（任务结束清空）；每日日志 `logs/YYYY-MM-DD.log`；交付物 `outputs/{firmware,reports,backups}/`。
- 版本控制由用户本人执行，不自动 `add/commit/push`；撤销依赖 git 历史，不再逐次生成整包备份。

## 导航（必读）
- 总导航与接手顺序：[docs/README.md](docs/README.md)
- 当前阶段（C15 自动驾驶/视觉/PID）：[docs/autonomous-driving.md](docs/autonomous-driving.md)
- 流程与命令：[docs/standards/workflow.md](docs/standards/workflow.md) · [docs/standards/commands.md](docs/standards/commands.md)
- 其余文档、标准、例程索引、硬件记录统一由 docs/README.md 分发，不在本文件重复。
```

### A3. 配套（去重与减压，让"保守"消失）
- **审核粒度**：从"每步复述审核"改为"**方案级批准 → 计划内连续执行 → 结束汇报**"。同步修订 `docs/standards/workflow.md` 的"开始任务"段，删除与 AGENTS.md 重复的条文，只保留流程细节。
- **备份策略**：默认**不再**对每次改动生成 `outputs/backups/*.zip`；以 git 提交作为撤销手段。仅当用户明确要求或涉及不可再生数据时才存档。同步修订 `docs/standards/files.md`、`workflow.md` "交付与收尾"。
- **日志策略**：`logs/` 只保留**每日 `YYYY-MM-DD.log`**（见 C 节）；单次构建/测试过程输出不再入库。

---

## B. 解决 `tmp/build-autocar` 与"tmp 应清空"的冲突

**冲突本质**：`build.ps1` 把编译中间件与缓存写进 `tmp/build-autocar/`（持久缓存，已被 .gitignore 忽略），而 AGENTS.md/`project-overview.md` 规定 `tmp/` 任务结束清空。两者不可兼得。

**方案（推荐）**：把构建缓存从 `tmp/` 迁到**独立、已忽略、语义清晰**的位置 `car/build/`，`tmp/` 恢复"纯临时、可清空"。

需要同步改动的点（执行时逐一核对）：

| 文件 | 改动 |
|---|---|
| `car/scripts/build.ps1` | `$buildPath`：`tmp\build-autocar` → `car\build` |
| `.gitignore` | 增加 `/car/build/`（并保留忽略 `keil-build*.log` 归档） |
| `docs/environment-and-build.md` | 第 125–131、135、145 行对 `tmp/build-autocar/` 的说明与"保留/清理"表述 |
| `docs/autonomous-driving.md` | 第 140 行"下载缓存 tmp/build-autocar/objects"表述 |
| `docs/standards/commands.md` | 若涉及构建缓存的清理示例 |

> 备选：若不改脚本，则须在 AGENTS.md 为 `tmp/build-autocar/` 显式开一个"持久构建缓存"例外。方案更推荐前者的"路径迁移"，因它让规则回到自洽。

---

## C. logs/ 命名规范 + 历史重命名

### C1. 现状
- 共 184 个：11 个每日 `YYYY-MM-DD.log`（规范），173 个单次运行日志；后者里 **91 个无时间戳**（如 `autonomous-build.log`）、**82 个带时间戳**（如 `keil-build-20261013-102840-166.log`），格式还不统一。
- `logs/` **未被 gitignore**（119 个已跟踪），且 **54 个日志文件名被 docs 引用**（`docs/environment-and-build.md`、`autonomous-driving.md`、`known-issues.md` 等）。

### C2. 目标命名规范
- 每日任务日志：`logs/YYYY-MM-DD.log`（**保持不变**）。
- 单次运行/构建/测试的**长期留档**日志：`logs/YYYY-MM-DD_HHMMSS-<slug>.log`（日期与时间，连字符主题，全小写）。
- 一次性过程输出：**不进 logs/**，写入 `tmp/<task>/` 或 `car/build/`，任务结束丢弃。

### C3. 历史重命名方案（执行时给你一张映射表）
1. 11 个每日日志不动。
2. 82 个已带时间戳的 → 规范为 `YYYY-MM-DD_HHMMSS-<slug>.log`。
3. 91 个无时间戳的 → 时间取自 `git log -1 --format=%cI`（无则用文件 mtime），slug 保留原语义。
4. **先做减法再改名**：对被任意 doc 引用的 54 个（多为证据链）→ 重命名并**同步更新引用**；对无引用、且被当日 `.log` 覆盖的一次性过程日志 → 直接删除或移入 `outputs/reports/<task>-<date>/`。

> ⚑ 取舍提示：重命名 173 个文件会改动 54 处文档链接并产生大量"重命名"提交。若你更看重**精简**而非保留每一条过程日志，可改用更省事的方案 D（logs 收敛为"每日一份 + 证据进 outputs"）。请在批准时二选一。

---

## D. 文件清理清单（你已授权：缓存/临时、冗余过程日志、大体积备份）

### D1. 缓存与临时残留（删除，合计约 110 MB）

| 目标 | 说明 / 体积 |
|---|---|
| `car/tests/__pycache__/` | Python 字节码缓存 |
| `simulation/__pycache__/` | 同上 |
| `car/.vscode/uv4.log.lock` | 0 字节锁文件 |
| `car/.vscode/keil-assistant.log` | 编辑器日志 |
| `car/autocar.uvguix.Lynn`、`car/autocar.uvguix.Sumi` | 个人窗口布局（已忽略） |
| `car/library/版本文件在doc文件夹下.txt` | 0 字节占位 |
| `tmp/figs/` | 约 93 MB，含两个 mp4（72 MB + 19 MB） |
| `tmp/build-autocar/` | 约 11 MB（随 B 节迁走后删除） |
| `tmp/field-robustness-2026-10-13/` | 约 0.3 MB |
| `tmp/` 收尾 | 清空后仅留 `.gitkeep` / 空目录 |

### D2. 冗余过程日志（logs/）
- 2026-10-13 的 53 个 `*-build/-tests/-ui.log` 中，**无文档引用**且被 `logs/2026-10-13.log` 摘要覆盖的部分 → 删除；有引用的保留并改名（见 C3）。
- 精确删除清单在执行时以"是否被 docs 引用"脚本生成，先给你过目再删。

### D3. 大体积备份（>5 MB，合计 286 MB）
建议**移出工作区离线归档**（而非直接删，以防需要），或按你的意愿直接删除；git 已覆盖其中大部分改动：

```
outputs/reports/field-robustness-validation-20261013.zip   226 MB
outputs/backups/element-fault-validation-20261013.zip       66.8 MB
outputs/backups/car_before-config250_20261013_124244.zip    47.6 MB
outputs/backups/car_before-qt-gpu_20261013_132705.zip       43.9 MB
outputs/backups/autocar_before-field-robustness_20261013.zip 41.7 MB
outputs/backups/control-tuning-iterations_20261012.zip      24.5 MB
outputs/backups/car_before-element-fault_20261013_111734.zip 18.9 MB
outputs/backups/car_before-element-speed_20261013_105100.zip 18.8 MB
outputs/backups/car_before-field-speed_20261013_102722.zip   12.2 MB
outputs/backups/config250-validation-20261013.zip            11.9 MB
```

### D4. 未纳入（按你的选择保留）
- `SmartCar-21AIVision-main/` 里的逐飞库副本、`car/library` 与 `SeekFree` 的重复库存 → **保留不动**。
- 若后续想再瘦身，可随时补做（参考/采购类文档去重、`simulation/docs/archive` 原始聊天移出源码树）。

**D 节预计释放：约 110（缓存/临时）+ 286（大备份）+ 若干日志 ≈ 400 MB+。**

---

## E. 需要同步修改的文档（去重与对齐）

| 文件 | 改动 |
|---|---|
| `AGENTS.md` | 全量替换为 A2 草案 |
| `docs/standards/workflow.md` | 删除与 AGENTS 重复的审核条文；默认放弃"每次改动整包备份"；日志改为"每日一份" |
| `docs/standards/files.md` | 更新 `tmp/`、`car/build/`、`outputs/backups/` 的职责与保留策略 |
| `docs/standards/commands.md` | 构建缓存路径与清理示例同步 |
| `docs/project-overview.md` | 目录职责表：`tmp/` 说明、新增 `car/build/` |
| `docs/environment-and-build.md` | 构建产物表（125–131、135、145 行）改为 `car/build/` |
| `docs/README.md` | 补 `autonomous-driving.md` 已在其中；核对与 AGENTS 的唯一入口关系 |
| 引用被改名日志的各 doc | 更新 54 处链接（若选 C3） |

---

## F. 执行顺序与提交建议（push 由你完成）

1. **A**：替换 AGENTS.md + 同步 standards/docs（纯文本改动，最安全）。
2. **B**：迁移构建缓存路径 + 改 build.ps1/.gitignore/docs，并跑一次构建验证仍能出固件。
3. **D1**：删除缓存与临时残留（无争议）。
4. **D3**：离线归档或删除大备份。
5. **C/D2**：日志重命名 + 更新引用（改动最大，单独一次提交）。

建议你按主题分批提交，便于"有意义的历史"：`chore: 精简AGENTS与标准` / `chore: 构建缓存迁至car/build` / `chore: 清理缓存与临时残留` / `chore: 归档大体积备份` / `refactor: 统一日志命名`。

> 注：`logs/` 当前未忽略、119 个已跟踪。日志重命名会产生较多 rename 记录；若希望历史更干净，附件方案 D 的"每日一份"收敛更优。

---

## G. 风险与待确认

1. **日志改名 vs 收敛**（C3 vs 备选 D）——需你二选一。⚑
2. **大备份是删除还是离线移出**——需你确认。⚑
3. 构建缓存迁移后需实跑一次 Keil 构建确认路径无误（本机有 Keil 时）。
4. AGENTS.md 是"契约文件"，其改动请你逐句过目后再定稿。

请就 G1、G2 给一个选择，其余我按上表执行；执行阶段不做任何 git 提交/推送。

---

# H. 未使用文件清查（2026-10-15 补充）

用"是否被编译/被其它文件引用"筛了一遍全工程。结论分三层：**Layer 1 是真正的死代码**，**Layer 2 是第三方依赖里未编译的部分（慎删）**，**Layer 3 是故意保留的历史归档**（当前工作用不到，但有留档价值）。请按层勾选是否纳入删除范围。

## Layer 1 — 确认未被引用的自有文件（低风险）

| 文件 | 判定 |
|---|---|
| `simulation/cli.py` | 全工程无任何 import/脚本调用（`launch.ps1` 只跑 `start_app.py`）；功能与 `simulation/tools/snapshot.py`、`validate_control.py` 重叠，疑似被取代 |
| `simulation/assets/image2.png`、`stop-reference-2~5.png`、`ui-before-status.png`、`ui-before-tabs.png`、`中期检查要求.txt` | 无任何代码或文档按名引用（`stop-reference-1`、`image1` 有引用） |

> 这两项删除前建议你扫一眼确认（尤其 `cli.py` 是否还想留作手动命令行入口）。

## Layer 2 — 第三方依赖中"从未被编译"的代码（⚠️ 可删但建议保留）

Keil 工程共引用 365 个文件；`car/library` 下 **497 个 `.c/.h` 里有 64 个 `.c` 从未被加入工程（不参与编译）**，`.c` 合计约 2.4 MB。集中在：

| 位置 | 未编译 `.c` | 说明 |
|---|---:|---|
| `car/library/components/usb/`（host / templates / dcd，共 3.0 MB） | 31 | 工程只用 USB device-CDC，**USB host 栈与各 class 模板全未用** |
| `car/library/sdk/CMSIS/Driver/DriverTemplates/` | 12 | 官方"模板"文件，设计上就不编译 |
| `car/library/sdk/components/serial_manager/` | 6 | 串口管理器未接入 |
| `car/library/components/fatfs/source/fsl_{ram,nand,mmc,usb}_disk/` | 4 | 只用 SD 盘后端 |
| `car/library/sdk/{utilities,cmsis_drivers,drives,board,components/osa}/` | 11 | `fsl_str/shell/notifier/sbrk`、`*_cmsis`、`romapi`、`pin_mux` 等 |

**为什么建议保留**：这是 vendored 的 SDK/厂商库，不是本工程代码；删它违反项目自己的"保留第三方原布局与许可"规则，且会给将来重新引入 SDK、或在 Keil 里重载工程带来隐性坑（`c_cpp_properties.json` 的 includePath 也引用了这些目录，需同步改）。
**若你坚持精简**：最低风险的做法是只删**与摄像头车完全无关的整块**——`components/usb/host/**` 与 `usb/templates/**`、`CMSIS/Driver/DriverTemplates/**`——保留 sdk 其余部分。

## Layer 3 — 历史归档/证据（当前工作用不到，但按规则"故意保留"）

| 位置 | 规模 | 备注 |
|---|---|---|
| `simulation/tools/history/` | 17 个文件 | 文档明确标注"历史工具，不属于当前验收入口" |
| `simulation/docs/archive/` | 10 个文件（含 `chat_messages.json` 59 KB、`本聊天记录.md` 56 KB） | 2026-10-11~12 阶段证据 |
| `simulation/configs/archive/` | 2 个 JSON | 历史参数 |
| `outputs/reports/simulation-history-2026-10-14/` | 148 个文件 / 9.2 MB | 一次性迁移归档 |
| `参考/镜头组例程.zip` | 3.19 MB | V3.5.5 旧 demo，功能被 `SeekFree/…/Example` 覆盖 |
| `simulation/assets/measurements/`（8 张实拍图 + manifest） | 7 MB | README 声明"字节一致持久保存"的证据 |

> 这一层都**不影响构建与运行**，删了不报错，但属于"可追溯证据"。是否精简交给你的取舍：**要极致精简就删；要留证就保留**。

## Layer 4 — 同级目录（未纳入）

- `SmartCar-21AIVision-main/`：仅被 `docs/autonomous-driving.md` 引用一次，本质是另一组的参考工程。你之前选择**保留**，故不列入。

## 预计收益（仅 Layer 1+2 的保守组合）

- Layer 1：约 15 个文件、< 1 MB，但消除"貌似有入口"的困惑。
- Layer 2（仅删 USB host/templates + CMSIS 模板）：约 43 个 `.c` + 对应头文件、**约 3.3 MB**，且不碰车端实际功能。
- Layer 3（若全删）：约 190 个文件、**约 20 MB**。

> 请在下方选择要把哪几层并入删除范围；Layer 2 若选"删"我默认只删 USB host/templates + CMSIS 模板这两个整块。
