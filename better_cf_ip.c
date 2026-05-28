#define _POSIX_C_SOURCE 200809L

/* Platform-specific includes */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
/* Winsock2 provides all socket/networking types and functions */
#include <winsock2.h>
#include <ws2tcpip.h>       /* inet_pton, inet_ntop */
/* Note: MinGW-w64 UCRT64 does NOT provide POSIX compat headers
   (sys/socket.h, netinet/*.h, arpa/inet.h, sys/select.h, sys/time.h).
   We use pure Winsock2 on this platform. */
#else
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

#include <ctype.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>

/* ---------- Platform compatibility layer ---------- */
#ifdef _WIN32

/* WSAStartup / WSACleanup with reference counting */
static int wsa_refcount = 0;
static int wsa_init(void) {
    if (wsa_refcount == 0) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
    }
    wsa_refcount++;
    return 0;
}
static void wsa_cleanup(void) {
    if (wsa_refcount > 0 && --wsa_refcount == 0) WSACleanup();
}

/* Winsock2 socket() returns SOCKET (uintptr_t); wrap to return int */
#undef socket
#define socket(d, t, p) ((int)WSASocketA((d), (t), (p), NULL, 0, 0))

/* Socket close on Windows uses closesocket() */
#undef socket_close
#define socket_close(fd) closesocket((SOCKET)(fd))

/* Non-blocking mode via ioctlsocket() on Windows */
static int set_fd_blocking(int fd, int blocking) {
    unsigned long mode = blocking ? 0 : 1;
    return ioctlsocket((SOCKET)(fd), FIONBIO, &mode) == 0 ? 0 : -1;
}

#else /* POSIX */

/* Socket close on POSIX uses close() */
#undef socket_close
#define socket_close(fd) close(fd)

#endif /* _WIN32 */

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* 可移植 strdup — 兼容非 POSIX 平台 */
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

#define MAX_DOWNLOAD_SIZE (32u * 1024u * 1024u)
#define MAX_LINE_LEN      1024
#define MAX_IP_LEN        128
#define MAX_DOMAIN_LEN    256
#define MAX_FILE_LEN      1024
#define MAX_HEADER_SIZE   65536
#define LOCATION_TABLE_SIZE 4096

/* ----------------------- 通用类型 ----------------------- */

typedef struct {
    char **items;
    size_t len;
    size_t cap;
} StringList;

typedef struct {
    char ip[MAX_IP_LEN];
    int latency_ms;
} RTTResult;

typedef struct {
    RTTResult *items;
    size_t len;
    size_t cap;
} RTTVector;

typedef struct {
    char iata[16];
    char city[256];
    int used;
} LocationEntry;

typedef struct {
    int max_speed_kbps;
    int tcp_ms;
    char data_center[32];
} SpeedResult;

typedef struct {
    char ip[MAX_IP_LEN];
    int max_speed_kbps;
    int tcp_ms;
    char data_center[256];
} CloudflareResult;

/* ----------------------- 全局状态 ----------------------- */

static char data_dir[PATH_MAX] = "";
static pthread_mutex_t random_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t random_state = 0;

static LocationEntry location_table[LOCATION_TABLE_SIZE];
static pthread_rwlock_t location_lock = PTHREAD_RWLOCK_INITIALIZER;

static char speed_test_domain[MAX_DOMAIN_LEN] = "";
static char speed_test_file[MAX_FILE_LEN] = "";

static pthread_once_t rtt_ssl_once = PTHREAD_ONCE_INIT;
static SSL_CTX *rtt_ssl_ctx = NULL;

/* 全局选项 */
static int opt_noninteractive = 0;  /* 非交互模式 */
static int opt_json_output = 0;     /* JSON 输出模式 */

/* ----------------------- 基础工具 ----------------------- */

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static double elapsed_seconds(const struct timespec *start, const struct timespec *end) {
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;
}

