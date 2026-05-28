# Better CF IP (C 语言版)

Cloudflare CDN IP 优选工具，用 C 语言编写。基于延迟（RTT）和带宽实测，从 Cloudflare 全球 IP 段中自动筛选出最适合当前网络的 IP 地址。

## 功能特性

- **IPv4 优选** — 从 Cloudflare IPv4 段中随机生成 IP，多轮 RTT + 带宽测试
- **IPv6 优选** — 从 Cloudflare IPv6 段中随机生成 IP，多轮 RTT + 带宽测试
- **TLS 支持** — 支持 HTTPS (443) 和 HTTP (80) 两种协议
- **单 IP 测速** — 对指定 IP 进行带宽和延迟测试，可自定义端口
- **并发 RTT 测试** — 多线程并发扫描，快速筛选低延迟 IP
- **自动生成随机 IP** — 基于 Cloudflare 公布的子网段随机生成 IP 地址
- **数据中心位置查询** — 通过 CF-RAY 响应头解析数据中心 IATA 码，并转换为可读的城市名称
- **缓存管理** — 本地缓存 IP 列表、测速 URL 和数据中心位置数据
- **数据更新** — 一键重新下载所有数据文件
- **可配置带宽目标** — 设置期望的最小带宽（Mbps），自动循环测试直到命中目标

## 系统要求

### 运行时依赖

| 依赖 | 版本要求 | 用途 |
|------|----------|------|
| libcurl | >= 7.x | 带宽测速 HTTP 下载 |
| OpenSSL | >= 1.1.x | TLS 握手与加密连接 |
| pthreads | POSIX 标准 | 多线程并发 RTT 测试 |

### 编译工具

| 工具 | 用途 |
|------|------|
| GCC 或 Clang | C11 编译器 |
| make | 构建工具 |
| pkg-config | 依赖库的编译标志探测（可选） |
| clang-format | 代码格式化（可选，`make format` 需要） |

### 安装依赖

**macOS (Homebrew):**
```bash
brew install curl openssl pkg-config
```

**Ubuntu / Debian:**
```bash
sudo apt-get install build-essential libcurl4-openssl-dev libssl-dev pkg-config
```

**CentOS / RHEL / Fedora:**
```bash
sudo dnf install gcc libcurl-devel openssl-devel pkgconfig make
```

## 构建方法

### Release 构建

```bash
make
```

生成的二进制文件为 `better-cf-ip-c`。

### Debug 构建

```bash
make CFLAGS="-O0 -g3 -ggdb -std=c11 -Wall -Wextra -pedantic -pthread"
```

### AddressSanitizer 构建

```bash
make CFLAGS="-O1 -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -std=c11 -Wall -Wextra -pedantic -pthread" LIBS="-lcurl -lssl -lcrypto -pthread -fsanitize=address -fsanitize=undefined"
```

### 清理构建产物

```bash
make clean
```

### 覆盖编译选项

项目使用 `?=` 赋值，支持通过环境变量覆盖：

```bash
# 使用 clang 编译
CC=clang make

# 自定义编译器标志
CFLAGS="-O2 -march=native" make
```

## 交互式菜单

程序默认启动交互式菜单，通过数字选择功能：

```
----------------------------------------
1. IPV4 优选 (TLS)
2. IPV4 优选 (非 TLS)
3. IPV6 优选 (TLS)
4. IPV6 优选 (非 TLS)
5. 单 IP 测速 (TLS)
6. 单 IP 测速 (非 TLS)
7. 清空缓存
8. 更新数据
0. 退出
请选择菜单 (默认 0):
```

### 选项详解

**选项 1-4: IP 优选 (IPv4/IPv6, TLS/非TLS)**

自动完成以下流程：
1. 读取本地缓存的子网列表（`ips-v4.txt` 或 `ips-v6.txt`）
2. 从子网中随机生成 100 个 IP 地址
3. 使用多线程并发测试 RTT（往返延迟），保留延迟最低的 10 个
4. 对 RTT 最优的 IP 逐个进行带宽测速
5. 达到设置的目标带宽即停止并输出结果
6. 若全部未达标则重新生成 IP 继续测试

运行过程中会提示输入：
- **期望带宽 (Mbps)** — 默认 1 Mbps，设置目标带宽（输入值 * 128 = kB/s 阈值）
- **RTT 测试进程数** — 默认 50，最大 100，控制并发扫描速度

**选项 5-6: 单 IP 测速**

手动输入一个 IP 地址和端口进行速度和延迟测试。

**选项 7: 清空缓存**

删除本地的 `ips-v4.txt`、`ips-v6.txt`、`url.txt`、`locations.json` 文件，下次操作自动重新下载。

**选项 8: 更新数据**

