# Better CF IP 项目完善计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 全方位完善 better-cf-ip-c 项目，涵盖代码质量、测试、自动化、文档和持续集成。

**Architecture:** 单文件 C 项目，使用 libcurl + OpenSSL + pthreads。通过渐进式改进，添加测试框架、CLI 参数解析、CI 集成、代码安全检查，最终实现可维护、可测试、可自动化的生产级工具。

**Tech Stack:** C11, libcurl, OpenSSL, pthreads, MinUnit (测试框架), GitHub Actions (CI)

---

## Task A1: 增强 Makefile

**Files:**
- Modify: `Makefile`
- Create: `.clang-format`

**Step 1: 扩展 Makefile 目标**

```makefile
CC ?= gcc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra -pedantic -pthread
PKG_CFLAGS := $(shell pkg-config --cflags libcurl openssl 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs libcurl openssl 2>/dev/null)
ifeq ($(strip $(PKG_LIBS)),)
PKG_LIBS := -lcurl -lssl -lcrypto
endif

TARGET := better-cf-ip-c
SRC := better_cf_ip.c
TEST_TARGET := test_runner
TEST_SRC := test_better_cf_ip.c

# 基础编译标志
BASE_CFLAGS := -std=c11 -pthread $(PKG_CFLAGS)
WARN_FLAGS := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion \
              -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes \
              -Wmissing-declarations -Wcast-qual -Wwrite-strings

# Release 编译
RELEASE_CFLAGS := -O3 $(BASE_CFLAGS) $(WARN_FLAGS)

# Debug 编译
DEBUG_CFLAGS := -O0 -g3 -ggdb $(BASE_CFLAGS) $(WARN_FLAGS)

# AddressSanitizer
ASAN_CFLAGS := -O1 -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer $(BASE_CFLAGS) $(WARN_FLAGS)
ASAN_LDFLAGS := -fsanitize=address -fsanitize=undefined

# 测试编译
TEST_CFLAGS := -O0 -g -DUNIT_TESTING $(BASE_CFLAGS) $(WARN_FLAGS)

.PHONY: all clean debug asan test test-run format

all: release

release: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(RELEASE_CFLAGS) -o $@ $< $(PKG_LIBS) -pthread

debug:
	$(CC) $(DEBUG_CFLAGS) -o $(TARGET)-debug $(SRC) $(PKG_LIBS) -pthread

asan:
	$(CC) $(ASAN_CFLAGS) -o $(TARGET)-asan $(SRC) $(PKG_LIBS) -pthread $(ASAN_LDFLAGS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRC) $(SRC)
	$(CC) $(TEST_CFLAGS) -o $@ $(TEST_SRC) $(PKG_LIBS) -pthread -lm

format:
	clang-format -i $(SRC)

clean:
	rm -f $(TARGET) $(TARGET)-debug $(TARGET)-asan $(TEST_TARGET)
```

**Step 2: 提交**

```bash
git add Makefile
git commit -m "build: enhance Makefile with debug/asan/test targets and stricter warnings"
```

---

## Task A2: 添加代码格式化配置

**Files:**
- Create: `.clang-format`

**Step 1: 创建 .clang-format**

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
AllowShortFunctionsOnASingleLine: None
AllowShortIfStatementsOnASingleLine: false
AllowShortLoopsOnASingleLine: false
SortIncludes: false
ColumnLimit: 120
```

**Step 2: 提交**

```bash
git add .clang-format
git commit -m "style: add clang-format configuration"
```

---

## Task B1: 修复代码安全与可移植性问题

**Files:**
- Modify: `better_cf_ip.c`

**问题列表与修复：**

### 1. 添加可移植的 `strdup` 实现
`strdup` 是 POSIX 扩展，非 C11 标准。添加条件编译的 fallback。

```c
/* 可移植 strdup — C11 中有条件支持 */
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
static char *portable_strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}
#else
#define portable_strdup strdup
#endif
```

将所有 `strdup` 替换为 `portable_strdup`。

### 2. 修复 `snprintf` 返回值检查
`snprintf` 返回值可能 >= size，表示截断。需要检查。

### 3. 添加 `_GNU_SOURCE` 条件保护
`strtok_r` 需要 `_POSIX_C_SOURCE >= 1` 或 `_GNU_SOURCE` — 确保所有 POSIX 函数在非 Linux 平台正确声明。

### 4. 移除冗余的 `PATH_MAX` 重定义
`#ifndef PATH_MAX` 已经是回退。保持，但确保在其他平台也能工作。

**Step 2: 提交**

```bash
git add better_cf_ip.c
git commit -m "fix: add portable strdup and improve cross-platform compatibility"
```

---

## Task B2: 添加单元测试框架

**Files:**
- Create: `test_better_cf_ip.c`
- Modify: `Makefile`（上面已完成）

**测试框架：** MinUnit — 极简 C 单元测试框架（仅 4 个宏）。

**测试内容：**

### 1. 工具函数测试
- `trim_in_place` — 前导/后置空格、空字符串、纯空格
- `copy_cstr` — 正常复制、截断、NULL 源
- `append_cstr` — 追加、溢出
- `file_exists` — 存在/不存在

