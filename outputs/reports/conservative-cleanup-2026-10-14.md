# 第一档保守清理 — 2026-10-14

用户明确批准第一档保守清理。以执行前工作区为基线，仅清理原列出的17个Git垃圾临时对象、旧仿真构建缓存和20个旧测试目录。未改源码/参数，未提交Git、运行gc/prune或改写历史；未Keil重建、覆盖发布或下载缓存、刷写、通电或操作热点/车辆。

## 实际清理

| 对象 | 删除原文件体积MiB | 结果 |
| --- | ---: | --- |
| 原17个Git tmp_obj垃圾临时文件 | 298.20 | 按原精确名单及SHA-256核对删除 |
| 旧simulation/build缓存 | 17.20 | 清理旧73份来源清单及相关生成文件，保留当前重建缓存 |
| 20个旧测试目录 | 9.25 | 原内容按清单核对，暂存至本任务tmp再删除 |
| 新保留的8份当前缓存及8个索引 | -2.22 | 默认与11份活动预设共12个入口，8个独立DLL版本 |

删除原材料共324.64MiB，扣除保留的当前缓存后净减少322.41MiB；最终净释放还扣除本报告、证据包及日志增量，见收尾数字。按文件逻辑大小统计，不等同NTFS实际分配空间。

调查至执行之间新增的`.git/objects/da/tmp_obj_isq3gM`（约18.49MiB）未列入原17个批准目标，本次保留。清理后Git统计仍有这1个垃圾对象，不宣称Git垃圾全部清空；正常对象count274、in-pack15318、pack3均保持原统计，索引、HEAD、引用及工作区状态保持。

## 验证与保护

- 清理前后`git fsck --full --no-progress`均退出0；记录中的dangling对象不作为损坏，也未删除正常不可达对象。
- 55项仿真完整回归通过。第一次隔离副本缺少清空环境变量时的默认来源副本，导致1项布置错误；补齐11份相同源码后重跑55项通过，没有改断言或产品代码。
- 当前默认与11份活动预设均已重建并加载；300个10ms控制周期启动检查距离0.391166m，无故障。这是仿真检查，不是实车验收。
- 部署到原项目后，12个入口均在禁止调用编译器的检查中加载现有DLL；独立进程禁用编译器发现后默认缓存加载通过。
- 18,841份其余原文件逐文件SHA-256不变，包括全部源码、配置、依赖、完整GCC、历史备份/报告/证据、simulation/runs、原始图片/视频、发布与验证固件、tmp/build-autocar、wifi本机配置以及未清理的field-robustness材料。
- Git HEAD/index/config/packed-refs以及refs/logs的基线文件哈希不变；记录的原工作区修改与未跟踪状态不变。只新增本任务报告和证据，另按流程追加日志。
- 17项文件最初带ReadOnly属性，普通删除被文件属性阻止；核实属性及内容后，对原名单使用-Force完成，没有修改系统ACL。
- 旧测试目录和旧DLL缓存先核对精确源/目标绝对路径及重解析点，移动到本任务暂存目录；递归删除只发生在本任务tmp内。

## 原Git删除清单

- `.git/objects/02/tmp_obj_4bstS5`：31.75MiB
- `.git/objects/02/tmp_obj_8oiD1l`：44.18MiB
- `.git/objects/02/tmp_obj_bUnnrf`：43.25MiB
- `.git/objects/02/tmp_obj_CK1Fly`：39.22MiB
- `.git/objects/02/tmp_obj_gNXPS0`：21.02MiB
- `.git/objects/02/tmp_obj_rMY2w7`：21.82MiB
- `.git/objects/02/tmp_obj_x3VEqW`：36.21MiB
- `.git/objects/02/tmp_obj_xj6hdA`：33.58MiB
- `.git/objects/07/tmp_obj_lGmaSk`：8.77MiB
- `.git/objects/47/tmp_obj_yHQZcL`：0.00MiB
- `.git/objects/ae/tmp_obj_p1Dq2U`：0.00MiB
- `.git/objects/cf/tmp_obj_bwU53c`：0.00MiB
- `.git/objects/d8/tmp_obj_W5KedH`：0.00MiB
- `.git/objects/da/tmp_obj_LYbdbG`：3.49MiB
- `.git/objects/da/tmp_obj_sKbjpS`：14.90MiB
- `.git/objects/f3/tmp_obj_HavVBI`：0.00MiB
- `.git/objects/fe/tmp_obj_e0lD2f`：0.00MiB

## 原测试目录删除清单

- `tmp/autodrive`：0.41MiB
- `tmp/autodrive-motor`：0.33MiB
- `tmp/autodrive-pid`：0.07MiB
- `tmp/autodrive-vision`：0.08MiB
- `tmp/build-script-tests`：1.14MiB
- `tmp/camera-debug-tests`：4.69MiB
- `tmp/camera-realignment`：0.00MiB
- `tmp/control-tuning`：0.18MiB
- `tmp/curve-outer-tests`：0.15MiB
- `tmp/encoder-feedback-tests`：0.13MiB
- `tmp/motor-control-tests`：0.60MiB
- `tmp/registry-tests`：0.00MiB
- `tmp/remote-command-tests`：0.06MiB
- `tmp/remote-control-tests`：0.13MiB
- `tmp/remote-integration-tests`：0.08MiB
- `tmp/remote-motor-tests`：0.29MiB
- `tmp/remote-servo-tests`：0.39MiB
- `tmp/servo-control-tests`：0.39MiB
- `tmp/wifi-layout`：0.00MiB
- `tmp/wifi-spi-tests`：0.12MiB

## 交付与后续

[清理证据包](conservative-cleanup-evidence-2026-10-14.zip)：11项，包含精确目标与保护哈希、前后Git检查、成功/失败回归日志、保留缓存与部署检查。逐项SHA-256及ZIP CRC通过，包SHA-256 `97dbd6934bf655662860ff248e1f2dc808331611ac8cc7fb0802f7adf865c17a`。没有备份已批准删除的垃圾或可重建缓存，避免以新副本抵消清理收益。

完整GCC、simulation/runs、tmp/figs原始素材、历史恢复点及验证档案保持。不实施第一档补充、工具链外置、历史档案外置或轻量发行包。后续若清理新Git对象、运行记录或旧证据，须另列精确范围审核。

现有默认/活动预设缓存已预热；未来改动C源码、编译器或C参数仍可能需要编译新DLL，已有匹配缓存可按原逻辑加载。未更改缓存签名算法、发现优先级或供应商代码。

收尾完成：本任务临时目录已删除；扣除新缓存、报告、证据包和日志后，实测净释放约321.56MiB，项目从2.552GiB降至2.238GiB。数字按文件逻辑大小统计。清理后再核对18,841份保护对象（日志原字节不变且仅追加）、Git基线元数据、17个垃圾目标和20个测试目录确切缺失、证据包CRC/SHA-256，均通过。
