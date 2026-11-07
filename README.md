# mini_dsp

一个用于学习和演示的轻量级 C11 DSP framework。当前范围是**离线 WAV 处理**与
**256 点 FFT/IFFT 实验**，不是产品级音频引擎。

- 语言/标准：C11，Linux，CMake，数学函数链接 `libm`
- 无第三方 DSP 依赖，FFT、biquad、WAV I/O 全部自行实现
- 算法与文件 I/O、设备 I/O、演示逻辑分离
- 无线程调度、无插件注册系统、无复杂配置系统

---

## 1. 目录与文件

```
mini_dsp/
├── CMakeLists.txt
├── README.md
├── framework/                 # 框架对象：数据视图、模块、管线、算法工具
│   ├── dsp_buffer.h           # struct dsp_buffer（交错 float 视图，不拥有内存）
│   ├── dsp_module.h           # dsp_module_ops / dsp_module
│   ├── dsp_pipeline.h         # struct dsp_pipeline + pipeline_* 接口
│   ├── dsp_pipeline.c         # ping-pong scratch、所有权、校验、错误传播
│   ├── dsp_block.h / .c       # 离线分块连续处理（历史跨块保持）
│   ├── dsp_error.h            # 统一状态码 + dsp_strerror()
│   ├── dsp_error.c
│   ├── dsp_math.h             # 显式 pi 常量（不用非标准 M_PI）
│   ├── fft.h                  # struct fft_runtime + 固定 256 点接口
│   └── fft.c                  # 基 2、迭代、原地 FFT/IFFT
├── module/                    # dsp_module 实现
│   ├── biquad.h / biquad.c    # RBJ 公式，高通 / 低通，直接 I 型，仅单声道
│   ├── agc.h / agc.c          # **固定增益**模块（历史名称为 AGC）
│   └── ns.h / ns.c            # 宽带噪声门（向下扩展器），多声道
├── audio/                     # 文件 I/O 与数值转换
│   ├── wav.h / wav.c          # 44 字节标准头的 WAV 读写
│   └── dsp_convert.h / .c     # int16 <-> float 转换
├── demo/                      # 演示层
│   ├── demo.h
│   ├── main.c                 # 只负责命令行解析
│   ├── demo_wav.c             # run_wav_demo()
│   └── demo_fft.c             # run_fft_demo()
└── tests/
    ├── temp.wav               # 生成好的练习音频（96044 字节）
    ├── gen_temp_wav.py        # 可重复生成 temp.wav（仅标准库）
    └── verify.sh              # 一键验收脚本（构建 + 演示 + ctest + 管线自检）
```

## 2. 构建与运行

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build

./build/dsp_demo fft
./build/dsp_demo wav tests/temp.wav tests/output.wav
```

`ctest` **默认不启用**（你是来跑自己 demo 的，不需要测试目标）。需要时显式打开：

```bash
cmake -S . -B build -DMINI_DSP_REGISTER_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

- `./build/dsp_demo` 或 `./build/dsp_demo wav`：使用默认路径 `tests/temp.wav` → `tests/output.wav`
- `./build/dsp_demo wav <input> <output>`：显式指定路径
- `./build/dsp_demo wav <input> <output> <n>`：按每块 `n` 帧分块处理（`n` 省略 = 整段一次处理）
- `./build/dsp_demo fft`：FFT/IFFT 自检演示
- 路径相对当前工作目录，**请从项目根目录运行**
- 显式指定的输出文件若已存在会被**覆盖**；输出路径与输入路径相同时程序直接拒绝

严格编译警告与 sanitizer（GCC/Clang）：

```bash
cmake -S . -B build-strict -DMINI_DSP_WERROR=ON -DMINI_DSP_SANITIZE=ON
cmake --build build-strict
```

一键验收：

```bash
bash tests/verify.sh              # 只构建 + 跑 demo + 校验 WAV
RUN_TESTS=1 bash tests/verify.sh  # 额外跑 ctest 与临时管线自检
```

默认流程：生成缺失的 `tests/temp.wav` → 严格警告 + ASan/UBSan 构建 →
跑 `fft` 与 `wav` 演示 → 两次运行结果字节比对 → WAV 头/尺寸/PCM 校验。
`RUN_TESTS=1` 时再追加 `ctest` 和临时管线自检（模块数量奇偶、块尺寸、所有权、错误传播）；
临时自检源码只写在构建目录里，不进入仓库。

## 2.1 在 VS Code 里开发

用 **Remote - WSL** 打开工程所在目录：

```
\\wsl.localhost\Ubuntu-22.04\home\zou\mini_dsp
```

`.vscode/` 已经配好，开箱可用：

