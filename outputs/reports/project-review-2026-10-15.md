# Autodrive 工程扫描评审（2026-10-15）

范围：`C:\Users\Lynn\Desktop\Files\Autodrive`，附带核对同级 `SeekFree/` 与 `SmartCar-21AIVision-main/`。
方法：目录清单、体积统计、内容哈希（md5）比对、git 工作树状态检查、AGENTS.md 链接与锚点核对。只读分析，未改动任何文件。

---

## 一、工程构成总览

| 目录 | 文件数 | 体积 | 性质 |
|---|---:|---:|---|
| `simulation/` | 16,375 | 1001 MB | 主机端仿真（含 **toolchain 953 MB**、runs 38 MB） |
| `outputs/` | 1,408 | 669 MB | 交付物（backups 378 MB、reports 289 MB） |
| `tmp/` | 370 | 104 MB | 任务临时区（**未清理**） |
| `car/` | 631 | 53 MB | 主工程（library 17 MB、tests 夹具约 34 MB） |
| `参考/` | 10 | 35 MB | 原始资料（已被 git 跟踪） |
| `logs/` | 184 | 1.3 MB | 每日 + 每次运行日志 |
| `docs/` | 29 | 440 KB | 说明/标准/例程索引 |
| `wifi/` | 6 | 28 KB | 热点与图传配置 |

工作树合计 **约 1.9 GB**（不含 `.git`）；`.git` 另有 **466 MB**。真实"源码级"内容（`car/src|include|config`、`simulation/*.py`、`docs/`）其实只有几 MB，磁盘的 95% 以上来自工具链、备份与运行产物。

---

## 二、冗余与多余文件

### 1. 逐飞开源库被存了 3 份（最典型的重复）
- `car/library/` 共 519 个文件，其中 **517 个与 `SeekFree/RT1064_Library/SeekFree_RT1064_Opensource_Library` 逐字节相同**（md5 一致，约 11 MB）。
- `SmartCar-21AIVision-main/` 下还有第三份 `SeekFree_RT1064_Opensource_Library`（约 19 MB，是视觉组的另一分支，558 个唯一哈希里只有 5 个与逐飞原版相同）。
- `car/library` 作为"工程自包含的依赖副本"有其正当性（构建不依赖外部目录），但**同一份库同时以三份实体存在于工作区**是明显的空间与版本漂移风险点。建议：`car/library` 保留，`SmartCar-21AIVision-main` 若只是归档参考则移出工作区或压缩。

### 2. AGENTS.md 已失效的整节内容
- `simulation/vendor/` 现在**只有 2 个文件**（`README.md` + `UPSTREAM_LICENSE`，共 36 KB），AGENTS.md 第 54 行"不重新维护 `simulation/vendor` 算法副本"所指的"算法副本"**已经不存在**。规则描述的是一个被删掉的对象。

### 3. tmp/ 未按规则清理（规则要求任务结束清空仅留空目录）
- `tmp/` 现存 104 MB / 370 文件，其中 `tmp/figs/` 93 MB（含两个 mp4：`VID_20261013_101115_HDR10PLUS.mp4` 72 MB、`a4176ea8...mp4` 19 MB），`tmp/build-autocar/` 11 MB（`.o`、`.lst` 构建中间件）。这些按项目自己的约定（AGENTS.md 第 18–19 行）本应删除。

### 4. outputs/ 备份堆积
- `outputs/backups/` 378 MB / 1005 文件，含 30+ 个 `car_before-*` / `autocar_before-*` 快照 zip，其中单文件 `field-robustness-validation-20261013.zip` 达 **226 MB**、`element-fault-validation-20261013.zip` 66.8 MB、`car_before-config250` 47.6 MB。
- 根目录已有 git 仓库做版本控制，这些"改动前整包快照"与 git 历史高度重叠，属可压缩/可离线归档内容。

### 5. 缓存与 IDE 遗留物
- `car/tests/__pycache__/`（含 `*.cpython-312.pyc`）、`simulation/__pycache__/`。
- `car/.vscode/keil-assistant.log`、`car/.vscode/uv4.log.lock`（0 字节锁文件）。
- `car/autocar.uvguix.Lynn`、`car/autocar.uvguix.Sumi`（186 KB 个人窗口布局，已被 .gitignore 覆盖但仍在盘上）。
- 空文件 `car/library/版本文件在doc文件夹下.txt`（0 字节占位）。
- 上述在 `outputs/backups/before-portability_20261012_232252/` 备份副本里又被复制了一份。

