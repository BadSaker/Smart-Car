# 多设备本地配置

共享仓库维护算法、配置模板、脚本和主要验收证据。热点凭据、工具路径、外部参数路径与运行缓存仅在本机保存。当前固件使用编译期网络配置，换热点后需重新编译并按单独授权烧录。

## 热点与逐飞助手

### 双击入口

双击`wifi/`目录的[热点配置.cmd](../wifi/热点配置.cmd)，进入中文交互菜单。热点专用文件的职责见[wifi说明](../wifi/README.md)。启动时只读检查；选1刷新，选2自动读取本机移动热点并更新，选3手动输入，选0退出。无需管理员权限、Python或额外依赖，使用系统Windows PowerShell 5.1；入口按自身位置定位项目，不依赖当前工作目录。执行策略Bypass仅对启动的PowerShell进程生效，不更改系统策略。

自动更新要求热点已开启且配置频段明确为2.4GHz。多个IPv4候选按接口名、索引和地址列出，必须显式选择；没有候选可手动输入实际地址。5GHz或未知频段不自动写入。手动配置前自行确认实际2.4GHz，密码隐藏输入，端口默认8080。开放网络额外确认风险；最终输入Y才写入，其他输入取消。自动模式确认后再次核对热点凭据、状态、频段和接口地址，有变化则拒绝写入并要求重新读取。

覆盖前备份旧头文件到`outputs/backups/wifi-config_<时间与随机标识>/camera_debug_local.h`，核对SHA-256后原子替换；备份失败、校验失败或取消操作均保留旧配置。内容相同不写文件、不创建备份。新建配置没有旧文件可备份。头文件和备份包含明文凭据，只能保留本机，均被Git忽略；控制台不显示密码。

入口只更新`wifi/camera_debug_local.h`，不启停Windows热点、不设置频段、不启动逐飞助手、不自动编译或刷机。更新不会改变车上已有固件，重新编译和刷写仍需单独授权。TCP监听存在也不能证明进程为逐飞助手或已经收到图像。

仅检查并退出，可在项目根目录执行：

```powershell
.\wifi\热点配置.cmd -StatusOnly
```

菜单实现为`wifi/wifi-menu.ps1`，底层复用同目录`configure-wifi.ps1`的结构化状态和配置写入函数。旧位置不保留重复入口；替换现有配置时同样创建本地备份，可通过`-BackupRoot`指定备份根目录。

### 命令行入口

在项目根目录执行`wifi/configure-wifi.ps1 -CheckOnly`只读取热点状态、脱敏凭据长度、候选地址和TCP监听，不创建报告或改写文件。

`-Auto`通过Windows WinRT读取热点SSID、密码、配置频段，生成`wifi/camera_debug_local.h`。PowerShell 7自动通过Windows PowerShell 5.1读取WinRT。优先从活动Wi-Fi Direct接口读取IPv4；多个候选必须显式指定`-ServerIP`或`-InterfaceIndex`，其他系统可尝试ICS关联。设置频段不等同对实际无线信道的测量。必须确认2.4GHz；5GHz配置拒绝生成。

`-Manual -ServerIP 192.168.137.1`隐藏输入密码，也可使用`-Ssid`及SecureString `-Password`。SSID必须1至32个可打印ASCII字节，密码为空或至少8字节，二者合计最多59字节。IP必须为实际可访问的IPv4；默认服务器端口8080、模块源端口0。C字符串正确转义，校验失败保留旧配置，相同内容不改写。脚本不启停热点或操作车辆。

手工配置可复制`wifi/camera_debug_local.example.h`为同目录本地头文件后填写。共享`car/config/camera_debug_config.h`不保存真实凭据，只通过相对路径接入。本地头文件内容在目录迁移中保持字节一致，旧位置不留副本；旧Git历史和旧固件可能仍含历史凭据，本次没有改写历史。

助手选择TCP Server，监听热点IPv4或0.0.0.0及8080端口；车辆目标不能使用0.0.0.0。监听检查不证明进程为助手，也不证明已收图。实车验收须有TCP建立及实际188×120图像；烧录、通电和实车操作另行授权。

## 工具路径

接手时先确认项目根目录为Autodrive，可从任意目录调用`car/scripts/check_project_layout.ps1 -ProjectRoot <Autodrive路径>`只读检查；它不替代读取AGENTS.md。仿真本机启动/工具发现诊断统一在`simulation/runtime/`，任务内验证可设置`SIMULATION_RUNTIME_ROOT`；发现缓存不是共享工具路径配置。

Keil：显式`build.ps1 -KeilPath`优先，其次`AUTOCAR_KEIL`、`car/scripts/tool_paths.local.json`中的`Keil`、PATH、固定盘Keil_v5布局。该JSON也可用`Gcc`键指定测试编译器。

主机测试：`AUTOCAR_HOST_CC`优先，随后独立GCC、PATH和有限已知安装布局。`test.ps1 -Compiler`支持显式路径。仿真用`SIMULATION_CC`；Python固定为`simulation/python/`，`SIMULATION_PYTHON`已停用，启动时不再发现外部解释器。编辑器用`AUTOCAR_ARM_CC`指定armclang，项目内include路径使用工作区变量。

2026-10-28仿真改用项目内完整CPython 3.13.18 x64、NumPy 2.5.0、Pillow 12.3.0及Tk/Tcl，脱离D盘Anaconda。首次部署执行`powershell -NoProfile -ExecutionPolicy Bypass -File simulation/setup_python.ps1`；版本、来源及哈希由`simulation/python-runtime.json`和`requirements.lock`锁定。日常测试通过`simulation/run_python.ps1`运行，详见[仿真说明](../simulation/README.md)。运行时二进制与下载缓存不提交Git；初始化不修改系统Python、PATH或注册表。启动环境缺失会报错，不自动下载或回退。Windows x64 GCC仍按原仿真工具链配置处理。

## Git与交付

忽略本地凭据、工具链二进制、DLL缓存、运行输出、临时目录和编辑器日志，取消跟踪不删除本机文件，不缩小旧Git历史。工具链版本、下载SHA-256及许可证保留。正式固件、报告和原始实测素材保留来源身份。

本地热点头文件虽不提交，编译出的AXF/BIN/HEX仍可能内嵌SSID和密码。共享前必须检查固件；2026-10-13维护提交未加入本机更新的三份固件，它们仍保留在工作区，已有Git历史中的固件和凭据未被清除。修改前备份与私有维护证据包也不加入本次提交。