static void trim_in_place(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static int read_line_trim(char *buf, size_t size) {
    if (!fgets(buf, (int)size, stdin)) return 0;
    buf[strcspn(buf, "\r\n")] = '\0';
    trim_in_place(buf);
    return 1;
}

static void copy_cstr(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void append_cstr(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0 || !src) return;
    size_t used = strlen(dst);
    if (used >= dst_size - 1) return;
    size_t n = strlen(src);
    if (n > dst_size - used - 1) n = dst_size - used - 1;
    memcpy(dst + used, src, n);
    dst[used + n] = '\0';
}

static const char *data_path(const char *name, char *out, size_t out_size) {
    if (!out || out_size == 0) return out;
    out[0] = '\0';
    if (data_dir[0] == '\0') {
        copy_cstr(out, out_size, name);
    } else {
        copy_cstr(out, out_size, data_dir);
        append_cstr(out, out_size, "/");
        append_cstr(out, out_size, name);
    }
    return out;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int mkdir_p(const char *dir) {
    if (!dir || dir[0] == '\0' || strcmp(dir, ".") == 0) return 0;
    char tmp[PATH_MAX];
    int n = snprintf(tmp, sizeof(tmp), "%s", dir);
    if ((size_t)n >= sizeof(tmp)) {
        /* 路径被截断，但仍可继续 */
        tmp[sizeof(tmp) - 1] = '\0';
    }
    size_t len = strlen(tmp);
    if (len == 0) return 0;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int save_to_file(const char *filename, const char *content, size_t len) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", filename);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        if (mkdir_p(dir) != 0) return -1;
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;
    size_t written = fwrite(content, 1, len, fp);
    int ok = (written == len && fclose(fp) == 0);
    return ok ? 0 : -1;
}

static char *get_file_content(const char *filename, size_t *out_len) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

static void init_random(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    random_state = ((uint64_t)ts.tv_sec << 32) ^ (uint64_t)ts.tv_nsec ^ (uint64_t)getpid();
    if (random_state == 0) random_state = 0x9e3779b97f4a7c15ULL;
}

static uint64_t next_random_u64_locked(void) {
    /* xorshift64*: 轻量、快速，外部使用 mutex 保证与 Go 版随机源一样串行访问 */
    random_state ^= random_state >> 12;
    random_state ^= random_state << 25;
    random_state ^= random_state >> 27;
    return random_state * 2685821657736338717ULL;
}

static int next_random_intn(int n) {
    if (n <= 0) return 0;
    pthread_mutex_lock(&random_mu);
    uint64_t v = next_random_u64_locked();
    pthread_mutex_unlock(&random_mu);
    return (int)(v % (uint64_t)n);
}

/* ----------------------- 动态数组 ----------------------- */

static void string_list_init(StringList *list) {
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static int string_list_push_dup(StringList *list, const char *s) {
    if (list->len == list->cap) {
        size_t new_cap = list->cap ? list->cap * 2 : 32;
        char **new_items = (char **)realloc(list->items, new_cap * sizeof(char *));
        if (!new_items) return -1;
        list->items = new_items;
        list->cap = new_cap;
    }
    list->items[list->len] = portable_strdup(s ? s : "");
    if (!list->items[list->len]) return -1;
    list->len++;
    return 0;
}

static void string_list_free(StringList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->len; i++) free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->len = 0;
    list->cap = 0;
}

static void rtt_vector_init(RTTVector *vec) {
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static int rtt_vector_push(RTTVector *vec, const char *ip, int latency_ms) {
    if (vec->len == vec->cap) {
        size_t new_cap = vec->cap ? vec->cap * 2 : 32;
        RTTResult *new_items = (RTTResult *)realloc(vec->items, new_cap * sizeof(RTTResult));
        if (!new_items) return -1;
        vec->items = new_items;
        vec->cap = new_cap;
    }
    snprintf(vec->items[vec->len].ip, sizeof(vec->items[vec->len].ip), "%s", ip);
    vec->items[vec->len].latency_ms = latency_ms;
    vec->len++;
    return 0;
}

static void rtt_vector_free(RTTVector *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

/* ----------------------- libcurl 下载 ----------------------- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    size_t max_len;
    int too_large;
} MemoryBuffer;

static size_t curl_write_to_memory(char *ptr, size_t size, size_t nmemb, void *userdata) {
    size_t bytes = size * nmemb;
    MemoryBuffer *mem = (MemoryBuffer *)userdata;
    if (mem->len + bytes > mem->max_len) {
        mem->too_large = 1;
        return 0;
    }
    if (mem->len + bytes + 1 > mem->cap) {
        size_t new_cap = mem->cap ? mem->cap * 2 : 8192;
        while (new_cap < mem->len + bytes + 1) new_cap *= 2;
        char *new_data = (char *)realloc(mem->data, new_cap);
        if (!new_data) return 0;
        mem->data = new_data;
        mem->cap = new_cap;
    }
    memcpy(mem->data + mem->len, ptr, bytes);
    mem->len += bytes;
    mem->data[mem->len] = '\0';
    return bytes;
}

static char *get_url_content(const char *target_url, size_t *out_len) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    MemoryBuffer mem;
    memset(&mem, 0, sizeof(mem));
    mem.max_len = MAX_DOWNLOAD_SIZE;

    curl_easy_setopt(curl, CURLOPT_URL, target_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_to_memory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || mem.too_large) {
        free(mem.data);
        return NULL;
    }
    if (!mem.data) {
        mem.data = portable_strdup("");
        mem.len = 0;
    }
    if (out_len) *out_len = mem.len;
    return mem.data;
}

/* ----------------------- IP 列表与随机 IP 生成 ----------------------- */

static StringList parse_ip_list(const char *content) {
    StringList list;
    string_list_init(&list);
    if (!content) return list;

    char *copy = portable_strdup(content);
    if (!copy) return list;

    char *saveptr = NULL;
    for (char *line = strtok_r(copy, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        trim_in_place(line);
        if (line[0] != '\0') string_list_push_dup(&list, line);
    }
    free(copy);
    return list;
}

static StringList random_sample(const StringList *list, size_t n) {
    StringList sampled;
    string_list_init(&sampled);
    if (!list || list->len == 0) return sampled;

    char **shuffled = (char **)malloc(list->len * sizeof(char *));
    if (!shuffled) return sampled;
    for (size_t i = 0; i < list->len; i++) shuffled[i] = list->items[i];

    pthread_mutex_lock(&random_mu);
    for (size_t i = list->len - 1; i > 0; i--) {
        size_t j = (size_t)(next_random_u64_locked() % (uint64_t)(i + 1));
        char *tmp = shuffled[i];
        shuffled[i] = shuffled[j];
        shuffled[j] = tmp;
    }
    pthread_mutex_unlock(&random_mu);

    if (n > list->len) n = list->len;
    for (size_t i = 0; i < n; i++) string_list_push_dup(&sampled, shuffled[i]);
    free(shuffled);
    return sampled;
}

static StringList get_random_ipv4s(const StringList *ip_list) {
    StringList random_ips;
    string_list_init(&random_ips);
    if (!ip_list) return random_ips;

    for (size_t i = 0; i < ip_list->len; i++) {
        char subnet[MAX_IP_LEN];
        snprintf(subnet, sizeof(subnet), "%s", ip_list->items[i]);
        trim_in_place(subnet);
        if (subnet[0] == '\0') continue;

        char *slash = strchr(subnet, '/');
        if (slash) *slash = '\0';

        char *parts[4] = {0};
        char *saveptr = NULL;
        int count = 0;
        for (char *tok = strtok_r(subnet, ".", &saveptr); tok && count < 4; tok = strtok_r(NULL, ".", &saveptr)) {
            parts[count++] = tok;
        }
        if (count == 4) {
            char ip[MAX_IP_LEN];
            snprintf(ip, sizeof(ip), "%s.%s.%s.%d", parts[0], parts[1], parts[2], next_random_intn(256));
            string_list_push_dup(&random_ips, ip);
        }
    }
    return random_ips;
}

static int split_colon_keep_empty(const char *s, char parts[][32], int max_parts) {
    int count = 0;
    const char *start = s;
    for (const char *p = s; ; p++) {
        if (*p == ':' || *p == '\0') {
            if (count < max_parts) {
                size_t len = (size_t)(p - start);
                if (len >= 31) len = 31;
                memcpy(parts[count], start, len);
                parts[count][len] = '\0';
                count++;
            }
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    return count;
}

static StringList get_random_ipv6s(const StringList *ip_list) {
    StringList random_ips;
    string_list_init(&random_ips);
    if (!ip_list) return random_ips;

    for (size_t i = 0; i < ip_list->len; i++) {
        char subnet[256];
        snprintf(subnet, sizeof(subnet), "%s", ip_list->items[i]);
        trim_in_place(subnet);
        if (subnet[0] == '\0') continue;

        char *slash = strchr(subnet, '/');
        if (slash) *slash = '\0';

        char sections[16][32];
        int section_count = 0;
        memset(sections, 0, sizeof(sections));

        char *dbl = strstr(subnet, "::");
        if (dbl) {
            char left[256], right[256];
            size_t left_len = (size_t)(dbl - subnet);
            if (left_len >= sizeof(left)) left_len = sizeof(left) - 1;
            memcpy(left, subnet, left_len);
            left[left_len] = '\0';
            snprintf(right, sizeof(right), "%s", dbl + 2);

            char left_parts[8][32];
            char right_parts[8][32];
            int left_count = 0, right_count = 0;
            memset(left_parts, 0, sizeof(left_parts));
            memset(right_parts, 0, sizeof(right_parts));

            if (left[0] != '\0') left_count = split_colon_keep_empty(left, left_parts, 8);
            else {
                /* 贴近 Go 版 strings.Split("", ":") 的行为 */
                strcpy(left_parts[0], "");
                left_count = 1;
            }
            if (right[0] != '\0') right_count = split_colon_keep_empty(right, right_parts, 8);

            int missing = 8 - left_count - right_count;
            if (missing < 0) continue;
            for (int j = 0; j < left_count && section_count < 16; j++)
                copy_cstr(sections[section_count++], sizeof(sections[0]), left_parts[j]);
            for (int j = 0; j < missing && section_count < 16; j++)
                copy_cstr(sections[section_count++], sizeof(sections[0]), "0");
            for (int j = 0; j < right_count && section_count < 16; j++)
                copy_cstr(sections[section_count++], sizeof(sections[0]), right_parts[j]);
        } else {
            section_count = split_colon_keep_empty(subnet, sections, 16);
        }

        if (section_count >= 3) {
            char ip[256];
            snprintf(ip, sizeof(ip), "%s:%s:%s:%x:%x:%x:%x:%x",
                     sections[0], sections[1], sections[2],
                     next_random_intn(65536), next_random_intn(65536),
                     next_random_intn(65536), next_random_intn(65536),
                     next_random_intn(65536));
            string_list_push_dup(&random_ips, ip);
        }
    }
    return random_ips;
}

/* ----------------------- 数据中心位置解析 ----------------------- */

static uint32_t hash_iata(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

static void location_map_clear(void) {
    pthread_rwlock_wrlock(&location_lock);
    memset(location_table, 0, sizeof(location_table));
    pthread_rwlock_unlock(&location_lock);
}

static void location_map_insert_locked(const char *iata, const char *city) {
    if (!iata || iata[0] == '\0') return;
    const uint32_t h = hash_iata(iata);
    for (size_t i = 0; i < LOCATION_TABLE_SIZE; i++) {
        size_t idx = (h + i) % LOCATION_TABLE_SIZE;
        if (!location_table[idx].used || strcmp(location_table[idx].iata, iata) == 0) {
            location_table[idx].used = 1;
            snprintf(location_table[idx].iata, sizeof(location_table[idx].iata), "%s", iata);
            snprintf(location_table[idx].city, sizeof(location_table[idx].city), "%s", city ? city : "");
            return;
        }
    }
}

static int lookup_data_center(const char *colo, char *out, size_t out_size) {
    if (!colo || colo[0] == '\0') {
        if (out_size) out[0] = '\0';
        return 0;
    }
    pthread_rwlock_rdlock(&location_lock);
    const uint32_t h = hash_iata(colo);
    for (size_t i = 0; i < LOCATION_TABLE_SIZE; i++) {
        size_t idx = (h + i) % LOCATION_TABLE_SIZE;
        if (!location_table[idx].used) break;
        if (strcmp(location_table[idx].iata, colo) == 0) {
            if (location_table[idx].city[0] != '\0') {
                snprintf(out, out_size, "%s", location_table[idx].city);
            } else {
                snprintf(out, out_size, "%s", colo);
            }
            pthread_rwlock_unlock(&location_lock);
            return 1;
        }
    }
    pthread_rwlock_unlock(&location_lock);
    snprintf(out, out_size, "%s", colo);
    return 0;
}

static void append_utf8(char *out, size_t out_size, size_t *pos, unsigned codepoint) {
    unsigned char bytes[4];
    int count = 0;
    if (codepoint <= 0x7F) {
        bytes[count++] = (unsigned char)codepoint;
    } else if (codepoint <= 0x7FF) {
        bytes[count++] = (unsigned char)(0xC0 | (codepoint >> 6));
        bytes[count++] = (unsigned char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        bytes[count++] = (unsigned char)(0xE0 | (codepoint >> 12));
        bytes[count++] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[count++] = (unsigned char)(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0x10FFFF) {
        bytes[count++] = (unsigned char)(0xF0 | (codepoint >> 18));
        bytes[count++] = (unsigned char)(0x80 | ((codepoint >> 12) & 0x3F));
        bytes[count++] = (unsigned char)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[count++] = (unsigned char)(0x80 | (codepoint & 0x3F));
    }
    for (int i = 0; i < count && *pos + 1 < out_size; i++) {
        out[(*pos)++] = (char)bytes[i];
    }
    if (out_size) out[*pos < out_size ? *pos : out_size - 1] = '\0';
}

static int hex4(const char *p, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        unsigned x;
        if (c >= '0' && c <= '9') x = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') x = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') x = (unsigned)(c - 'A' + 10);
        else return 0;
        v = (v << 4) | x;
    }
    *out = v;
    return 1;
}

static const char *json_read_string(const char *p, const char *end, char *out, size_t out_size) {
    if (!p || p >= end || *p != '"') return NULL;
    p++;
    size_t pos = 0;
    if (out_size) out[0] = '\0';
    while (p < end && *p) {
        unsigned char c = (unsigned char)*p++;
        if (c == '"') {
            if (out_size) out[pos < out_size ? pos : out_size - 1] = '\0';
            return p;
        }
        if (c == '\\' && p < end) {
            char esc = *p++;
            switch (esc) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (p + 4 <= end && hex4(p, &cp)) {
                        p += 4;
                        /* 处理 UTF-16 surrogate pair */
                        if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= end && p[0] == '\\' && p[1] == 'u') {
                            unsigned low = 0;
                            if (hex4(p + 2, &low) && low >= 0xDC00 && low <= 0xDFFF) {
                                p += 6;
                                cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                            }
                        }
                        append_utf8(out, out_size, &pos, cp);
                        continue;
                    }
                    c = '?';
                    break;
                }
                default: c = (unsigned char)esc; break;
            }
        }
        if (pos + 1 < out_size) out[pos++] = (char)c;
    }
    if (out_size) out[pos < out_size ? pos : out_size - 1] = '\0';
    return NULL;
}

static int json_extract_string(const char *obj_start, const char *obj_end,
                               const char *key, char *out, size_t out_size) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = obj_start;
    size_t pat_len = strlen(pattern);
    while (p && p < obj_end) {
        const char *found = strstr(p, pattern);
        if (!found || found >= obj_end) break;
        p = found + pat_len;
        while (p < obj_end && isspace((unsigned char)*p)) p++;
        if (p >= obj_end || *p != ':') continue;
        p++;
        while (p < obj_end && isspace((unsigned char)*p)) p++;
        if (p < obj_end && *p == '"') {
            return json_read_string(p, obj_end, out, out_size) != NULL;
        }
    }
    if (out_size) out[0] = '\0';
    return 0;
}

/* ----------------------- 连接、RTT、HTTP 响应头检测 ----------------------- */

#ifndef _WIN32
static int set_fd_blocking(int fd, int blocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (blocking) flags &= ~O_NONBLOCK;
    else flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}
#endif

static int set_socket_timeout_ms(int fd, int timeout_ms) {
    if (timeout_ms < 1) timeout_ms = 1;
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) return -1;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) return -1;
    return 0;
}

static int connect_tcp_timeout(const char *ip, int port, int timeout_ms, int *tcp_ms_out) {
    struct sockaddr_storage ss;
    socklen_t ss_len = 0;
    memset(&ss, 0, sizeof(ss));

    if (strchr(ip, ':')) {
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&ss;
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons((uint16_t)port);
        if (inet_pton(AF_INET6, ip, &addr6->sin6_addr) != 1) return -1;
        ss_len = sizeof(*addr6);
    } else {
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&ss;
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons((uint16_t)port);
        if (inet_pton(AF_INET, ip, &addr4->sin_addr) != 1) return -1;
        ss_len = sizeof(*addr4);
    }

    int fd = socket(ss.ss_family, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    if (set_fd_blocking(fd, 0) != 0) {
        socket_close(fd);
        return -1;
    }

    long long start = now_ms();
    int rc = connect(fd, (struct sockaddr *)&ss, ss_len);
    if (rc != 0 && errno != EINPROGRESS) {
        socket_close(fd);
        return -1;
    }

    if (rc != 0) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        rc = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (rc <= 0) {
            socket_close(fd);
            return -1;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 || so_error != 0) {
            socket_close(fd);
            return -1;
        }
    }

    long long elapsed = now_ms() - start;
    if (tcp_ms_out) *tcp_ms_out = (int)elapsed;
    set_fd_blocking(fd, 1);
    return fd;
}

static int wait_fd(int fd, int want_write, int timeout_ms) {
    if (timeout_ms < 1) return -1;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int rc = select(fd + 1, want_write ? NULL : &fds, want_write ? &fds : NULL, NULL, &tv);
    return rc > 0 ? 0 : -1;
}

static int send_all_deadline(int fd, const char *buf, size_t len, long long deadline_ms) {
    size_t sent = 0;
    while (sent < len) {
        int rem = (int)(deadline_ms - now_ms());
        if (wait_fd(fd, 1, rem) != 0) return -1;
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int headers_have_cf_ray(const char *headers, size_t len) {
    size_t pos = 0;
    while (pos < len) {
        size_t line_start = pos;
        while (pos < len && headers[pos] != '\n') pos++;
        size_t line_end = pos;
        if (pos < len && headers[pos] == '\n') pos++;

        while (line_start < line_end && (headers[line_start] == '\r' || headers[line_start] == ' ' || headers[line_start] == '\t')) line_start++;
        if (line_end > line_start && headers[line_end - 1] == '\r') line_end--;
        if (line_end - line_start >= 7 && strncasecmp(headers + line_start, "CF-RAY:", 7) == 0) {
            return 1;
        }
    }
    return 0;
}

static int buffer_contains(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len) {
    if (!haystack || !needle || needle_len == 0) return 1;
    if (haystack_len < needle_len) return 0;

    size_t last = haystack_len - needle_len;
    for (size_t i = 0; i <= last; i++) {
        if (haystack[i] == needle[0] && memcmp(haystack + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int buffer_has_header_end(const char *buf, size_t len) {
    if (buffer_contains(buf, len, "\r\n\r\n", 4)) return 1;
    if (buffer_contains(buf, len, "\n\n", 2)) return 1;
    return 0;
}

static int read_headers_raw(int fd, long long deadline_ms) {
    char headers[MAX_HEADER_SIZE + 1];
    size_t len = 0;
    while (len < MAX_HEADER_SIZE) {
        int rem = (int)(deadline_ms - now_ms());
        if (wait_fd(fd, 0, rem) != 0) return -1;
        ssize_t n = recv(fd, headers + len, MAX_HEADER_SIZE - len, 0);
        if (n <= 0) return -1;
        len += (size_t)n;
        headers[len] = '\0';
        if (buffer_has_header_end(headers, len)) {
            return headers_have_cf_ray(headers, len) ? 0 : -1;
        }
    }
    return -1;
}

static void rtt_ssl_init_once(void) {
    rtt_ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (rtt_ssl_ctx) {
        SSL_CTX_set_verify(rtt_ssl_ctx, SSL_VERIFY_NONE, NULL);
        SSL_CTX_set_options(rtt_ssl_ctx, SSL_OP_NO_COMPRESSION);
    }
}

static int ssl_write_all(SSL *ssl, int fd, const char *buf, size_t len, long long deadline_ms) {
    size_t sent = 0;
    while (sent < len) {
        int rem = (int)(deadline_ms - now_ms());
        if (rem <= 0) return -1;
        set_socket_timeout_ms(fd, rem);
        int n = SSL_write(ssl, buf + sent, (int)(len - sent));
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return 0;
}

static int read_headers_ssl(SSL *ssl, int fd, long long deadline_ms) {
    char headers[MAX_HEADER_SIZE + 1];
    size_t len = 0;
    while (len < MAX_HEADER_SIZE) {
        int rem = (int)(deadline_ms - now_ms());
        if (rem <= 0) return -1;
        set_socket_timeout_ms(fd, rem);
        int n = SSL_read(ssl, headers + len, (int)(MAX_HEADER_SIZE - len));
        if (n <= 0) return -1;
        len += (size_t)n;
        headers[len] = '\0';
        if (buffer_has_header_end(headers, len)) {
            return headers_have_cf_ray(headers, len) ? 0 : -1;
        }
    }
    return -1;
}

static int test_rtt(const char *ip, int use_tls) {
    const int port = use_tls ? 443 : 80;
    int total_ms = 0;

    for (int i = 0; i < 3; i++) {
        long long start = now_ms();
        int tcp_ms = 0;
        int fd = connect_tcp_timeout(ip, port, 1000, &tcp_ms);
        if (fd < 0) return 0;
        total_ms += tcp_ms;

        long long deadline = start + 1000LL;
        int rem = (int)(deadline - now_ms());
        if (rem <= 0) {
            socket_close(fd);
            return 0;
        }
        set_socket_timeout_ms(fd, rem);

        const char *req = "GET / HTTP/1.1\r\n"
                          "Host: cloudflare.com\r\n"
                          "User-Agent: Mozilla/5.0\r\n"
                          "Connection: close\r\n\r\n";

        int ok = 0;
        if (use_tls) {
            pthread_once(&rtt_ssl_once, rtt_ssl_init_once);
            if (!rtt_ssl_ctx) {
                socket_close(fd);
                return 0;
            }
            SSL *ssl = SSL_new(rtt_ssl_ctx);
            if (!ssl) {
                socket_close(fd);
                return 0;
            }
            SSL_set_fd(ssl, fd);
            char hostname[] = "cloudflare.com";
            SSL_set_tlsext_host_name(ssl, hostname);
            rem = (int)(deadline - now_ms());
            if (rem > 0) set_socket_timeout_ms(fd, rem);
            if (rem > 0 && SSL_connect(ssl) == 1 &&
                ssl_write_all(ssl, fd, req, strlen(req), deadline) == 0 &&
                read_headers_ssl(ssl, fd, deadline) == 0) {
                ok = 1;
            }
            SSL_free(ssl);
        } else {
            if (send_all_deadline(fd, req, strlen(req), deadline) == 0 &&
                read_headers_raw(fd, deadline) == 0) {
                ok = 1;
            }
        }
        socket_close(fd);
        if (!ok) return 0;
    }
    return total_ms / 3;
}

/* ----------------------- 并发 RTT 测试 ----------------------- */

typedef struct {
    const StringList *ip_list;
    size_t next_index;
    size_t completed;
    int use_tls;
    int total;
    pthread_mutex_t index_mu;
    pthread_mutex_t result_mu;
    pthread_mutex_t progress_mu;
    RTTVector results;
} RTTContext;

static void *rtt_worker(void *arg) {
    RTTContext *ctx = (RTTContext *)arg;
    for (;;) {
        pthread_mutex_lock(&ctx->index_mu);
        size_t idx = ctx->next_index++;
        pthread_mutex_unlock(&ctx->index_mu);

        if (idx >= ctx->ip_list->len) break;

        const char *ip = ctx->ip_list->items[idx];
        int avg_ms = test_rtt(ip, ctx->use_tls);
        if (avg_ms > 0) {
            pthread_mutex_lock(&ctx->result_mu);
            rtt_vector_push(&ctx->results, ip, avg_ms);
            pthread_mutex_unlock(&ctx->result_mu);
        }

        pthread_mutex_lock(&ctx->progress_mu);
        ctx->completed++;
        size_t current = ctx->completed;
        pthread_mutex_unlock(&ctx->progress_mu);

        if (current % 10 == 0 || current == (size_t)ctx->total) {
            printf("RTT 测试进度: %zu/%d\n", current, ctx->total);
            fflush(stdout);
        }
    }
    return NULL;
}

static int compare_rtt_result(const void *a, const void *b) {
    const RTTResult *ra = (const RTTResult *)a;
    const RTTResult *rb = (const RTTResult *)b;
    return (ra->latency_ms > rb->latency_ms) - (ra->latency_ms < rb->latency_ms);
}

static RTTVector run_rtt_test(const StringList *ip_list, int task_num, int use_tls) {
    RTTVector empty;
    rtt_vector_init(&empty);
    if (!ip_list || ip_list->len == 0) return empty;

    if ((size_t)task_num > ip_list->len) task_num = (int)ip_list->len;
    if (task_num <= 0) return empty;

    RTTContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ip_list = ip_list;
    ctx.use_tls = use_tls;
    ctx.total = (int)ip_list->len;
    if (pthread_mutex_init(&ctx.index_mu, NULL) != 0) {
        fprintf(stderr, "错误: 初始化互斥锁失败\n");
        return empty;
    }
    if (pthread_mutex_init(&ctx.result_mu, NULL) != 0) {
        pthread_mutex_destroy(&ctx.index_mu);
        return empty;
    }
    if (pthread_mutex_init(&ctx.progress_mu, NULL) != 0) {
        pthread_mutex_destroy(&ctx.result_mu);
        pthread_mutex_destroy(&ctx.index_mu);
        return empty;
    }
    rtt_vector_init(&ctx.results);

    pthread_t *threads = (pthread_t *)calloc((size_t)task_num, sizeof(pthread_t));
    if (!threads) {
        pthread_mutex_destroy(&ctx.progress_mu);
        pthread_mutex_destroy(&ctx.result_mu);
        pthread_mutex_destroy(&ctx.index_mu);
        return empty;
    }

    for (int i = 0; i < task_num; i++) {
        if (pthread_create(&threads[i], NULL, rtt_worker, &ctx) != 0) {
            fprintf(stderr, "警告: 创建测试线程失败\n");
        }
    }
    for (int i = 0; i < task_num; i++) pthread_join(threads[i], NULL);
    free(threads);

    pthread_mutex_destroy(&ctx.index_mu);
    pthread_mutex_destroy(&ctx.result_mu);
    pthread_mutex_destroy(&ctx.progress_mu);

    qsort(ctx.results.items, ctx.results.len, sizeof(RTTResult), compare_rtt_result);

    if (ctx.results.len > 10) {
        printf("RTT 测试完成，%zu/%d 个 IP 有效，保留延迟最低的 10 个\n", ctx.results.len, ctx.total);
        ctx.results.len = 10;
    } else {
        printf("RTT 测试完成，%zu/%d 个 IP 有效\n", ctx.results.len, ctx.total);
    }
    return ctx.results;
}

/* ----------------------- 速度测试：使用 libcurl 复刻 Go 的自定义 Dial ----------------------- */

typedef struct {
    long long window_bytes;
    struct timespec window_start;
    int started;
    int max_speed_kbps;
    char cf_ray[256];
    char data_center[32];
} SpeedCtx;

static void extract_data_center(const char *cf_ray, char *out, size_t out_size) {
    if (!cf_ray || cf_ray[0] == '\0') {
        if (out_size) out[0] = '\0';
        return;
    }
    const char *dash = strrchr(cf_ray, '-');
    if (!dash || dash[1] == '\0') {
        if (out_size) out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%s", dash + 1);
    trim_in_place(out);
}

static size_t speed_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr;
    size_t bytes = size * nmemb;
    SpeedCtx *ctx = (SpeedCtx *)userdata;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    if (!ctx->started) {
        ctx->window_start = now;
        ctx->started = 1;
    }

    ctx->window_bytes += (long long)bytes;
    double elapsed = elapsed_seconds(&ctx->window_start, &now);
    if (elapsed >= 1.0) {
        int speed_kb = (int)((double)ctx->window_bytes / 1024.0 / elapsed);
        if (speed_kb > ctx->max_speed_kbps) ctx->max_speed_kbps = speed_kb;
        ctx->window_bytes = 0;
        ctx->window_start = now;
    }
    return bytes;
}

static void speed_finalize(SpeedCtx *ctx) {
    if (!ctx || !ctx->started || ctx->window_bytes <= 0) return;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed = elapsed_seconds(&ctx->window_start, &now);
    if (elapsed <= 0.0) return;

    int speed_kb = (int)((double)ctx->window_bytes / 1024.0 / elapsed);
    if (speed_kb > ctx->max_speed_kbps) ctx->max_speed_kbps = speed_kb;
}

static size_t speed_header_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
    size_t bytes = size * nitems;
    SpeedCtx *ctx = (SpeedCtx *)userdata;

    const char *colon = memchr(buffer, ':', bytes);
    if (colon) {
        size_t key_len = (size_t)(colon - buffer);
        while (key_len > 0 && isspace((unsigned char)buffer[key_len - 1])) key_len--;
        if (key_len == 6 && strncasecmp(buffer, "CF-RAY", 6) == 0) {
            size_t value_len = bytes - (size_t)(colon - buffer) - 1;
            if (value_len >= sizeof(ctx->cf_ray)) value_len = sizeof(ctx->cf_ray) - 1;
            memcpy(ctx->cf_ray, colon + 1, value_len);
            ctx->cf_ray[value_len] = '\0';
            trim_in_place(ctx->cf_ray);
            extract_data_center(ctx->cf_ray, ctx->data_center, sizeof(ctx->data_center));
        }
    }
    return bytes;
}

static void bracket_ipv6_if_needed(const char *ip, char *out, size_t out_size) {
    if (strchr(ip, ':') && ip[0] != '[') snprintf(out, out_size, "[%s]", ip);
    else snprintf(out, out_size, "%s", ip);
}

static SpeedResult run_speed_test_simple(const char *ip, int port, int use_tls) {
    SpeedResult result;
    memset(&result, 0, sizeof(result));

    if (speed_test_domain[0] == '\0' || speed_test_file[0] == '\0') {
        return result;
    }

    CURL *curl = curl_easy_init();
    if (!curl) return result;

    const char *scheme = use_tls ? "https" : "http";
    int default_url_port = use_tls ? 443 : 80;
    char url[2048];
    snprintf(url, sizeof(url), "%s://%s/%s", scheme, speed_test_domain, speed_test_file);

    char connect_ip[256];
    bracket_ipv6_if_needed(ip, connect_ip, sizeof(connect_ip));

    char connect_to_entry[2048];
    snprintf(connect_to_entry, sizeof(connect_to_entry), "%s:%d:%s:%d",
             speed_test_domain, default_url_port, connect_ip, port);
    struct curl_slist *connect_to = NULL;
    connect_to = curl_slist_append(connect_to, connect_to_entry);

    SpeedCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CONNECT_TO, connect_to);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, speed_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, speed_header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 32L * 1024L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (use_tls) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    }

    CURLcode res = curl_easy_perform(curl);
    speed_finalize(&ctx);

    long response_code = 0;
    curl_off_t downloaded = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &downloaded);

    if ((res == CURLE_OK || res == CURLE_OPERATION_TIMEDOUT) &&
        response_code >= 200 && response_code < 300 &&
        downloaded > 0) {
        curl_off_t connect_us = 0;
        if (curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME_T, &connect_us) == CURLE_OK) {
            result.tcp_ms = (int)(connect_us / 1000);
        }
        result.max_speed_kbps = ctx.max_speed_kbps;
        snprintf(result.data_center, sizeof(result.data_center), "%s", ctx.data_center);
    }

    curl_slist_free_all(connect_to);
    curl_easy_cleanup(curl);
    return result;
}

/* ----------------------- 数据文件下载与初始化 ----------------------- */

static void download_all_data(void) {
    char path[PATH_MAX];
    data_path("url.txt", path, sizeof(path));
    if (!file_exists(path)) {
        printf("本地 %s 不存在，正在下载...\n", path);
        size_t len = 0;
        char *content = get_url_content("https://www.baipiao.eu.org/cloudflare/url", &len);
        if (!content) {
            printf("下载测速 URL 失败\n");
            return;
        }
        if (save_to_file(path, content, len) != 0) {
            printf("保存测速 URL 失败\n");
            free(content);
            return;
        }
        free(content);
    }

    size_t url_len = 0;
    char *url_content = get_file_content(path, &url_len);
    if (!url_content) {
        printf("读取测速 URL 失败\n");
        return;
    }
    (void)url_len;
    trim_in_place(url_content);
    char *slash = strchr(url_content, '/');
    if (slash) {
        *slash = '\0';
        snprintf(speed_test_domain, sizeof(speed_test_domain), "%s", url_content);
        snprintf(speed_test_file, sizeof(speed_test_file), "%s", slash + 1);
    } else {
        printf("测速 URL 格式异常\n");
    }
    free(url_content);

    struct {
        const char *file;
        const char *url;
    } downloads[] = {
        {"ips-v4.txt", "https://www.baipiao.eu.org/cloudflare/ips-v4"},
        {"ips-v6.txt", "https://www.baipiao.eu.org/cloudflare/ips-v6"},
    };

    for (size_t i = 0; i < sizeof(downloads) / sizeof(downloads[0]); i++) {
        data_path(downloads[i].file, path, sizeof(path));
        if (!file_exists(path)) {
            printf("本地 %s 不存在，正在下载...\n", path);
            size_t len = 0;
            char *content = get_url_content(downloads[i].url, &len);
            if (!content) {
                printf("下载 IP 列表失败\n");
                return;
            }
            if (save_to_file(path, content, len) != 0) {
                printf("保存 IP 列表失败\n");
                free(content);
                return;
            }
            free(content);
        }
    }

    data_path("locations.json", path, sizeof(path));
    if (!file_exists(path)) {
        printf("本地 %s 不存在，正在下载...\n", path);
        size_t len = 0;
        char *content = get_url_content("https://www.baipiao.eu.org/cloudflare/locations", &len);
        if (!content) {
            printf("获取位置信息失败\n");
            return;
        }
        if (save_to_file(path, content, len) != 0) {
            printf("保存位置信息失败\n");
            free(content);
            return;
        }
        free(content);
    }
}

static void init_locations(void) {
    download_all_data();

    char path[PATH_MAX];
    data_path("locations.json", path, sizeof(path));
    size_t len = 0;
    char *body = get_file_content(path, &len);
    if (!body) {
        printf("读取位置文件失败\n");
        return;
    }

    location_map_clear();
    size_t loaded = 0;

    pthread_rwlock_wrlock(&location_lock);
    const char *p = body;
    const char *end = body + len;
    while (p < end) {
        const char *obj_start = strchr(p, '{');
        if (!obj_start || obj_start >= end) break;
        const char *obj_end = strchr(obj_start, '}');
        if (!obj_end || obj_end > end) break;

        char iata[32] = "";
        char city[256] = "";
        if (json_extract_string(obj_start, obj_end, "iata", iata, sizeof(iata))) {
            json_extract_string(obj_start, obj_end, "city", city, sizeof(city));
            location_map_insert_locked(iata, city);
            loaded++;
        }
        p = obj_end + 1;
    }
    pthread_rwlock_unlock(&location_lock);

    free(body);
    printf("已加载 %zu 个数据中心位置信息\n", loaded);
}

/* ----------------------- 主流程 ----------------------- */

static CloudflareResult cloudflare_test(int ip_type, int use_tls, int task_num, int speed_threshold_kbps) {
    CloudflareResult final_result;
    memset(&final_result, 0, sizeof(final_result));

    download_all_data();

    char filename[PATH_MAX];
    data_path(ip_type == 6 ? "ips-v6.txt" : "ips-v4.txt", filename, sizeof(filename));
    size_t content_len = 0;
    char *content = get_file_content(filename, &content_len);
    if (!content) {
        printf("读取 IP 列表失败\n");
        return final_result;
    }
    (void)content_len;

    StringList ip_list = parse_ip_list(content);
    free(content);

    printf("正在从 %zu 个子网中随机生成 IP...\n", ip_list.len);

    size_t sample_size = 100;
    if (ip_list.len < sample_size) sample_size = ip_list.len;

    for (;;) {
        RTTVector rtt_results;
        rtt_vector_init(&rtt_results);

        while (rtt_results.len == 0) {
            StringList sampled = random_sample(&ip_list, sample_size);
            StringList test_ips = ip_type == 6 ? get_random_ipv6s(&sampled) : get_random_ipv4s(&sampled);

            printf("已生成 %zu 个测试 IP，开始 RTT 测试...\n", test_ips.len);
            rtt_results = run_rtt_test(&test_ips, task_num, use_tls);

            string_list_free(&sampled);
            string_list_free(&test_ips);

            if (rtt_results.len > 0) break;
            rtt_vector_free(&rtt_results);
            printf("当前所有 IP 都存在 RTT 丢包，继续新的 RTT 测试...\n");
        }

        printf("待测速的 IP 地址\n");
        for (size_t i = 0; i < rtt_results.len; i++) {
            printf("%s 往返延迟 %d 毫秒\n", rtt_results.items[i].ip, rtt_results.items[i].latency_ms);
        }

        for (size_t i = 0; i < rtt_results.len; i++) {
            const char *ip = rtt_results.items[i].ip;
            printf("正在测试 %s\n", ip);
            int speed_port = use_tls ? 443 : 80;
            SpeedResult sr = run_speed_test_simple(ip, speed_port, use_tls);

            printf("%s 峰值速度 %d kB/s", ip, sr.max_speed_kbps);
            if (sr.data_center[0] != '\0') {
                char city[256];
                lookup_data_center(sr.data_center, city, sizeof(city));
                printf(", 数据中心 %s", city);
            }
            printf("\n");

            if (sr.max_speed_kbps >= speed_threshold_kbps) {
                snprintf(final_result.ip, sizeof(final_result.ip), "%s", ip);
                final_result.max_speed_kbps = sr.max_speed_kbps;
                final_result.tcp_ms = sr.tcp_ms;
                if (sr.data_center[0] != '\0') {
                    lookup_data_center(sr.data_center, final_result.data_center, sizeof(final_result.data_center));
                } else {
                    final_result.data_center[0] = '\0';
                }
                rtt_vector_free(&rtt_results);
                string_list_free(&ip_list);
                return final_result;
            }
        }

        rtt_vector_free(&rtt_results);
        printf("当前所有 IP 都未达到期望带宽，重新开始新一轮测试...\n");
    }
}

static void run_ip_selector(int ip_type, int use_tls) {
    int bandwidth = 1;
    int task_num = 50;
    char input[MAX_LINE_LEN];

    printf("请设置期望的带宽大小 (默认最小 1，单位 Mbps): ");
    if (read_line_trim(input, sizeof(input))) {
        if (input[0] == '\0') {
            bandwidth = 1;
        } else {
            char *endptr = NULL;
            long val = strtol(input, &endptr, 10);
            if (endptr == input || *endptr != '\0' || val <= 0) {
                printf("输入无效，已使用默认值 1 Mbps\n");
                bandwidth = 1;
            } else {
                bandwidth = (int)val;
            }
        }
    }

    printf("请设置 RTT 测试进程数 (默认 50，最大 100): ");
    if (read_line_trim(input, sizeof(input))) {
        if (input[0] == '\0') {
            task_num = 50;
        } else {
            char *endptr = NULL;
            long val = strtol(input, &endptr, 10);
            if (endptr == input || *endptr != '\0') {
                printf("输入无效，已使用默认值 50\n");
                task_num = 50;
            } else if (val <= 0) {
                printf("进程数不能为 0，自动设置为默认值\n");
                task_num = 50;
            } else {
                task_num = (int)val;
            }
            if (task_num > 100) {
                printf("超过最大进程限制，自动设置为最大值\n");
                task_num = 100;
            }
        }
    }

    int speed = bandwidth * 128;
    long long start = now_ms();
    CloudflareResult res = cloudflare_test(ip_type, use_tls, task_num, speed);
    long long end = now_ms();

    int real_bandwidth = res.max_speed_kbps / 128;
    printf("\n");
    printf("优选 IP: %s\n", res.ip);
    printf("设置带宽: %d Mbps\n", bandwidth);
    printf("实测带宽: %d Mbps\n", real_bandwidth);
    printf("峰值速度: %d kB/s\n", res.max_speed_kbps);
    printf("往返延迟: %d 毫秒\n", res.tcp_ms);
    printf("数据中心: %s\n", res.data_center);
    printf("总计用时: %lld 秒\n", (end - start) / 1000LL);
}

static void run_single_speed_test(int use_tls) {
    char input[MAX_LINE_LEN];
    char ip[MAX_IP_LEN];

    printf("请输入需要测速的 IP: ");
    if (!read_line_trim(ip, sizeof(ip))) return;

    int default_port = use_tls ? 443 : 80;
    int port = default_port;
    printf("请输入需要测速的端口 (默认%d): ", default_port);
    if (read_line_trim(input, sizeof(input))) {
        if (input[0] == '\0') {
            port = default_port;
        } else {
            char *endptr = NULL;
            long val = strtol(input, &endptr, 10);
            if (endptr == input || *endptr != '\0' || val <= 0 || val > 65535) {
                printf("输入无效，已使用默认端口 %d\n", default_port);
                port = default_port;
            } else {
                port = (int)val;
            }
        }
    }

    printf("正在测速 %s 端口 %d\n", ip, port);
    SpeedResult sr = run_speed_test_simple(ip, port, use_tls);
    if (sr.data_center[0] != '\0') {
        char city[256];
        lookup_data_center(sr.data_center, city, sizeof(city));
        printf("%s 平均速度 %d kB/s, TCP延迟 %dms, 数据中心=%s\n",
               ip, sr.max_speed_kbps, sr.tcp_ms, city);
    } else {
        printf("%s 平均速度 %d kB/s, TCP延迟 %dms\n", ip, sr.max_speed_kbps, sr.tcp_ms);
    }
}

static void clear_cache(void) {
    const char *files[] = {"locations.json", "ips-v4.txt", "ips-v6.txt", "url.txt"};
    char path[PATH_MAX];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        data_path(files[i], path, sizeof(path));
        remove(path);
    }
    printf("缓存已清空，下次操作会自动重新下载数据\n");
}

static void update_data(void) {
    printf("正在重新下载数据...\n");
    const char *files[] = {"locations.json", "ips-v4.txt", "ips-v6.txt", "url.txt"};
    char path[PATH_MAX];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        data_path(files[i], path, sizeof(path));
        remove(path);
    }
    init_locations();
}

static void run_single_speed_test_cli(const char *ip, int port, int use_tls) {
    if (!ip || ip[0] == '\0') {
        fprintf(stderr, "错误: 未指定 IP 地址\n");
        return;
    }
    int default_port = use_tls ? 443 : 80;
    if (port <= 0 || port > 65535) port = default_port;

    printf("正在测速 %s 端口 %d\n", ip, port);
    SpeedResult sr = run_speed_test_simple(ip, port, use_tls);

    if (opt_json_output) {
        char dc[256] = "";
        if (sr.data_center[0] != '\0') {
            lookup_data_center(sr.data_center, dc, sizeof(dc));
        }
        printf("{\n");
        printf("  \"ip\": \"%s\",\n", ip);
        printf("  \"max_speed_kbps\": %d,\n", sr.max_speed_kbps);
        printf("  \"tcp_ms\": %d,\n", sr.tcp_ms);
        printf("  \"data_center\": \"%s\"\n", dc[0] ? dc : "");
        printf("}\n");
    } else {
        if (sr.data_center[0] != '\0') {
            char city[256];
            lookup_data_center(sr.data_center, city, sizeof(city));
            printf("%s 平均速度 %d kB/s, TCP延迟 %dms, 数据中心=%s\n",
                   ip, sr.max_speed_kbps, sr.tcp_ms, city);
        } else {
            printf("%s 平均速度 %d kB/s, TCP延迟 %dms\n", ip, sr.max_speed_kbps, sr.tcp_ms);
        }
    }
}

static void show_menu(void) {
    char input[MAX_LINE_LEN];
    for (;;) {
        printf("----------------------------------------\n");
        printf("1. IPV4 优选 (TLS)\n");
        printf("2. IPV4 优选 (非 TLS)\n");
        printf("3. IPV6 优选 (TLS)\n");
        printf("4. IPV6 优选 (非 TLS)\n");
        printf("5. 单 IP 测速 (TLS)\n");
        printf("6. 单 IP 测速 (非 TLS)\n");
        printf("7. 清空缓存\n");
        printf("8. 更新数据\n");
        printf("0. 退出\n");
        printf("请选择菜单 (默认 0): ");

        if (!read_line_trim(input, sizeof(input))) break;
        if (input[0] == '\0') snprintf(input, sizeof(input), "0");

        if (strcmp(input, "0") == 0) {
            printf("退出成功\n");
            return;
        } else if (strcmp(input, "1") == 0) {
            run_ip_selector(4, 1);
        } else if (strcmp(input, "2") == 0) {
            run_ip_selector(4, 0);
        } else if (strcmp(input, "3") == 0) {
            run_ip_selector(6, 1);
        } else if (strcmp(input, "4") == 0) {
            run_ip_selector(6, 0);
        } else if (strcmp(input, "5") == 0) {
            run_single_speed_test(1);
        } else if (strcmp(input, "6") == 0) {
            run_single_speed_test(0);
        } else if (strcmp(input, "7") == 0) {
            clear_cache();
        } else if (strcmp(input, "8") == 0) {
            update_data();
        } else {
            printf("无效输入，请重新选择\n");
        }
    }
}

#ifndef UNIT_TESTING
int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
#ifdef _WIN32
    if (wsa_init() != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    /* 解析 --data-dir 旧式参数 (保持兼容) */
    const char *env_dir = getenv("BETTER_CF_IP_DATA_DIR");
    if (env_dir && env_dir[0] != '\0') {
        snprintf(data_dir, sizeof(data_dir), "%s", env_dir);
    }

    /* getopt 参数解析 */
    int opt_ip_type = 4;
    int opt_use_tls = 1;
    int opt_bandwidth = 1;
    int opt_task_num = 50;
    int opt_single_ip_mode = 0;
    int opt_port = 0;
    int opt_clear_cache_flag = 0;
    int opt_update_flag = 0;
    char opt_single_ip[MAX_IP_LEN] = "";

    static struct option long_options[] = {
        {"ipv4",       no_argument,       0, '4'},
        {"ipv6",       no_argument,       0, '6'},
        {"tls",        no_argument,       0, 't'},
        {"plain",      no_argument,       0, 'p'},
        {"json",       no_argument,       0, 'j'},
        {"noninteractive", no_argument,   0, 'n'},
        {"bandwidth",  required_argument, 0, 'b'},
        {"single",     required_argument, 0, 's'},
        {"port",       required_argument, 0, 'P'},
        {"clear-cache",no_argument,       0, 'c'},
        {"update",     no_argument,       0, 'u'},
        {"data-dir",   required_argument, 0, 'd'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "46tpjnb:s:P:cud:h", long_options, NULL)) != -1) {
        switch (c) {
            case '4': opt_ip_type = 4; break;
            case '6': opt_ip_type = 6; break;
            case 't': opt_use_tls = 1; break;
            case 'p': opt_use_tls = 0; break;
            case 'j': opt_json_output = 1; /* fall through */
            case 'n': opt_noninteractive = 1; break;
            case 'b': opt_bandwidth = (int)strtol(optarg, NULL, 10); break;
            case 's': opt_single_ip_mode = 1; snprintf(opt_single_ip, sizeof(opt_single_ip), "%s", optarg); break;
            case 'P': opt_port = (int)strtol(optarg, NULL, 10); break;
            case 'c': opt_clear_cache_flag = 1; break;
            case 'u': opt_update_flag = 1; break;
            case 'd': snprintf(data_dir, sizeof(data_dir), "%s", optarg); break;
            case 'h':
                printf("用法: better-cf-ip-c [选项]\n");
                printf("\n选项:\n");
                printf("  -4, --ipv4             测试 IPv4（默认）\n");
                printf("  -6, --ipv6             测试 IPv6\n");
                printf("  -t, --tls              使用 TLS（默认）\n");
                printf("  -p, --plain            不使用 TLS\n");
                printf("  -j, --json             输出 JSON 格式\n");
                printf("  -n, --noninteractive   非交互模式\n");
                printf("  -b, --bandwidth N      设置目标带宽 (Mbps，默认 1)\n");
                printf("  -s, --single IP        单 IP 测速\n");
                printf("  -P, --port PORT        测速端口（默认 443/80）\n");
                printf("  -c, --clear-cache      清空缓存\n");
                printf("  -u, --update           更新数据\n");
                printf("  -d, --data-dir DIR     数据目录\n");
                printf("  -h, --help             显示帮助信息\n");
                return 0;
            default:
                fprintf(stderr, "用法: better-cf-ip-c [选项]\n");
                fprintf(stderr, "使用 -h 查看帮助\n");
                return 1;
        }
    }

    init_random();
    curl_global_init(CURL_GLOBAL_DEFAULT);
    OPENSSL_init_ssl(0, NULL);

    init_locations();

    if (opt_clear_cache_flag) {
        clear_cache();
        if (opt_noninteractive) goto cleanup;
    }
    if (opt_update_flag) {
        update_data();
        if (opt_noninteractive) goto cleanup;
    }

    if (opt_single_ip_mode) {
        run_single_speed_test_cli(opt_single_ip, opt_port, opt_use_tls);
        goto cleanup;
    }

    if (opt_noninteractive) {
        int speed = opt_bandwidth * 128;
        long long start = now_ms();
        CloudflareResult res = cloudflare_test(opt_ip_type, opt_use_tls, opt_task_num, speed);
        long long end = now_ms();
        int real_bandwidth = res.max_speed_kbps / 128;

        if (opt_json_output) {
            printf("{\n");
            printf("  \"ip\": \"%s\",\n", res.ip);
            printf("  \"max_speed_kbps\": %d,\n", res.max_speed_kbps);
            printf("  \"tcp_ms\": %d,\n", res.tcp_ms);
            printf("  \"real_bandwidth_mbps\": %d,\n", real_bandwidth);
            printf("  \"target_bandwidth_mbps\": %d,\n", opt_bandwidth);
            printf("  \"data_center\": \"%s\",\n", res.data_center);
            printf("  \"elapsed_seconds\": %lld\n", (end - start) / 1000LL);
            printf("}\n");
        } else {
            printf("优选 IP: %s\n", res.ip);
            printf("实测带宽: %d Mbps\n", real_bandwidth);
            printf("峰值速度: %d kB/s\n", res.max_speed_kbps);
            printf("往返延迟: %d 毫秒\n", res.tcp_ms);
            printf("数据中心: %s\n", res.data_center);
            printf("总计用时: %lld 秒\n", (end - start) / 1000LL);
        }
        goto cleanup;
    }

    show_menu();

cleanup:
    if (rtt_ssl_ctx) SSL_CTX_free(rtt_ssl_ctx);
    curl_global_cleanup();
    return 0;
}
#endif
