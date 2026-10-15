# 热点配置工具

双击本目录的[热点配置.cmd](热点配置.cmd)。启动只读检查，选1刷新、2自动更新、3手动配置、0退出；最终输入Y才写入。本目录按自身位置定位，不依赖当前工作目录。

| 文件 | 职责 |
| --- | --- |
| 热点配置.cmd | 系统Windows PowerShell 5.1启动入口 |
| wifi-menu.ps1 | 中文菜单、脱敏状态、确认与地址选择 |
| configure-wifi.ps1 | 共享热点读取、校验、备份和原子写入 |
| camera_debug_local.example.h | 可提交的虚构凭据模板 |
| camera_debug_local.h | 本机真实凭据，被Git忽略 |

自动入口要求热点开启且配置频段明确2.4GHz，多地址必须选择。手动配置隐藏输入密码，需自行确认实际2.4GHz；开放网络额外确认。自动确认后热点或接口信息变化时拒绝更新。

在Autodrive根目录执行：

```powershell
.\wifi\热点配置.cmd -StatusOnly
.\wifi\configure-wifi.ps1 -CheckOnly
.\wifi\configure-wifi.ps1 -Auto
.\wifi\configure-wifi.ps1 -Manual -ServerIP 192.168.137.1
```

手动IP须替换为当前机器的实际地址。命令行Auto/Manual是直接配置入口，不带菜单确认；需要确认交互时使用CMD菜单。只读检查不创建文件。

默认写入本目录的`camera_debug_local.h`。覆盖前备份到`../outputs/backups/wifi-config_<时间与随机标识>/`并核对哈希，失败保留旧文件；内容相同不写入、不生成备份。模板可复制为本机头文件后填写。真实头文件和备份包含明文凭据，不得提交Git。

固件仍从`car/config/camera_debug_config.h`接入本地头文件。工具不启停热点、不改变频段、不启动逐飞助手、不编译或刷写；更新本地文件不会改变车上已有固件，编译与刷写须另行授权。C13只控制车上Wi-Fi开关，不选择热点。

测试保留在`../car/tests/`，报告和备份保留在`../outputs/`。迁移前日志与验收报告中的旧路径是历史记录，不作为当前入口。多设备限制和连接验收见[本地环境](../docs/local-environment.md)。