### 2. 数据结构测试
- `string_list_init` / `string_list_push_dup` / `string_list_free`
- `rtt_vector_init` / `rtt_vector_push` / `rtt_vector_free`

### 3. JSON 解析测试
- `json_read_string` — 简单字符串、转义字符、unicode
- `json_extract_string` — 基本提取、缺失 key
- `hex4` — 有效/无效 hex
- `append_utf8` — 各种 codepoint

### 4. IP 处理测试
- `parse_ip_list` — 正常列表、空内容
- `bracket_ipv6_if_needed` — IPv4/IPv6/已括号化
- `extract_data_center` — 正常 cf-ray、无效输入

### 5. HTTP 头检测测试
- `headers_have_cf_ray` — 有 CF-RAY、无 CF-RAY
- `buffer_contains` — 包含、不包含
- `buffer_has_header_end` — 有结束标记、无结束标记

### 6. 排序测试
- `compare_rtt_result` — 小于、大于、相等

**Step N: 提交**

```bash
git add test_better_cf_ip.c
git commit -m "test: add unit test framework with MinUnit and comprehensive test cases"
```

---

## Task C1: 添加 CLI 参数解析（非交互模式）

**Files:**
- Modify: `better_cf_ip.c`

**设计：**
- 使用 `getopt`（POSIX 标准）解析命令行参数
- 保持交互模式作为默认（无参数时）
- 添加非交互模式的启动方式

**支持的参数：**
```
用法: better-cf-ip-c [选项]

选项:
  -4, --ipv4            测试 IPv4（默认）
  -6, --ipv6            测试 IPv6
  -t, --tls             使用 TLS（默认）
  -p, --plain           不使用 TLS
  -j, --json            输出 JSON 格式
  -b, --bandwidth N     设置目标带宽 (Mbps，默认 1)
  -s, --single IP       单 IP 测速
  -P, --port PORT       测速端口（默认 443/80）
  -c, --clear-cache     清空缓存
  -u, --update          更新数据
  -d, --data-dir DIR    数据目录
  -h, --help            显示帮助信息
```

**实现方案：**
- 在 `main()` 中添加参数解析逻辑
- 非交互模式直接执行对应操作后退出
- `--help` 显示使用说明
- 保持与现有 `--data-dir` 兼容

**Step N: 提交**

```bash
git add better_cf_ip.c
git commit -m "feat: add CLI argument parsing for non-interactive mode"
```

---

## Task C2: 添加 JSON 输出模式

**Files:**
- Modify: `better_cf_ip.c`

**设计：**
- 添加全局 `output_json` 标志
- 在结果输出点判断 JSON/文本模式
- JSON 输出包含：IP、带宽、延迟、数据中心、用时

**输出格式：**
```json
{
  "ip": "1.1.1.1",
  "max_speed_kbps": 102400,
  "tcp_ms": 12,
  "data_center": "Los Angeles, US",
  "elapsed_seconds": 45
}
```

**Step N: 提交**

```bash
git add better_cf_ip.c
git commit -m "feat: add JSON output mode for automation/scripting"
```

---

## Task D1: 添加 README 文档

**Files:**
- Create: `README.md`

**内容包含：**
- 项目概述
- 构建要求（libcurl, OpenSSL）
- 构建方法
- 交互式使用说明
- 命令行参数说明
- 环境变量 (`BETTER_CF_IP_DATA_DIR`)
- JSON 输出示例
- 常见问题

**Step N: 提交**

```bash
git add README.md
git commit -m "docs: add comprehensive README with usage instructions"
```

---

## Task D2: 添加 GitHub Actions CI

**Files:**
- Create: `.github/workflows/ci.yml`

**CI 配置：**
```yaml
name: CI

on:
  push:
    branches: [ main ]
  pull_request:
    branches: [ main ]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: sudo apt-get update && sudo apt-get install -y libcurl4-openssl-dev libssl-dev
      - name: Build release
        run: make
      - name: Build with AddressSanitizer
        run: make asan
      - name: Run tests
        run: make test
```

**Step N: 提交**

```bash
mkdir -p .github/workflows
git add .github/workflows/ci.yml
git commit -m "ci: add GitHub Actions workflow for build, ASAN, and tests"
```

---

## Task E1: 代码审查与最终清理

**Files:**
- Modify: `better_cf_ip.c`

**检查项：**
- 所有函数是否有 `static` 声明（文件内可见）
- 未使用的参数是否用 `(void)` 标注
- `const` 正确性 — 只读指针使用 `const`
- 移除硬编码的魔法数字
- 检查所有 `malloc`/`strdup` 的 NULL 返回值
- 检查所有 `pthread_mutex_*` 返回值

**Step N: 提交**

```bash
git add better_cf_ip.c
git commit -m "refactor: code cleanup with const correctness and null checks"
```

---

## 执行顺序

1. Task A1: 增强 Makefile
2. Task A2: 代码格式化配置
3. Task B1: 修复代码安全与可移植性
4. Task B2: 添加单元测试
5. Task C1: CLI 参数解析
6. Task C2: JSON 输出
7. Task D1: README
8. Task D2: GitHub Actions CI
9. Task E1: 最终清理
