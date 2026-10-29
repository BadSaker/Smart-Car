# 仿真与实车边界诊断独立评审（2026-10-28）

范围：只读检查 `probe_reality_gap.py`、原桥接/模型、真实 `servo_control.c` / `autonomous_control.c` / `main.c` / `autonomous_drive.c` 及现有 JSON/CSV 证据；未运行新闭环、未改 car/固件或诊断实现。以下行号对应评审时文件。

## 发现（按严重性）

### P2（已关闭）：丢弃旧帧后，通用 stop_evidence 仍把未提交给 C 的图像标为新鲜、可接受

位置：`simulation/tools/probe_reality_gap.py:160–168,221`，关联 `simulation/engine.py::_frame/path_diagnostic/capture_frame/latch_stop`。

`vision()` 在 age-aware 且 age>150 时直接返回，C 的 `frame_ms` 正确保留在最后消费帧；但调用它的 `_frame()` 仍增加 Python `frame_id`，将 `frame_time_ms` 写为当前 now，并保留本次新渲染图像。`sim.result()` 因而混合了旧 C 视觉结果与被丢弃图像的时标。

现成证据：`delay-aware/result.json` 的 10150ms 停车为摄像头超时，`first_bad.frame_age_ms=160`；同一文件 `stop_evidence.frame_time_ms=10150`，`stop_evidence.path.frame_fresh=true`，`accepted=true`。`last-frame.png` 也是最后渲染的被拒绝图像，不能当作故障前最后被 C 处理的图像。C 超时结论仍成立，但这部分输出不可作为一致的因果证据。

建议：诊断输出明确分开渲染/提交/消费帧身份，或先在报告排除上述通用 stop_evidence 图像新鲜度、接受状态、帧号和末帧图像的因果用途。核验应以 C `frame_ms` 与控制器 fault 为准。无需修改 car。

复核关闭依据：修正后的工具独立保存 `consumed.sequence/stamp/image`，只在实际调用 C vision 后推进；逐拍断言 C 帧龄等于当前时刻减消费帧时标。故障摘要和 path 按消费帧重写，另外保留 rendered 身份；`last-frame.png` 写最后消费图，`last-rendered-frame.png` 写最后渲染图。年龄门限改为读取编译后的 C 宏。

只读核验 `delay-aware-reviewed/result.json`：10150ms 仍报摄像头超时，消费帧501、时间9990ms、帧龄160ms，`frame_fresh=false` / `accepted=false`；渲染帧509、时间10150ms。新旧1015条CSV逐行比较12个时间、控制、运动与C帧龄字段，共12180项，差异0；全部新CSV消费帧龄关系成立。现用脚本与该次工作区快照SHA256相同（DBB6245D6C70A23E03573FEE601F134DC43CF20CF1606528ED2F0403A5CD94C2）。未运行新试验。旧 `delay-aware` 保留为原始证据，仍不能使用其错误的通用帧身份字段。

## 必须保留的边界（不判为本轮因果结论错误）

- `probe_service()` 直接复用真实 `servo_control.c`，但重写了非抢占的服务确认。没有编译执行 `autonomous_control.c` 或 `main.c`，因此不能称完整主循环/中断真实复现。当前无重启、无异步抢占情境下，服务代次与服务龄的简化足以验证所述对照。
- `--main-loop-ms=60` 表达每60ms一次有效服务及图像提交机会。真实 main 每圈多次调用 `app_service_controls()`（图像前后、捕获后、发送返回后），因此不能把此参数直接解释为实测“一圈耗时60ms”。结论应使用“有效服务间隔/帧提交间隔假设”。服务10/40/60ms、响应常数、模型电机参数均无板上测量。
- 原始实验PWM桩的 `d*2` 是50Hz、PWM_DUTY_MAX=10000下的名义脉宽；旧列 `pwm_effective_pulse_us` 不是引脚实测或完整驱动仿真。修正工具已将其改名 `pwm_nominal_pulse_us`，从当前driver头读取PWM_DUTY_MAX，按频率宏换算，并使平台桩哈希进入编译签名。真实 `zf_driver_pwm.c:112–117` 还经过65535刻度转换，SDK再次按计数周期取整，且驱动选择整PWM周期重载（`:157`附近）。因此本试验包含应用层整数脉宽/占空比量化，未覆盖所有硬件量化与更新时间。
- `service_age_ms` 在 `sim.tick()` 后采集，而 `probe_service()` 在 `autonomous_drive_tick()` 后执行；服务边界上的遥测龄可为0，但该次C控制输入看到的是此前服务龄。它可用于观察服务节奏，不能不加说明地称每次控制检查所用的龄。暂停导致故障时没有新有效服务，110ms故障证据不受影响。
- 160ms图像延迟对照只有在该延迟表示“CSI完成时间至主循环复制/提交之间的旧帧积压”时才对应 main 的年龄规则；曝光/成像到CSI完成的延迟不等同于该时标。持续CSI采集通常提供更新完成帧，不能仅由主循环变慢断言固定积压160ms。该试验是时标语义对照，不证明现场已有该积压。
- `pause-ideal` / `delay-ideal` 只运行至12s，`fault=无`，但 `pass_run=false`（未完成整圈）；只能写注入观察窗内未报错，不能写通过整圈验收。
- `main40` 完成虚拟位置终点，但 `pass_run=false`，有4个10ms撤PWM采样、最低速度约159.29mm/s、Q余量-45；应写“完成位置目标，但连续行驶验收失败”。
- `main60-low` 现有证据仅AB方向128.78s通过，不能写双向通过。诊断到首次停车即结束，没有完整故障后两秒采集窗口；修正结果已明确标注，旧通用窗口数值不代表实际完整导出。

## 核心结论核验

1. 原 `native_bridge.c` 每10ms直接设 `servo_service_ms=now` / 当前run_generation，确实掩盖独立主循环服务停止。这是已核实的模拟边界遗漏。
2. 真实舵机 `servo_advance_motion()` 到期后只走一次步长，随后把最后步进时刻设为now，不补走遗漏步数。当前配置20us/20ms，假设有效服务间隔60ms时最多20us/60ms；原模型独立按20ms更新。降低有效服务频率会变慢是代码事实。
3. hardware10对照通过；hardware60在33.17s PATH_LOST，main60在18.97s PATH_LOST；frame40（25fps、舵机服务10ms）40.8s通过，servo40（50fps、舵机服务40ms）33.09s PATH_LOST，支持此模型、此速度档、此方向下的服务频率敏感性。不能据此给出实车故障时间、临界周期，或唯一现场根因。
4. 150ms暂停中，真实服务确认分支在10.10s、服务龄110ms报舵机超时；理想分支在12s观察窗内无fault。符合100ms门限严格大于比较和10ms控制采样。此对照没有证明实车PIT在所有阻塞下都能正常运行。
5. 默认motor_gain=1.25、deadzone=15，模型满1000‰对应空载设定速度1231.25mm/s；不是实测空载转速，也不是所有轮胎耦合瞬态的数学上界。它明显低于2500mm/s单轮guard，说明默认工况没有代表性地覆盖实车可能的高速边界。gain3.75整圈通过，仅说明该受控参数下未触发故障；不能归因为“增益提高导致超速”，更不能称已验证现场超速原因或保护的全部边界。

结论：P2在修正工具及 `delay-aware-reviewed` 证据中已关闭；未发现推翻上述有条件服务频率/时标对照的实现错误。交付继续区分新旧证据语义、模型边界与现场事实，修改主工程仍需另行批准。
