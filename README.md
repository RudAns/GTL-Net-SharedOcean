<div align="center">

# 《盗贼之海》式的共享海面

**Tessendorf FFT 海洋 · 船体浮力 · 确定性海面 · 网络时钟同步 · 服务器权威船体**<br>
让多人航海游戏里每个玩家看到同一片海、同一条船：技术调研 + UE 5.8 可联机的交互复现

<a href="LICENSE"><img alt="license MIT" src="https://img.shields.io/badge/license-MIT-0a7bbb"></a>
<img alt="引擎 UE 5.8" src="https://img.shields.io/badge/%E5%BC%95%E6%93%8E-UE_5.8-313131?logo=unrealengine">
<img alt="代码 C++ · HLSL" src="https://img.shields.io/badge/%E4%BB%A3%E7%A0%81-C%2B%2B_%C2%B7_HLSL-00599c">
<a href="Docs/Research.html"><img alt="调研 51 篇文献" src="https://img.shields.io/badge/%E8%B0%83%E7%A0%94-51_%E7%AF%87%E6%96%87%E7%8C%AE-e05d44"></a>
<img alt="联机 支持" src="https://img.shields.io/badge/%E8%81%94%E6%9C%BA-%E6%94%AF%E6%8C%81-8a2be2">
<img alt="手游 可降级" src="https://img.shields.io/badge/%E6%89%8B%E6%B8%B8-%E5%8F%AF%E9%99%8D%E7%BA%A7-2ea44f">

