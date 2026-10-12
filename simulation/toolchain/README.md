# 独立 GCC 工具链

已部署 WinLibs GCC 14.3.0 / MinGW-w64 12.0.0，Windows x86-64、UCRT、POSIX线程模型。完整工具链位于 `mingw64`，包含编译器、链接器、头文件、库及发行包配套工具，不依赖 Vivado，也没有修改系统 PATH 或安装系统级软件。

来源：[WinLibs 发行页](https://github.com/brechtsanders/winlibs_mingw/releases/tag/14.3.0posix-12.0.0-ucrt-r1)。下载包已与 GitHub 发行元数据中的 SHA-256 校验一致，校验值和来源记录在 `deployment.json`。解压内容约935.8 MiB。

从 simulation 目录检查：

```powershell
& .\toolchain\mingw64\bin\gcc.exe --version
& .\toolchain\mingw64\bin\gcc.exe -dumpmachine
```

预期目标为 `x86_64-w64-mingw32`。程序已有的工具查找逻辑会优先选取此目录；环境变量 `SIMULATION_CC` 如被显式设置则优先使用指定工具。

工具链发行标识和编译选项已纳入算法 DLL 缓存标识，避免本次更换工具链后沿用旧编译器的 DLL。原 DLL 保留。默认新 DLL 标识为 `c26fe24193056923`。

验证记录在 `../reports/independent-gcc-check.json`：完整赛道双向各7000步，直道和圆弧各1000步，共16004次结果对比，22项数值输出和4×120路径识别行均与原默认 DLL 完全一致。新控制参数重新编译、缓存无需编译器加载、启动界面加载新 DLL 也已验证。对比结果只覆盖这些测试输入，不代表所有参数或设备均已验证。

工具链使用 UCRT；本次在当前64位Windows设备上通过编译和加载验证，尚未在另一台电脑上验收。移动软件时应携带整个 `toolchain` 目录，包括 `deployment.json` 和配套库。许可证原文保存在 `licenses`，发行包内原有许可证亦保留。
