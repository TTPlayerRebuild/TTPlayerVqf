# Windows Action 的 FFmpeg 路径转换修正

日期：2026-10-20。

## 报错与原因

对应失败任务：[TTPlayerVqf / 39329177157](https://github.com/TTPlayerRebuild/TTPlayerVqf/actions/runs/39329177157)。
配置已识别 MSVC 19.51、VC-LTL 和 Python，失败位置是 `build_ffmpeg.py` 调用 GNU make 的
`clean` 目标。CMake 的 `execute_process` 报错只是该子进程失败的传递。

原脚本为让 Windows 原生 GNU make 读取 MSYS 路径，对 Makefile 和 `ffbuild/config.mak`
执行了 26 次不带边界的字符串替换：

```python
content = content.replace('/' + drive + '/', drive.upper() + ':/')
```

这会把普通单字母子目录也视为盘符。GitHub Runner 使用 `D:/a/仓库/仓库`：

| 输入 | 原脚本结果 |
| --- | --- |
| `D:/a/TTPlayerVqf/TTPlayerVqf/...` | `D:A:/TTPlayerVqf/TTPlayerVqf/...` |
| `/d/a/TTPlayerVqf/TTPlayerVqf/...` | `/dA:/TTPlayerVqf/TTPlayerVqf/...` |

因此 GNU make 在读取顶层 `include .../Makefile` 时即失败，退出码为 2；尚未进入真正的清理或编译。
此前本机源码路径没有独立 `/a/` 子目录，未触发这条边界。

## 修复

- 仅转换处于路径开头的 `/盘符/`，保留路径内部的单字母目录。
- 已有 `D:/...`、Makefile 变量路径、相对路径及 URL 不被修改，转换可重复执行。
- 重新配置前移除构建目录中生成的顶层 Makefile，让 configure 重建。FFmpeg 的规则为
  `test -e Makefile || echo "include $source_path/Makefile" > Makefile`，只修正转换函数不能
  修复上次失败遗留的 Makefile；已在原失败目录中覆盖这一恢复路径。源目录与构建目录相同则拒绝操作。
- VQF、RM、TAK 的独立构建脚本均修正同一缺陷；各工程仍可单独检出、构建。
- GNU make 自举、FFmpeg configure、clean、build 失败时，输出对应日志的最后 60 行和完整日志路径。
- 三个 Action 在失败时上传 CMake 配置日志、FFmpeg 日志、生成的 Makefile/config.mak。
  失败日志附件保留 7 天；运行发行包内容不变，私有测试不会进入 Action。
- 保留强制检查 `make clean` 的退出码，没有通过忽略失败继续构建。

## 本地验证方法

私有证据位于 `rebuild/tests/vqf_action_20261020`。

1. 建立 `a/TTPlayerVqf/TTPlayerVqf` 形式的测试目录，并通过含空格的 Git for Windows 路径执行构建。
2. 用原脚本运行真正的 GNU make 自举和 FFmpeg configure，稳定复现 `clean` 退出 2；
   底层日志明确显示被改坏的 `...20261020A:/TTPlayerVqf/.../Makefile` 路径。
3. 三工程共用的私有回归覆盖原生与 MSYS 盘符、嵌套单字母目录、Makefile 变量、引号、相对路径、
   URL、重复转换、真实 GNU make clean，以及失败时保留并显示底层日志；三组测试均通过。
4. 三个工作流通过 Actionlint。没有运行远程 Action 或创建 Release。

## 完整构建结果

在同一失败目录运行 `build.ps1 -Package -SourcePackage -PackageVersion 2026.10.20`，
保留旧 GNU make 与 FFmpeg 源码缓存，修复后成功重新配置、clean、编译、链接并生成两个 ZIP。
本机为 Windows 11，MSVC 19.51.36260，Python 3.14.7；Git 通过含空格的目录访问。
VC-LTL／YY-Thunks 使用已校验的本地依赖缓存，编译没有使用此前生成的 FFmpeg 静态库。

- XP／Win7 静态导入审计通过：4 个系统 DLL、144 个导入。
- 运行 ZIP 仅含 `AddIn/ttp_vqf.dll` 与 `SHA256SUMS.txt`；内外摘要均正确。
- 对应源码 ZIP 含修正后的构建脚本、工作流、完整 FFmpeg 源码和 GNU make 源码，未混入私有测试。
- 单声道 `achterba.vqf` 完整解码 10,600,448 字节，双声道 `song96.vqf` 为 20,430,848 字节；
  两者 PCM SHA-256 均与 2026-10-19 已验证基线相同。
- 本轮 RM／TAK 完成相同路径处理和真实 GNU make 回归，没有重新执行它们的完整编译与播放矩阵。

日志和摘要分别保存在私有 `original-build.log`、`fixed-build.log`、`regression.log` 和
`validation.json`。本轮修复属于构建流程，没有修改 VQF 解码算法或依赖版本。
