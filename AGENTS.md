# 项目协作入口

东南大学第二十一届智能车校赛备赛，主线为逐飞 RT1064（MIMXRT1064DVL6B）摄像头智能车，主工程 `car/autocar.uvprojx`。背景、规则、硬件与阶段见下方导航，不在本文件展开。

## 工作方式

- 新任务先复述目标、范围、涉及路径与验证方法，经用户批准后执行；计划内步骤连续执行、不再逐步请示，范围变更时重新提交审核。
- 区分已核实事实、工程选择与待验证假设；编译通过不等于实车验证，未确认引脚、电源极性、舵机中位与电机方向前不宣称实车可用。
- 只读来源（本目录 `参考/`、同级 `SeekFree/`）不修改；`car/` 是唯一的算法维护位置，不在别处维护副本。

## 目录与产物

- 主工程 `car/`；构建缓存 `car/build/`（不提交）；构建入口 `car/scripts/build.ps1`。
- 任务临时区 `tmp/`（任务结束清空）；日志 `logs/YYYY-MM-DD.log`（每日一份）；交付物 `outputs/{firmware,reports,backups}/`。
- 版本控制由用户本人执行，不自动 `add/commit/push`；撤销依赖 git 历史，不再逐次生成整包备份。

## 导航（必读）

- 总导航与接手顺序：[docs/README.md](docs/README.md)
- 当前阶段（C15 自动驾驶/视觉/PID）：[docs/autonomous-driving.md](docs/autonomous-driving.md)
- 流程与命令：[docs/standards/workflow.md](docs/standards/workflow.md) · [docs/standards/commands.md](docs/standards/commands.md)
- 其余说明、标准、例程索引与硬件记录统一由 docs/README.md 分发，不在本文件重复。