| 文件 | 作用 |
|---|---|
| `tasks.json` | configure / build / build:strict / run:fft / run:wav / verify / clean |
| `launch.json` | F5 直接 gdb 调试 `dsp_demo fft` 与 `dsp_demo wav` |
| `c_cpp_properties.json` | IntelliSense 读 `build/compile_commands.json` |
| `settings.json` | LF 行尾、排除 build 目录、C11、编译命令路径 |
| `extensions.json` | 推荐 C/C++、CMake Tools |

常用操作：

- `Ctrl+Shift+B`：增量构建（自动先跑 CMake 配置，编译错误会标到行号）
- `Ctrl+Shift+P` → `Tasks: Run Task`：运行 `run: fft`、`verify`、`build:strict` 等
- `F5`：在 `fft_transform` 之类的函数里下断点，直接看 `fft->real[]`、biquad 系数
- 改过 `CMakeLists.txt` 后重新 `build` 会自动重跑配置并刷新 `compile_commands.json`

> `build/` 是 Debug 构建，专门给断点用的；带 `-Werror` 和 sanitizer 的验证走
> `build:strict` 或 `tests/verify.sh`，两者互不干扰。

## 3. 主要接口

```c
struct dsp_buffer { float *data; uint32_t frames; uint32_t channels; };

struct dsp_module_ops {
    int  (*process)(void *ctx, struct dsp_buffer *in, struct dsp_buffer *out);
    void (*reset)(void *ctx);
    void (*destroy)(void *ctx);
};
struct dsp_module { const char *name; void *priv; const struct dsp_module_ops *ops; };

int  pipeline_init(struct dsp_pipeline *pipe, uint32_t max_frames, uint32_t channels);
int  pipeline_add(struct dsp_pipeline *pipe, struct dsp_module *module);
int  pipeline_process(struct dsp_pipeline *pipe, struct dsp_buffer *in, struct dsp_buffer *out);
void pipeline_reset(struct dsp_pipeline *pipe);
void pipeline_destroy(struct dsp_pipeline *pipe);

struct dsp_module *biquad_module_create(const struct biquad_config *cfg, int sample_rate);
struct dsp_module *create_agc_module(void);
struct dsp_module *ns_module_create(const struct ns_config *cfg, int sample_rate);

/* 离线分块连续处理：把一段 PCM 按 block_frames 切块喂给同一条 pipeline */
int dsp_block_process(struct dsp_pipeline *pipe, const float *input, float *output,
                      uint32_t frames, uint32_t block_frames);

/* 同上，但每块结果交给 sink，适合边算边写盘、不占整段输出内存 */
struct dsp_block_sink {
    int (*write)(void *user, uint32_t frame_offset, uint32_t frames, const float *data);
    void *user;
};
int dsp_block_stream(struct dsp_pipeline *pipe, const float *input, uint32_t frames,
                     uint32_t block_frames, float *block_scratch,
                     const struct dsp_block_sink *sink);

int fft_forward(struct fft_runtime *fft, const float *input, uint32_t frames);
int fft_transform(struct fft_runtime *fft, int inverse);

int wav_read(const char *path, struct wav_file *wav);
int wav_write(const char *path, const struct wav_file *wav);
```

所有可失败函数返回 `DSP_OK`(0) 或负错误码（见 `framework/dsp_error.h`），
`dsp_strerror()` 给出可读原因。**没有静默直通**：创建失败、处理失败都会向上传播。

## 4. 设计要点

**pipeline / scratch**

- 调用者传入零初始化的 `struct dsp_pipeline`
- `pipeline_init` 分配两块 scratch，每块 `max_frames * channels * sizeof(float)`，乘法和
  `size_t` 上限都做了溢出检查
- 两块按模块索引交替使用（ping-pong）；`pipeline_process` 内**不调用** malloc/free
- 每次调用模块前都重设 scratch 视图的 `frames`/`channels`/`data`
- 最终结果**无条件**复制到调用者的 `out`，与模块数量奇偶无关；零模块时原样透传
- 模块返回后校验 `out->data` 未被替换、尺寸未被改动，违者返回 `DSP_ERR_STATE`
- 成功后 pipeline 接管模块所有权；添加失败时调用者仍负责释放
- 静态所有权表拒绝"同一模块对象重复添加"和"同一模块被两条 pipeline 拥有"
- `pipeline_destroy` 依次：模块 `destroy` → 释放模块对象 → 释放 scratch → 清零结构，
  二次调用是安全的空操作

**biquad**

- 直接 I 型：`y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]`
- RBJ 公式：`omega = 2*pi*fc/fs`，`alpha = sin(omega)/(2Q)`，`a0 = 1 + alpha`，
  `b0/b1/b2/a1/a2` 全部先除以 `a0` 再保存
