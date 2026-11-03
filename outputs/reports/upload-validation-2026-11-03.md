# 上传前排除项迁移及构建回归验证

用户授权：将此前建议排除的本机备份、批量帧图和完整过程 CSV 移至 E:/AA/car/ignore_restore，随后编译并运行回归。未修改算法、配置或测试断言，未烧录，未执行 Git add/commit/push。

## 迁移

244 个文件，约 96.90 MiB，按原项目相对路径移至 E:/AA/car/ignore_restore。每个文件移动前后 SHA-256 一致，项目中对应源文件已不存在。

逐文件恢复清单：E:/AA/car/ignore_restore/move-manifest-20261103.json。恢复时按 RelativePath 放回项目根目录，先检查同名文件，避免覆盖后续工作。

范围：outputs/backups/before-sync-20261102 内的文件；field-fix、single-mode、track-diagnosis、video1223-retest、video1438-audit、video1604-audit、video1723-review、vision-repair 八个 2026-11-03 分析目录中的 CSV，以及 all-数字、all-frames-数字、frame-数字命名的 PNG/JPG。关键图、JSON 摘要、日志、文字报告及正式测试 ZIP 保留。

历史报告中的全帧图和 CSV 相对路径现指向本机归档材料，不再随仓库分发；需完整复核时从上述清单查找。历史报告未重写。

## 编译与回归

- car/scripts/build.ps1：实际 Keil 全量构建，0 错误、0 警告。Code=153504、RO=13340、RW=300、ZI=135548 字节。
- car/scripts/test.ps1：19 项算法检查通过。
- 工程 unittest：170 项，首次 161 通过、3 失败、6 环境错误；6 个临时目录权限错误使用已有 check_build_tests.py 兼容入口逐项复测后全部通过。最终逐项汇总 167 通过、3 失败。
- 仿真 unittest：59 项，首次 51 通过、8 环境错误；权限兼容入口复测，校正 Windows PowerShell 模块路径并在沙箱外重试依赖下载后，8 项全部通过。最终逐项汇总 59 通过。此为原始全套加受影响项目复测的汇总，并非一次全套无错误运行。
- 工程剩余失败：test_real_gray_outer_quality（C 断言 line 83）、test_controller（C 断言 line 116）、test_sharp_turn_caps_speed_even_if_all_four_settings_match（924.662109375 > 850）。与此前 vision-repair-2026-11-03/final-host-tests.log 的三个失败名称及断言值一致，未放宽断言。
- 源码、头文件、配置、工程及仿真测试文件的前后 SHA-256 无变化。两份录像回归 ZIP 保留且实录回归执行通过。

运行日志位于 car/build/upload-validation-20261103/（不提交），包括 host-tests.log、simulation-tests.log、build-behavior-recheck.log、wifi-layout-recheck.log、screenshot-recheck.log、simulation-permissions-recheck.log、runtime-module-recheck.log、runtime-network-recheck.log。首次环境报错日志保留，不能单独作为最终结果。

## 固件

outputs/firmware 中 AXF/HEX/BIN 均与 car/build/objects 对应文件哈希一致。

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| autocar.axf | 1111456 | D30C3AB01435DEDFF494A9024D8A79C855E2DEC09028B698528CA97E1973B05E |
| autocar.bin | 173632 | 6BAEA72A83A4E39550D7348D5231D67B43886A88517596692CF9929847D523C5 |
| autocar.hex | 488425 | 4DF143E779FA45CC9B5779ADB40BFDC4E64F15ACF889D9EB8EE13DB15F089EEF |

NO_FLASH。编译与主机回归不代表实车验收；最新实车记录中的转向、环岛、避障和终点问题继续保留。此次构建使用本机 Wi-Fi 配置，公开分享固件前仍须核对内嵌网络配置；本报告不记录其值。

临时目录清理：自动审批拒绝递归清理，理由是部分目录带历史任务名称、缺少本轮归属证明。未执行该批量删除，回归生成的 tmp 缓存保留；迁移脚本已移除。归档最终复核244文件全部通过。
