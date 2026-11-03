# 排除项恢复及重新编译回归

用户要求恢复上轮迁出的排除项，并重新编译、回归验证。已按 E:/AA/car/ignore_restore/move-manifest-20261103.json 将244个文件（约96.90 MiB）复制回项目原路径。写入前检查路径边界、重解析点、归档SHA-256和目标冲突，未发现冲突；恢复后244项哈希全部一致。归档及清单保留，未删除归档。

恢复范围仅为清单记录的 outputs/backups/before-sync-20261102 文件及八个分析目录中的CSV、批量帧图。备份内容恢复到备份目录，不覆盖当前算法。上轮报告中已迁出的图片/CSV现已恢复，旧迁移报告作为历史记录保留，以本报告描述的恢复状态为准。未修改.gitignore。

## 验证结果

| 项目 | 结果 |
|---|---|
| Keil真实全量编译 | 0错误、0警告，Code153504/RO13340/RW300/ZI135548字节 |
| 原生图像处理/PID算法检查 | 19/19通过 |
| 工程主机回归 | 一次完整170项，167通过、3失败、0环境错误；223.585秒 |
| 仿真回归 | 一次完整59项全部通过；37.327秒 |
| 源码、配置及测试文件前后哈希 | 无变化 |
| AXF/HEX/BIN | 与本轮构建缓存及上轮发布固件哈希一致 |

三个工程失败与恢复前及历史最终回归的名称和断言值一致：test_real_gray_outer_quality（C line83）、test_controller（C line116）、test_sharp_turn_caps_speed_even_if_all_four_settings_match（924.662109375 > 850）。未放宽或绕过断言，不宣称工程全套通过。

本轮使用项目内simulation/python/python.exe；car/build/restore-validation-20261103/run_suite.py复用既有Windows临时目录继承权限办法，以标准unittest discover分别执行car/tests与simulation/tests。该运行器仅调整测试运行环境及Windows PowerShell模块路径，不修改生产代码或测试逻辑。

完整日志与恢复核对结果位于 car/build/restore-validation-20261103/：restore-result.json、build-console.log、algorithm-tests.log、host-tests.log、simulation-tests.log、source-before.csv、firmware.json。该目录属于不提交的构建缓存。本轮未尝试再次删除前轮被审批拒绝的tmp目录。

## 发布固件

| 文件 | SHA-256 |
|---|---|
| autocar.axf | D30C3AB01435DEDFF494A9024D8A79C855E2DEC09028B698528CA97E1973B05E |
| autocar.bin | 6BAEA72A83A4E39550D7348D5231D67B43886A88517596692CF9929847D523C5 |
| autocar.hex | 4DF143E779FA45CC9B5779ADB40BFDC4E64F15ACF889D9EB8EE13DB15F089EEF |

NO_FLASH。未执行Git add/commit/push。主机验证与编译不代表实车验收，现有转向、环岛、避障及终点问题状态不因本轮恢复而改变。