- 高通 `b0=(1+cos)/2, b1=-(1+cos), b2=b0`；低通 `b0=(1-cos)/2, b1=1-cos, b2=b0`
- 校验采样率、有限数、`0 < fc < fs/2`、`q > 0`；NOTCH/PEAK/SHELF 返回 `DSP_ERR_UNSUPPORTED`
- 创建时先置为直通系数 + 零历史，再套用设计结果；设计失败则对象保持直通
- `reset` 只清历史，不动系数
- 多声道输入明确拒绝（`DSP_ERR_UNSUPPORTED`），不共用一组历史状态

**AGc（重要声明）**

> 名为 AGC 的模块**并不是自动增益控制**。它当前是固定增益：`y = x * gain`，`gain` 默认 1.0。
> `target_level=1.0`、`attack_ms=10.0`、`release_ms=100.0` 只是为将来预留的字段，
> 当前不参与任何计算，也没有电平检测或平滑。`reset` 把 `gain` 恢复为 1.0。

**FFT**

- 固定 `FFT_SIZE = 256`，基 2、迭代、原地；`fft_forward` 接收且只接收 256 个实数样本
- `fft_transform(inverse=0)`：位反转 + 蝶形，负角度，不缩放
- `fft_transform(inverse=1)`：正角度，结束时实部与虚部都除以 256
- 处理期间不分配内存；循环索引用 `offset`/`span`/`half`/`idx`，避免与虚数单位混淆
- 幅度分析、测试信号生成都在 demo 层，不在算法实现里

**块处理（dsp_block）**

- 输入是一整段交错 float PCM，驱动按 `block_frames` 切成连续的块，每块调用一次
  `pipeline_process`；模块历史（biquad 的 x1/x2/y1/y2、NS 的包络与增益）由 pipeline
  自己跨块保留，所以**分块结果与整段一次处理逐位相同**
- 驱动本身**不分配内存**：scratch 的容量由 `pipeline_init` 的 `max_frames` 决定，
  `block_frames` 必须落在 `1..max_frames`
- 不做文件 I/O、不睡眠、不涉及线程——这是"离线连续处理已解码的 PCM"，不是设备实时流
- 校验齐全：`block_frames == 0`、超过 `max_frames`、`frames*channels` 溢出、未初始化的
  pipeline、空输入/输出指针，都会立即返回错误码而不是中途失败
- 允许 `input == output`（就地处理）：pipeline 从不写输入视图；但部分重叠的缓冲区不支持
- `dsp_block_stream` 把每块结果交给 `sink->write()`，不自建整段输出缓冲；sink 返回非
  `DSP_OK` 会立即中止并把该错误码向上传播

**NS（宽带噪声门）**

> 注意：这是**时域噪声门 / 向下扩展器**，不是"噪声估计 + 谱增益"的频谱降噪。
> 频谱 NS 需要分帧和 overlap-add，mini_dsp 目前没有，模块也不假装有。

- 逐样本处理：包络跟随 → 带迟滞的阈值判决 → 增益平滑，全程不需要分帧
- 阈值下方把增益压到 `floor_db`（默认 -24 dB），阈值上方回到 1.0
- `hysteresis_db` 拉开"关门"和"开门"两个阈值，避免在阈值附近抖动
- `attack_ms` 控制增益关闭速度，`release_ms` 控制恢复速度（一阶平滑，
  系数由 `1 - exp(-1/(tau*fs))` 推出）
- **多声道支持**：每个声道独立的包络 / 增益 / 锁存状态，最多 `NS_MAX_CHANNELS`(8) 路，
  超出明确返回 `DSP_ERR_UNSUPPORTED`
- 声道数变化会重置历史，避免把上一个布局的状态套到新布局上
- 配置全部做范围校验（-96..0 dB、时间常数 > 0、迟滞 0..24 dB）
- `reset` 只清包络/增益/锁存，保留配置

**WAV**

- 仅支持 `format=1`、单声道、16-bit、48000 Hz、`fmt` 长度 16、`fmt` 后紧跟 `data`
- 其他格式、`fmt` 之前的额外 chunk：明确拒绝
- 不要求 `RIFF size == 36 + data_size`（`data` 之后可能有合法元数据）
- 全部字段按显式小端读写，不依赖结构体填充与主机字节序
- `data_size` 上限 16 MiB；`frames = data_size / block_align`
- 写入时重新生成标准头（`RIFF size = 36 + data_size`，整文件 `44 + data_size`），
  不复制调用者的头部字节，也不修改/释放调用者的 PCM

## 5. 验收状态（实测结果）