### 6. logs/ 中大量一次性过程日志
- 184 个日志中，**仅 2026-10-13 一天就有 53 个** `*-build.log` / `*-tests.log` / `*-ui.log`，多为逐次构建/测试的过程输出。而 `logs/2026-10-13.log`（14 KB）已是当天任务的结构化总结。这些细粒度过程日志与在跑日志、当日总结三层信息冗余，建议归档到 `outputs/reports/` 或直接清理。

### 7. 参考/ 内部重叠
- 三份目的重叠的采购类文档：`购买建议.docx`（7.8 MB）、`器件租借与购买建议清单.xlsx`（0.49 MB）、`板卡套件链接_2026.9.19.docx`（0.55 MB）。
- `镜头组例程.zip`（3.19 MB，V3.5.5 库的旧 demo）与 `SeekFree/RT1064_Library/Example` 内容功能重合。
- 两份 0.01 MB 的极小占位文档（`逐飞开源库资料与学习说明(见内部链接).docx`、`各组别罚时细则.docx`）。

### 8. simulation/docs/archive/ 中的原始聊天记录
- `chat_messages.json`（59 KB）+ `本聊天记录.md`（56 KB）把原始对话存进仓库，与同目录已整理归档的 `.md` 说明重复；原始聊天属于"过程证据"，更适合放 outputs 而非随源码树维护。

### 9. car/tests/fixtures 大体积二进制夹具
- `r50a45-sequences.zip` 25 MB、`element-sequences.zip` 8 MB、`geometric-path-sequences.zip` 0.4 MB 等，是已跟踪的测试数据。若可复现生成则不必入库；否则至少应确认是否所有人都需要。

---

## 三、Git 工作树状态问题（比文件冗余更值得先处理）

仓库 `origin = git@github.com:BadSaker/Smart-Car.git`，HEAD `a77bd0b`，但工作树与已提交状态严重脱节：

- **857 个文件显示为 modified**，其中 **约 621 个仅因换行符差异**（工作树是 CRLF；仓库**没有 `.gitattributes`**，`core.autocrlf` 也未设置）。用 `git diff --ignore-cr-at-eol` 过滤后只剩 236 个真实变更。
- **155 个已跟踪文件显示为 deleted**：多为 `simulation/reports/*`、`simulation/backups/*`——它们在 10-14 的"工程规范化"里被移到 `outputs/`，但删除**尚未 commit**。
- **149 个 untracked**：64 个在 `logs/`、51 个在 `outputs/`、17 个在 `car/`（新增测试与 fixtures）。

结果：`git status` 现在几乎不可读，既掩盖了真实改动，也意味着"规范化"工作未落库。建议优先级：加 `.gitattributes`（如 `* text=auto`，二进制类型标记 `-text`）→ 分主题提交（换行归一化、删除迁移、新增测试/报告各成一批）。

---

## 四、AGENTS.md 设计的不合理之处

AGENTS.md 本身只有 58 行，属于"必要的精简"，但也正因精简而与 docs 大面积重叠、且夹带了会过期的信息。具体问题：

**1. 与实际状态直接矛盾（事实性错误）**
第 15 行"不创建根目录 Git 仓库，不修改来源中的嵌套 Git"——根目录**已存在** git 仓库（`Smart-Car.git`，跟踪 1157 个文件，含 `参考/`）。而"不修改原始资料"与"把 `参考/` 纳入 git 跟踪"也相互拉扯。契约文件里的硬约束若与现实冲突，会持续误导接手者。

**2. 引用了已不存在的对象**
第 54 行"不重新维护 `simulation/vendor` 算法副本"——`vendor/` 里已无算法副本（见二·2）。规则指向空对象，读者会困惑。

**3. 把"带日期的进展"写进静态契约**
第 58 行"2026-10-12至13日的多设备配置入口整理已完成并有独立授权记录；该历史授权不扩展为后续任务许可"——这是**带时间戳的进度流水**，属于 `logs/` 或 `docs/README.md` 的职责。契约文件一旦承载会滚动更新的进展，就必然需要频繁改写，违背"入口稳定"的初衷。

