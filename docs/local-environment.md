# 多设备本地配置

共享仓库维护算法、配置模板、脚本和主要验收证据。热点凭据、工具路径、外部参数路径与运行缓存仅在本机保存。当前固件使用编译期网络配置，换热点后需重新编译并按单独授权烧录。

## 热点与逐飞助手

在项目根目录执行`car/scripts/configure-wifi.ps1 -CheckOnly`只读取热点状态、脱敏凭据长度、候选地址和TCP监听，不创建报告或改写文件。

`-Auto`通过Windows WinRT读取热点SSID、密码、配置频段，生成`car/config/camera_debug_local.h`。PowerShell 7自动通过Windows PowerShell 5.1读取WinRT。优先从活动Wi-Fi Direct接口读取IPv4；多个候选必须显式指定`-ServerIP`或`-InterfaceIndex`，其他系统可尝试ICS关联。设置频段不等同对实际无线信道的测量。必须确认2.4GHz；5GHz配置拒绝生成。

`-Manual -ServerIP 192.168.137.1`隐藏输入密码，也可使用`-Ssid`及SecureString `-Password`。SSID必须1至32个可打印ASCII字节，密码为空或至少8字节，二者合计最多59字节。IP必须为实际可访问的IPv4；默认服务器端口8080、模块源端口0。C字符串正确转义，校验失败保留旧配置，相同内容不改写。脚本不启停热点或操作车辆。

手工配置可复制`car/config/camera_debug_local.example.h`为本地头文件后填写。共享`camera_debug_config.h`不保存真实凭据。测试使用显式虚构宏。旧Git历史和旧固件可能仍含历史凭据，本次没有改写历史。

助手选择TCP Server，监听热点IPv4或0.0.0.0及8080端口；车辆目标不能使用0.0.0.0。监听检查不证明进程为助手，也不证明已收图。实车验收须有TCP建立及实际188×120图像；烧录、通电和实车操作另行授权。

## 工具路径

Keil：显式`build.ps1 -KeilPath`优先，其次`AUTOCAR_KEIL`、`car/scripts/tool_paths.local.json`中的`Keil`、PATH、固定盘Keil_v5布局。该JSON也可用`Gcc`键指定测试编译器。

主机测试：`AUTOCAR_HOST_CC`优先，随后独立GCC、PATH和有限已知安装布局。`test.ps1 -Compiler`支持显式路径。仿真用`SIMULATION_CC`；启动解释器用`SIMULATION_PYTHON`。编辑器用`AUTOCAR_ARM_CC`指定armclang，项目内include路径使用工作区变量。

当前机器验证解释器Python3.13.0，NumPy2.5.0、Pillow12.3.0，与requirements.txt一致；Tk由Python安装提供。依赖不自动安装。其他机器须先检查64位解释器、Tk和Windows x64 GCC。

## Git与交付

忽略本地凭据、工具链二进制、DLL缓存、运行输出、临时目录和编辑器日志，取消跟踪不删除本机文件，不缩小旧Git历史。工具链版本、下载SHA-256及许可证保留。正式固件、报告和原始实测素材保留来源身份。

本地热点头文件虽不提交，编译出的AXF/BIN/HEX仍可能内嵌SSID和密码。共享前必须检查固件；2026-10-13维护提交未加入本机更新的三份固件，它们仍保留在工作区，已有Git历史中的固件和凭据未被清除。修改前备份与私有维护证据包也不加入本次提交。