环境：**WSL2 Ubuntu 22.04.5 LTS**，gcc 11.4.0，cmake 3.22.1，GNU Make 4.3，Python 3.10.12，
工程位于 `/home/zou/mini_dsp`。

构建方式：`-DCMAKE_BUILD_TYPE=RelWithDebInfo -DMINI_DSP_WERROR=ON -DMINI_DSP_SANITIZE=ON`，
即 `-Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wundef
-Wwrite-strings` **全部 + `-Werror`**，并链接 `libasan.so.6` / `libubsan.so.1`
（`ldd build-verify/dsp_demo` 已确认运行时确实链接）。

| 检查 | 结果 |
|---|---|
| `cmake -S . -B build-verify` + `cmake --build` | ✅ 0 警告 0 错误 |
| `./build-verify/dsp_demo fft` | ✅ 退出码 0 |
| `./build-verify/dsp_demo wav tests/temp.wav tests/output.wav` | ✅ 退出码 0 |
| `ctest` | ✅ 2/2 通过（`dsp_fft_demo`、`dsp_wav_demo`） |
| `bash tests/verify.sh` | ✅ ALL CHECKS PASSED |
| 管线自检（0/1/2/3 模块、超大块、所有权、错误传播、reset/destroy 次数） | ✅ 27 项全过 |
| ASan/UBSan/LeakSanitizer | ✅ 所有运行（含失败路径）无报告 |
| 失败路径（输入不存在、输出等于输入、错误参数） | ✅ 均返回 1 并给出明确原因 |

FFT 实测（1000 Hz、幅度 0.5、256 点、fs=48000）：

- 峰值落在 **bin 5**（937.50 Hz），幅度 **0.4187982** —— 符合 1000 Hz≈bin 5.333 的泄漏预期，
  不要求恰好 0.5
- IFFT 与 `0.5 × input` 对比：最大实部误差 **6.706e-08**，最大虚部残差 **5.894e-08**（阈值 1e-4）
- 整数 bin 校验：1500 Hz 落在 **bin 8**，单边幅度 **0.5000000**，其余单边 bin 均 < 1e-3

WAV 实测：

- 输入 48000 帧 / 1 ch / 48000 Hz / 16 bit，输出 **96044 字节**
- `RIFF size = 96036`、`data size = 96000`、channels=1、rate=48000、bits=16
- 两次运行输出 **字节完全一致**
- 稳态 RMS 5784.4（理论 5792.6，增益 0.9986，与 440 Hz 处 |H|≈1 吻合）
- 演示打印的 `dry/wet peak` 为 0.250000 → 0.278562，峰值出现在第 75 个样本：
  这是 IIR 从零状态启动时 100 Hz 高通对突发信号的**瞬态过冲**，不是系数错误
  （稳态峰值 8181/8192，RMS 与增益均已验证）

构建过程中真实暴露并修复的两个问题（保留记录，说明这些检查确实在跑）：

1. `framework/fft.c` 缺少 `#include <stddef.h>`，`NULL` 未声明；
2. `audio/wav.c` 中 `(size_t)frames > SIZE_MAX / 2u` 在 64 位下恒假，触发 `-Wtype-limits`，
   改为按 16 MiB 上限对帧数做检查。

**仍未验证**：32 位平台、非 x86 架构、其他编译器（clang）下的构建与行为。


## 6. 当前能力边界（不在本次实现范围）

- **不是**真正的自动增益控制（无电平检测、无 attack/release 平滑）
- `ns` 只是宽带噪声门，**没有**噪声估计与谱增益，也没有 AEC
- 无加窗、无滑窗、无 overlap-add（因此也做不了频谱降噪）
- biquad 仅单声道；多声道直接报错（`ns` 支持多声道）
- 无任意 WAV 格式解码（只有 mono/16-bit/48 kHz/format=1 且无前置额外 chunk）
- 无 tinyalsa 设备接入、无实时线程与设备同步；不支持分块 WAV 流式处理
- 无带延迟模块的 flush/drain 协议；模块必须同步写满全部输出样本
- 同一 pipeline 实例不提供并发 `process` 保证

后续接 tinyalsa 时，应放在独立设备适配层：设备侧完成格式转换后，复用同一套
`dsp_buffer` / `pipeline`，framework 与算法不依赖设备 API。

## 7. 重新生成练习音频

```bash
python3 tests/gen_temp_wav.py            # 已存在则拒绝覆盖
python3 tests/gen_temp_wav.py --force    # 显式覆盖
```

参数：48000 帧、单声道、16-bit、48000 Hz、440 Hz 正弦、幅度 0.25（峰值 8192 计数），
标准 44 字节头、无额外 chunk，文件共 **96044** 字节。
Python 只用于生成测试素材，不是 DSP 运行依赖。