**4. 关键文档缺席导航**
导航列了 21–25 条链接，却**漏掉当前阶段最重要的 `docs/autonomous-driving.md`**（234 行，C15 自动驾驶主指南，`docs/README.md` 与 `project-overview.md` 都把它当第一入口）。同时 `docs/archive-sync.md` 与整个 `docs/examples/*`（12 个例程索引页）也未在 AGENTS.md 出现。入口反而指向了次级文档。

**5. 导航职责与 docs/README.md 重复（双份维护）**
AGENTS.md 的"## 导航"与 `docs/README.md` 的"标准与资料"表高度重叠（两者各自 20+ 条链接、指向同一批文件）。同一组链接维护在两处，任何一处改名都会失配。更稳的做法是 AGENTS.md 只保留"必读三四个入口 + 指向 docs/README.md"，其余交给 docs 自己维护。

**6. 规则正文与 standards 文档重复（三处写同一件事）**
"复述-审核-执行"流程同时出现在 AGENTS.md 第 6–7 行和 `docs/standards/workflow.md` "开始任务"；`tmp/` 清理与绝对路径校验同时出现在 AGENTS.md 第 18–20 行和 `workflow.md` 交付收尾 + `commands.md`；`outputs/{firmware,reports,backups}` 职责在 AGENTS.md 第 20 行、`workflow.md`、`project-overview.md` 目录职责表三处各写一遍。**多处真相源 = 漂移风险**，改一处忘一处。

**7. 用锚点做"冻结边界"引用太脆**
第 33 行把冻结约束指向 `docs/seekfree-assistant-debugging.md#屏幕与wi-fi冻结约束`（该锚点当前存在）。但锚点不随文件重命名自动更新，链接检查器只验文件是否存在、不验锚点，标题一改就静默失效。建议给冻结内容独立文件或用稳定 id。

**8. 两段导航结构割裂、格式不齐**
"## 导航"（24–47）与"## 仿真与多设备协作"（49–58）功能相同却分列两节，后者又把"链接"和"策略规则"混在同一串 bullet 里；"导航"节内部还有一处孤立空行和一个无项目符号的孤行（第 30 行）。作为"入口文件"，结构一致性直接影响可扫读性。

**9. 未声明工作区全局**
AGENTS.md 把 `参考/` 与 `SeekFree/` 并列成"原始资料来源"，但 `SeekFree/` 实际在 `Autodrive` **之外的同级目录**（`docs/README.md` 说得准确，AGENTS.md 表述含糊），且完全没提同级的 `SmartCar-21AIVision-main/`。只看 AGENTS.md 的接手者无法得知工作区还有第二个参考工程树。

**10. CLAUDE.md 的说明性注释**
`CLAUDE.md` 用 `@AGENTS.md` 导入并在注释里解释"为什么需要这层间接"（版本/开关兼容）。这个"兼容性说明"属于维护备忘，放在入口文件里属于轻微噪声（可移入 docs 或日志）。

---

## 五、建议处理顺序（供你勾选）

1. **先修 git**：加 `.gitattributes` 归一换行，分批提交"换行归一化 / 删除迁移 / 新增测试报告"，让 `git status` 恢复可用。
2. **清 tmp/**：删除 `tmp/figs/`、`tmp/build-autocar/`，保留空 `tmp/`（符合既有规则）。
3. **归档 logs/**：把 10-13 的 53 个过程日志移入 `outputs/reports/` 或直接删除，只留每日 `.log`。
4. **瘦身 outputs/backups/**：226 MB 的 zip、>40 MB 的多个快照识别为可离线归档或删除（有 git 兜底）。
5. **修 AGENTS.md**：改为"必读入口 + 指向 docs/README.md"，删掉带日期的进展句、修正 git 与 vendor 两处事实错误、补上 `autonomous-driving.md`。
6. **处理三份库副本**：确认 `SmartCar-21AIVision-main/` 是否需要留在工作区。
7. **收尾**：`参考/` 采购类文档去重、`simulation/docs/archive` 原始聊天移出源码树、清理 `__pycache__` 与 `uv4.log.lock`。

---

*本评审仅作分析，未执行任何删除或修改。第 4、5 项涉及规则文件与版本库，建议单独授权后再处理。*