先清空缓存，然后重新初始化（下载所有数据文件并加载数据中心位置信息）。

## 环境变量

### `BETTER_CF_IP_DATA_DIR`

指定数据文件的存储目录。若未设置，数据文件保存在程序当前工作目录。

```bash
# 指定数据目录
export BETTER_CF_IP_DATA_DIR=/path/to/data
./better-cf-ip-c
```

程序也会优先检查命令行参数 `--data-dir`：

```bash
./better-cf-ip-c --data-dir /path/to/data
```

优先级：`--data-dir` 参数 > `BETTER_CF_IP_DATA_DIR` 环境变量 > 当前目录。

## 数据来源

所有数据文件均从 [baipiao.eu.org](https://baipiao.eu.org) 自动下载：

| 文件名 | 数据源 URL | 说明 |
|--------|-----------|------|
| `ips-v4.txt` | `https://www.baipiao.eu.org/cloudflare/ips-v4` | Cloudflare IPv4 子网列表 |
| `ips-v6.txt` | `https://www.baipiao.eu.org/cloudflare/ips-v6` | Cloudflare IPv6 子网列表 |
| `url.txt` | `https://www.baipiao.eu.org/cloudflare/url` | 带宽测速使用的下载文件 URL |
| `locations.json` | `https://www.baipiao.eu.org/cloudflare/locations` | 数据中心 IATA 码到城市名称的映射 |

数据仅在本地不存在时自动下载，之后使用本地缓存。可通过菜单**选项 7** 清空缓存或**选项 8** 强制更新。

## 示例输出

### IPv4 优选

```
正在从 54 个子网中随机生成 IP...
已生成 100 个测试 IP，开始 RTT 测试...
RTT 测试进度: 10/100
RTT 测试进度: 20/100
...
RTT 测试完成，8/100 个 IP 有效，保留延迟最低的 10 个
待测速的 IP 地址
1.1.1.1 往返延迟 12 毫秒
1.1.2.1 往返延迟 15 毫秒
...
正在测试 1.1.1.1
1.1.1.1 峰值速度 51200 kB/s, 数据中心 Los Angeles, US

优选 IP: 1.1.1.1
设置带宽: 1 Mbps
实测带宽: 400 Mbps
峰值速度: 51200 kB/s
往返延迟: 12 毫秒
数据中心: Los Angeles, US
总计用时: 23 秒
```

### 单 IP 测速

```
请输入需要测速的 IP: 1.1.1.1
请输入需要测速的端口 (默认443):
正在测速 1.1.1.1 端口 443
1.1.1.1 平均速度 51200 kB/s, TCP延迟 12ms, 数据中心=Los Angeles, US
```

## 项目架构

```
better-cf-ip-c/
├── better_cf_ip.c        # 单文件主程序
├── Makefile              # 构建配置
├── .clang-format         # 代码格式化规则
├── .gitignore            # Git 忽略规则
├── locations.json        # 数据中心位置数据（自动下载）
├── .context/
│   └── todos.md          # 开发待办
└── docs/plans/
    └── 2026-05-28-better-cf-ip-perfection-plan.md  # 完善计划
```

### 核心技术实现

- **RTT 测试** — 使用原生 socket `connect()` + `select()` 超时控制，非阻塞连接 + 轮询检测
- **TLS 握手** — 通过 OpenSSL 的 `SSL_connect()` 实现，共享全局 SSL_CTX
- **带宽测速** — 使用 libcurl 的 `CURLOPT_CONNECT_TO` 解析域名到目标 IP，实现自定义 Dial
- **数据中心解析** — 解析 HTTP 响应头的 `CF-RAY` 字段提取 IATA 码，哈希表映射到城市名
- **并发控制** — 多线程共享任务队列，逐 IP 分配，互斥锁保护进度和结果收集
- **JSON 解析** — 手写轻量 JSON 解析器，无第三方依赖，支持 unicode 转义和 surrogate pair

## 许可协议

MIT License

版权所有 (c) 2025 better-cf-ip-c

特此授予任何人免费获得本软件及相关文档文件（以下简称"软件"）副本的许可，不受限制地处理本软件，包括但不限于使用、复制、修改、合并、发布、分发、再许可和/或出售软件副本的权利，并允许获得软件的人这样做，但须符合以下条件：

上述版权声明和本许可声明应包含在所有副本或实质性部分中。

本软件按"原样"提供，不提供任何明示或暗示的保证，包括但不限于适销性、特定用途适用性和不侵权的保证。在任何情况下，作者或版权持有人均不对因本软件或本软件的使用或其他交易而产生的任何索赔、损害或其他责任负责，无论是在合同诉讼、侵权诉讼还是其他诉讼中。