[📥 下载调研报告](https://github.com/RudAns/GTL-Net-SharedOcean/releases/latest/download/Research.html) · [📥 下载落地文档](https://github.com/RudAns/GTL-Net-SharedOcean/releases/latest/download/Implementation.html)

<br>

[![调研报告封面](Docs/shots/00_research_cover.png)](Docs/Research.html)

</div>

## 调研报告

**[Docs/Research.html](Docs/Research.html)**：单文件网页，约 12,000 字，51 条中、日、韩、英、西班牙语文献，带 2 个交互示意，下载后用浏览器打开。

报告以 Rare 在 SIGGRAPH 2018 发表的《The Technical Art of Sea of Thieves》为主要一手资料，结合 GDC 2019 讲稿、官方开发日志、Tessendorf 的海洋模拟讲义，以及《Atlas》等游戏和引擎的公开资料，还原了巨浪、浮力与多人一致性三部分。Rare 没有公开的部分都标为推断并给出依据。主要结论：

- 海面是 Tessendorf FFT 海洋，分平静、普通、风暴三档。浪高随风速平方增长，风暴档放大频谱与水平位移，浪尖的次表面色和泡沫进一步强化视觉高度。
- 多人一致性的关键是把海面做成“种子、海况参数、时间”的纯函数：各端在同步的网络时钟下独立计算，网络上只传时钟和海况。船由服务器权威模拟并复制，甲板上的玩家使用相对船体的坐标。
- 物理只需要长波（玩法波段），短波只影响画面，可以只在客户端的 GPU 上算；服务器的计算量因此很小。
- 手游可以按“FFT / 烘焙的循环位移序列 / 顶点 Gerstner 波”三档降级，同步结构不变。

## 技术落地文档

**[Docs/Implementation.html](Docs/Implementation.html)**：按数据流顺序整理什么走网络、FFT 级联与确定性条件、网络时钟、带位移反解的高度查询、服务器权威的船与两种浮力模型、快照压缩、三种客户端呈现方式、海面 clipmap 与 GPU 路径，附关键常数、坑点清单和移植清单，示意图内嵌，可以直接对照源码搬进自己的项目。

> 提示：如需在自己的项目中使用，可以把本工程的源码和这份 HTML 一起交给 AI 编程助手，让它参照文档把需要的部分接入你的工程。

## UE 5.8 可交互 Demo

一个可联机的航海小游戏：在甲板上走动，到船尾掌舵，升降帆、转舵，在海岛之间航行、收集漂流宝箱，穿过会移动的风暴区。“原理实验台”按“海面与巨浪 / 船体浮力 / 多人一致性”三组列出开关，可以逐项切换并观察海浪、船体和两端画面的变化；网络仪表实时显示往返时间、两端海浪时钟误差和吃水偏差。

![风暴区的巨浪](Docs/shots/07_storm.png)

### 运行

| 脚本 | 作用 |
|---|---|
| `Run.bat` | 以主机（监听服务器）启动，单人游玩也用它 |
| `Run2P.bat` | 同一台机器左右并排开主机和客户端，对比两端画面。使用专用端口 17790，启动前先关闭之前打开的演示窗口，窗口标题标明主机 / 客户端 |
| `RunClient.bat [地址]` | 作为客户端加入另一台机器上的主机，默认 127.0.0.1；主机还没开时每 3 秒自动重试 |

需要 UE 5.8。脚本会自动查找引擎（见 `Tools/find_ue.ps1`），也可以用环境变量 `UE_ROOT` 指定引擎目录。首次运行前编译一次：

```
<引擎目录>\Engine\Build\BatchFiles\Build.bat SharedOceanEditor Win64 Development -Project="<路径>\SharedOcean.uproject"
```

### 操作

| 按键 | 作用 |
|---|---|
| W A S D / 鼠标 / 空格 | 在甲板上移动、环视、跳跃 |
| E | 在船尾舵轮旁掌舵 / 离开；掌舵时 W/S 升降帆，A/D 转舵 |
| Tab | 原理实验台（可点击每一项） |
| `[` `]` | 浮力探针数量 1 / 2 / 4 / 8 / 16 |
| `,` `.` | 涌浪倍率（夸张巨浪的美术控制） |
| `-` `=` | 插值延迟 |
| H / Esc | 按键说明 / 关闭面板 |

### 原理开关

服务器开关作用于所有玩家（由主机切换），客户端开关只改变本机怎样计算和呈现共享状态。

| 键 | 开关 | 作用于 | 观察什么 |
|---|---|---|---|
| F1 | 海面分层：全部 / 只看玩法波段 / 只看画面波段 / 波段着色 | 本机画面 | 船的运动只由 16 m 以上的长波决定，短波只是画面 |
| F2 | 浮力探针与查询点 | 本机画面 | 每个探针的入水深度与受力 |
| F3 | 海浪时钟：网络时钟 / 引擎默认服务器时间 / 各自本地时钟 | 客户端 | 本地时钟下两端的浪错开十几秒，船浮在空中或沉进水里 |
| F4 | 船的呈现：整体快照插值 / 海面同步回退 / 水平插值 + 竖直重建 | 客户端 | 网络仪表里的“两端吃水差” |
| F5 | 海况：平静 / 普通 / 风暴 | 服务器 | 8 秒平滑过渡，各端同步 |
| F6 | 风暴区显示 | 本机画面 | 风暴中心沿固定路径移动，是共享时间的函数，不需要同步 |
| F7 | 模拟网络延迟 0 / 100 / 200 / 350 ms | 客户端 | 延迟变大时网络时钟仍然对齐 |
| F8 | 快照幽灵 | 客户端 | 最新快照（线框）与插值回放位置的差距 |
| F9 | 浮力模型：探针受力 / 平面拟合 + 弹簧 | 服务器 | 平面拟合更稳，但丢掉局部浪形 |
| F10 | 服务器物理频率 60 / 30 / 10 Hz | 服务器 | 降低服务器物理开销后船体运动的变化；配合 F9 对比两种浮力模型在低频率下的表现 |

| 浮力探针（F2） | 平面拟合 + 弹簧（F9） |
|---|---|
| ![](Docs/shots/06_probes.png) | ![](Docs/shots/09_planefit.png) |

| 波段着色：橙色为玩法波段，蓝色为画面波段 | 只剩 1 个探针时船的姿态 |
|---|---|
| ![](Docs/shots/05_layer_tint.png) | ![](Docs/shots/11_one_probe.png) |

| 客户端：网络时钟下两端吃水一致 | 客户端：本地时钟下海浪错开 14 秒 |
|---|---|
| ![](Docs/shots/c01_synced.png) | ![](Docs/shots/c03_local_clock.png) |

| 客户端：快照幽灵（F8） | 原理实验台 |
|---|---|
| ![](Docs/shots/c07_ghost.png) | ![](Docs/shots/13_lab.png) |

### 代码结构

| 文件 | 内容 |
|---|---|
| `OceanFFT` | Tessendorf FFT：Phillips 风浪 + 窄带涌浪、量化色散（海面每 512 s 精确循环）、确定性高斯种子、一次四个复数 FFT 输出位移、斜率与雅可比 |
| `OceanManager` | 每台机器各自运行：6 个级联（G0/G1 玩法波段，V2/V3 画面波段，S0/S1 风暴），按本机的网络时钟求值，上传纹理，提供带位移反解的高度查询 |
| `SharedOceanShaders` 模块 + `Shaders/OceanFFT.usf` | 画面波段的 GPU 路径（RDG 计算着色器）：频谱推进、行列两遍逆 FFT（组共享内存）、雅可比泡沫；与 CPU 路径的约定完全一致，可在实验台切换对比 |
| `OceanSurface` | 以相机为中心的 9 层几何 clipmap，外层丢弃内层已覆盖的像素，层间几何过渡，避免裂缝 |
| `SONetClock` | NTP 式网络时钟：最短往返样本 + 限速修正；可切换为引擎默认服务器时间或本地时钟作对照 |
| `SOShip` | 服务器权威刚体：浮力探针 / 平面拟合 + 弹簧，帆与舵，岛屿碰撞；快照自定义序列化（整数厘米、smallest-three 四元数、16 位速度）；客户端 Hermite 插值与三种呈现方式 |
| `SOCharacter` | UE5 小白人水手，CharacterMovement 基座移动站在甲板上，掌舵 |
| `SOChest` | 漂流宝箱：只复制水平位置，起伏在每台机器上按本机海面计算 |
| `SOEnvironment` | 天空大气、体积云、雾、海岛、码头、灯塔；风暴区局部压暗 |
| `SOHud` | 代码构建的 UMG：罗盘、航海仪表、网络仪表、原理实验台、提示与说明 |
| `SOCapture` | 自测脚本：注入按键、PASS/FAIL 检查、截图 |
| `Tools/build_assets.py` | 无头生成材质（海面 WPO 与着色的 HLSL 写在脚本里便于审阅）、建空地图 |

### 自测

```
powershell -File Tools/iterate.ps1 [-Assets] [-NoBuild] [-NoHost] [-Net]
```

编译 → 可选重建材质 → 离屏运行主机自测 12 项（甲板行走、鼠标环视、掌舵、航行、转向、风暴、平面拟合、10 Hz 物理、单探针、GPU 画面波段、离舵、帧时间）；`-Net` 额外在同一台机器上起一对监听服务器和客户端，从客户端检查快照带宽、时钟同步、三种时钟模式与三种呈现方式下的两端吃水差、模拟延迟下的时钟误差和在航行的船上行走，共 6 项。结果在 `Saved/Capture`（截图与 `*_results.txt`），最近一次的结果和截图也存在 `Docs/` 下。

### 资源

- 角色：UE5 第三人称模板自带的 Mannequin（`Content/Characters/Mannequins`），正常比例。
- 船、岛、码头、灯塔、宝箱：全部在 C++ 中程序化生成（`SOMeshBuilder`），顶点色着色。

## 许可证

本仓库自己编写的代码、脚本、着色器、文档和截图以 [MIT 许可证](LICENSE) 发布。下列第三方内容不在 MIT 范围内，仍按各自的许可使用：

- 角色 Mannequin（`Content/Characters/Mannequins`）：Epic Games 随引擎模板提供的示例内容（EULA 中的 Examples），按 Unreal Engine EULA 可随项目再分发，版权归 Epic Games 所有。
- Unreal Engine 5.8：打开、编译和运行本工程需遵守 [Unreal Engine EULA](https://www.unrealengine.com/eula/unreal)。
