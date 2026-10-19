# ttp_vqf

TTPlayer 5.7.9 兼容的 x86 TwinVQ/VQF Reader 插件。恢复原工厂、Reader 和元数据 ABI，使用固定的 FFmpeg 9.0.2 最小静态后端；无需额外安装 VC 运行库或 tvqdec.dll。

## 构建

```powershell
./build.ps1 -Package -SourcePackage
```

需要 VS 2026 C++ Win32 工具、CMake、Python 3.12+、Git for Windows。源码和 GNU make 构建工具自动从官方来源下载并校验 SHA-256。默认使用本项目的 `build` 缓存，独立构建不依赖播放器、其他插件或私有测试目录。

- `build/Release/ttp_vqf.dll`：XP SP3／Win7／Windows 10/11 共用，要求 SSE2。
- `ttp_vqf-YYYY.MM.DD.zip`：仅 DLL 与 SHA256SUMS。
- `ttp_vqf-YYYY.MM.DD-source.zip`：独立对应源码附件，包含本项目、完整固定 FFmpeg 源码、兼容修改、GNU make 源码、许可证和重建入口。
- Action 的 Release a Version 使用北京时间日期与同日 pN 版本；自动构建、校验 XP/Win7 导入并打包，不运行或上传私有测试。

源码附件内 `third_party/ffmpeg` 会自动作为源目录使用，可修改后重新构建。VC-LTL／YY-Thunks 仍按固定摘要获取。版权和完整许可证保存在仓库与源码附件，不嵌入 DLL。

[重建依据、差异和验证](docs/IMPLEMENTATION_AND_VALIDATION.md) · [依赖及修改记录](docs/DEPENDENCIES.md)

本项目恢复宿主行为和可维护解码实现；现代解码核心与原外部闭源库不保证 PCM 位精确相等。具体已验证范围和剩余边界见文档。
