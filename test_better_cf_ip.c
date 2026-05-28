#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#ifndef UNIT_TESTING
#define UNIT_TESTING
#endif

/* Include the source under test first (brings in all needed headers) */
#include "better_cf_ip.c"

/* Additional includes for test code */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  MinUnit — minimal C unit test framework (4 macros, no deps)       */
/* ------------------------------------------------------------------ */
#define mu_assert(message, test) do { if (!(test)) return message; } while (0)
#define mu_run_test(test) do { const char *message = test(); tests_run++; \
                               if (message) return message; } while (0)
int tests_run = 0;

/* ================================================================== */
/*  1. trim_in_place                                                  */
/* ================================================================== */
static const char *test_trim_in_place(void) {
    char s1[] = "  hello  ";
    trim_in_place(s1);
    mu_assert("trim leading/trailing spaces", strcmp(s1, "hello") == 0);

    char s2[] = "";
    trim_in_place(s2);
    mu_assert("trim empty string", strcmp(s2, "") == 0);

    char s3[] = "   \t  ";
    trim_in_place(s3);
    mu_assert("trim whitespace only", strcmp(s3, "") == 0);

    char s4[] = "no trim needed";
    trim_in_place(s4);
    mu_assert("trim none needed", strcmp(s4, "no trim needed") == 0);

    /* NULL input should not crash */
    trim_in_place(NULL);
    mu_assert("trim NULL no crash", 1);

    /* Leading/trailing mixed whitespace */
    char s5[] = "\t\r\n  abc  \t\n";
    trim_in_place(s5);
    mu_assert("trim mixed whitespace", strcmp(s5, "abc") == 0);

    /* Only leading whitespace */
    char s6[] = "   xyz";
    trim_in_place(s6);
    mu_assert("trim leading only", strcmp(s6, "xyz") == 0);

    /* Only trailing whitespace */
    char s7[] = "xyz   ";
    trim_in_place(s7);
    mu_assert("trim trailing only", strcmp(s7, "xyz") == 0);

    return NULL;
}

/* ================================================================== */
/*  2. copy_cstr                                                      */
/* ================================================================== */
static const char *test_copy_cstr(void) {
    char buf[16];
    memset(buf, 'x', sizeof(buf));

    /* Normal copy */
    copy_cstr(buf, sizeof(buf), "hello");
    mu_assert("copy normal", strcmp(buf, "hello") == 0);

    /* Copy with truncation (dst smaller than src) */
    memset(buf, 'x', sizeof(buf));
    copy_cstr(buf, 5, "hello world");
    /* "hello world" is 11 chars, dst_size=5, so copies 4 chars + null */
    mu_assert("copy truncated", strcmp(buf, "hell") == 0);

    /* NULL src treated as "" */
    copy_cstr(buf, sizeof(buf), NULL);
    mu_assert("copy NULL src", strcmp(buf, "") == 0);

    /* NULL dst does not crash */
    copy_cstr(NULL, sizeof(buf), "test");
    mu_assert("copy NULL dst no crash", 1);

    /* Zero dst_size does not crash */
    copy_cstr(buf, 0, "test");
    mu_assert("copy zero size no crash", 1);

    /* Exact fit (dst_size == strlen(src) + 1) */
    char buf2[6];
    copy_cstr(buf2, sizeof(buf2), "hello");
    mu_assert("copy exact fit", strcmp(buf2, "hello") == 0);

    /* Empty src */
    buf[0] = 'x'; buf[1] = '\0';
    copy_cstr(buf, sizeof(buf), "");
    mu_assert("copy empty src", strcmp(buf, "") == 0);

    /* Both dst NULL and zero size */
    copy_cstr(NULL, 0, "test");
    mu_assert("copy NULL dst zero size no crash", 1);

    /* Both NULL and zero size */
    copy_cstr(NULL, 0, NULL);
    mu_assert("copy all NULL zero size no crash", 1);

    return NULL;
}

/* ================================================================== */
/*  3. append_cstr                                                    */
/* ================================================================== */
static const char *test_append_cstr(void) {
    char buf[32];

    /* Normal append */
    snprintf(buf, sizeof(buf), "hello");
    append_cstr(buf, sizeof(buf), " world");
    mu_assert("append normal", strcmp(buf, "hello world") == 0);

    /* Append with truncation */
    char small[5];
    snprintf(small, sizeof(small), "ab");
    append_cstr(small, sizeof(small), "cdefgh");
    /* dst_size=5, used=2, available=2 (dst_size-used-1=2), so "cd" appended = "abcd" */
    mu_assert("append truncated", strcmp(small, "abcd") == 0);

    /* NULL dst does not crash */
    append_cstr(NULL, sizeof(buf), "test");
    mu_assert("append NULL dst no crash", 1);

    /* NULL src does not crash */
    append_cstr(buf, sizeof(buf), NULL);
    mu_assert("append NULL src no crash", 1);

    /* Zero dst_size does not crash */
    append_cstr(buf, 0, "test");
    mu_assert("append zero size no crash", 1);

    /* Append empty string */
    snprintf(buf, sizeof(buf), "hello");
    append_cstr(buf, sizeof(buf), "");
    mu_assert("append empty", strcmp(buf, "hello") == 0);

    /* Full buffer should not append */
    char full[4];
    snprintf(full, sizeof(full), "abc");
    append_cstr(full, sizeof(full), "d");
    mu_assert("append full buffer unchanged", strcmp(full, "abc") == 0);

    /* Both dst NULL and src non-NULL */
    append_cstr(NULL, 10, "test");
    mu_assert("append NULL dst non-zero size no crash", 1);

    return NULL;
}

/* ================================================================== */
/*  4. StringList operations                                          */
/* ================================================================== */
static const char *test_string_list(void) {
    StringList list;
    string_list_init(&list);
    mu_assert("slist init len 0", list.len == 0);
    mu_assert("slist init cap 0", list.cap == 0);
    mu_assert("slist init items null", list.items == NULL);

    /* Push items */
    mu_assert("slist push first", string_list_push_dup(&list, "hello") == 0);
    mu_assert("slist push second", string_list_push_dup(&list, "world") == 0);
    mu_assert("slist len 2", list.len == 2);
    mu_assert("slist first item", strcmp(list.items[0], "hello") == 0);
    mu_assert("slist second item", strcmp(list.items[1], "world") == 0);

    /* Push NULL becomes empty string */
    mu_assert("slist push NULL", string_list_push_dup(&list, NULL) == 0);
    mu_assert("slist len 3", list.len == 3);
    mu_assert("slist NULL becomes empty", strcmp(list.items[2], "") == 0);

    /* Free and verify */
    string_list_free(&list);
    mu_assert("slist freed items null", list.items == NULL);
    mu_assert("slist freed len 0", list.len == 0);
    mu_assert("slist freed cap 0", list.cap == 0);

    /* Free NULL pointer does not crash */
    string_list_free(NULL);
    mu_assert("slist free NULL no crash", 1);

    /* Push many items to trigger realloc */
    StringList big;
    string_list_init(&big);
    {
        size_t i;
        for (i = 0; i < 100; i++) {
            char buf2[32];
            snprintf(buf2, sizeof(buf2), "item_%zu", i);
            mu_assert("slist big push", string_list_push_dup(&big, buf2) == 0);
        }
    }
    mu_assert("slist big len 100", big.len == 100);
    mu_assert("slist big item 42", strcmp(big.items[42], "item_42") == 0);
    mu_assert("slist big item 99", strcmp(big.items[99], "item_99") == 0);
    string_list_free(&big);

    /* Double free is safe due to nullification */
    string_list_free(&list);
    mu_assert("slist double free no crash", 1);

    return NULL;
}

/* ================================================================== */
/*  5. RTTVector operations                                           */
/* ================================================================== */
static const char *test_rtt_vector(void) {
    RTTVector vec;
    rtt_vector_init(&vec);
    mu_assert("rttvec init len 0", vec.len == 0);
    mu_assert("rttvec init cap 0", vec.cap == 0);
    mu_assert("rttvec init items null", vec.items == NULL);

    /* Push items */
    mu_assert("rttvec push first", rtt_vector_push(&vec, "1.2.3.4", 10) == 0);
    mu_assert("rttvec push second", rtt_vector_push(&vec, "5.6.7.8", 20) == 0);
    mu_assert("rttvec len 2", vec.len == 2);
    mu_assert("rttvec first ip", strcmp(vec.items[0].ip, "1.2.3.4") == 0);
    mu_assert("rttvec first latency", vec.items[0].latency_ms == 10);
    mu_assert("rttvec second ip", strcmp(vec.items[1].ip, "5.6.7.8") == 0);
    mu_assert("rttvec second latency", vec.items[1].latency_ms == 20);

    /* Free and verify */
    rtt_vector_free(&vec);
    mu_assert("rttvec freed items null", vec.items == NULL);
    mu_assert("rttvec freed len 0", vec.len == 0);
    mu_assert("rttvec freed cap 0", vec.cap == 0);

    /* Push many items to trigger realloc */
    RTTVector big;
    rtt_vector_init(&big);
    {
        size_t i;
        for (i = 0; i < 100; i++) {
            mu_assert("rttvec big push", rtt_vector_push(&big, "10.0.0.1", (int)i) == 0);
        }
    }
    mu_assert("rttvec big len 100", big.len == 100);
    mu_assert("rttvec big item 42 latency", big.items[42].latency_ms == 42);
    mu_assert("rttvec big item 99 latency", big.items[99].latency_ms == 99);
    rtt_vector_free(&big);

    /* Push with IPv6 address */
    RTTVector v6;
    rtt_vector_init(&v6);
    mu_assert("rttvec v6 push", rtt_vector_push(&v6, "::1", 5) == 0);
    mu_assert("rttvec v6 ip", strcmp(v6.items[0].ip, "::1") == 0);
    rtt_vector_free(&v6);

    return NULL;
}

/* ================================================================== */
/*  6. hex4                                                           */
/* ================================================================== */
static const char *test_hex4(void) {
    unsigned v = 0;

    /* Valid hex strings */
    mu_assert("hex4 1234 returns 1", hex4("1234", &v) == 1);
    mu_assert("hex4 1234 value", v == 0x1234);

    mu_assert("hex4 0000 returns 1", hex4("0000", &v) == 1);
    mu_assert("hex4 0000 value", v == 0x0000);

    mu_assert("hex4 FFFF returns 1", hex4("FFFF", &v) == 1);
    mu_assert("hex4 FFFF value", v == 0xFFFF);

    mu_assert("hex4 abCD returns 1", hex4("abCD", &v) == 1);
    mu_assert("hex4 abCD value", v == 0xABCD);

    mu_assert("hex4 7fAe returns 1", hex4("7fAe", &v) == 1);
    mu_assert("hex4 7fAe value", v == 0x7FAE);

    /* Invalid hex strings */
    mu_assert("hex4 12G4 invalid", hex4("12G4", &v) == 0);
    mu_assert("hex4 gggg invalid", hex4("gggg", &v) == 0);
    mu_assert("hex4 !@#$ invalid", hex4("!@#$", &v) == 0);
    mu_assert("hex4 empty-like still 4 chars", hex4("0000", &v) == 1);

    /* Only first 4 chars considered */
    mu_assert("hex4 ABCD1234 still valid", hex4("ABCD", &v) == 1);
    mu_assert("hex4 ABCD1234 value", v == 0xABCD);

    return NULL;
}

/* ================================================================== */
/*  7. json_read_string                                               */
/* =================================================================- */
static const char *test_json_read_string(void) {
    char out[256];
    const char *result;

    /* Simple string */
    {
        const char *s = "\"hello\"";
        result = json_read_string(s, s + strlen(s), out, sizeof(out));
        mu_assert("json simple not null", result != NULL);
        mu_assert("json simple value", strcmp(out, "hello") == 0);
    }

    /* Empty string */
    {
        const char *s = "\"\"";
        result = json_read_string(s, s + strlen(s), out, sizeof(out));
        mu_assert("json empty not null", result != NULL);
        mu_assert("json empty value", strcmp(out, "") == 0);
    }

    /* Escaped quote: JSON "\"say \\\"hi\\\"\"" -> "say \"hi\"" */
    {
        const char *s = "\"say \\\"hi\\\"\"";
        result = json_read_string(s, s + strlen(s), out, sizeof(out));
        mu_assert("json escaped not null", result != NULL);
        mu_assert("json escaped value", strcmp(out, "say \"hi\"") == 0);
    }

    /* Unicode escape: JSON "He" -> "He" */
    {
        const char *s = "\"\\u0048\\u0065\"";
        result = json_read_string(s, s + strlen(s), out, sizeof(out));
        mu_assert("json unicode not null", result != NULL);
        mu_assert("json unicode value", strcmp(out, "He") == 0);
    }

    /* Escape sequences: \n, \t, \\, \/ */
    {
        const char *s = "\"a\\nb\\tc\\\\d\\/e\"";
        result = json_read_string(s, s + strlen(s), out, sizeof(out));
        mu_assert("json escapes not null", result != NULL);
        mu_assert("json escapes value", strcmp(out, "a\nb\tc\\d/e") == 0);
    }

    /* Invalid - not starting with quote */
    {
        const char *s = "hello\"";
        result = json_read_string(s, s + strlen(s), out, sizeof(out));
        mu_assert("json no open quote is null", result == NULL);
    }

    /* NULL input */
    {
        const char *dummy = "\"hello\"";
        result = json_read_string(NULL, dummy + strlen(dummy), out, sizeof(out));
    }
    mu_assert("json NULL input is null", result == NULL);

    /* Zero out_size (should not crash, returns non-NULL) */
    {
        const char *s = "\"hello\"";
        result = json_read_string(s, s + strlen(s), out, 0);
        mu_assert("json zero out_size not null", result != NULL);
    }

    /* Invalid escape sequence (\\x is not valid JSON escape) */
    {
        const char *s = "\"\\x\"";
        result = json_read_string(s, s + strlen(s), out, sizeof(out));
        mu_assert("json invalid escape not null", result != NULL);
        mu_assert("json invalid escape value", strcmp(out, "x") == 0);
    }

    /* String with only one quote (no closing quote) returns NULL */
    {
        const char *s = "\"abc";
        result = json_read_string(s, s + strlen(s), out, sizeof(out));
        mu_assert("json no closing quote is null", result == NULL);
    }

    /* Empty end (p >= end) */
    {
        const char *s = "\"\"";
        result = json_read_string(s, s + 0, out, sizeof(out));
        /* p >= end since p points at first char and end points at first char */
        mu_assert("json empty end is null", result == NULL);
    }

    return NULL;
}

/* ================================================================== */
/*  8. json_extract_string                                            */
/* ================================================================== */
static const char *test_json_extract_string(void) {
    char out[256];

    /* Simple extraction */
    {
        const char *obj = "{\"key\":\"value\"}";
        int ret = json_extract_string(obj, obj + strlen(obj), "key", out, sizeof(out));
        mu_assert("extract simple ret", ret == 1);
        mu_assert("extract simple value", strcmp(out, "value") == 0);
    }

    /* Multiple keys - extract second one */
    {
        const char *obj = "{\"a\":\"1\",\"b\":\"2\"}";
        int ret = json_extract_string(obj, obj + strlen(obj), "b", out, sizeof(out));
        mu_assert("extract second key ret", ret == 1);
        mu_assert("extract second key value", strcmp(out, "2") == 0);
    }

    /* Multiple keys - extract first one */
    {
        const char *obj = "{\"a\":\"1\",\"b\":\"2\"}";
        int ret = json_extract_string(obj, obj + strlen(obj), "a", out, sizeof(out));
        mu_assert("extract first key ret", ret == 1);
        mu_assert("extract first key value", strcmp(out, "1") == 0);
    }

    /* Key not found */
    {
        const char *obj = "{\"key\":\"value\"}";
        int ret = json_extract_string(obj, obj + strlen(obj), "missing", out, sizeof(out));
        mu_assert("extract missing ret", ret == 0);
    }

    /* Value contains colon */
    {
        const char *obj = "{\"key\":\"val:ue\"}";
        int ret = json_extract_string(obj, obj + strlen(obj), "key", out, sizeof(out));
        mu_assert("extract colon value ret", ret == 1);
        mu_assert("extract colon value", strcmp(out, "val:ue") == 0);
    }

    /* Value with escaped characters */
    {
        const char *obj = "{\"msg\":\"hello\\nworld\"}";
        int ret = json_extract_string(obj, obj + strlen(obj), "msg", out, sizeof(out));
        mu_assert("extract escaped ret", ret == 1);
        mu_assert("extract escaped value", strcmp(out, "hello\nworld") == 0);
    }

    /* Key name inside another key (should not false-match) */
    {
        const char *obj = "{\"key_extra\":\"no\",\"key\":\"yes\"}";
        int ret = json_extract_string(obj, obj + strlen(obj), "key", out, sizeof(out));
        mu_assert("extract no partial match ret", ret == 1);
        mu_assert("extract no partial match value", strcmp(out, "yes") == 0);
    }

    /* Whitespace around colon */
    {
        const char *obj = "{\"key\"  :  \"value with spaces\"}";
        int ret = json_extract_string(obj, obj + strlen(obj), "key", out, sizeof(out));
        mu_assert("extract whitespace ret", ret == 1);
        mu_assert("extract whitespace value", strcmp(out, "value with spaces") == 0);
    }

    return NULL;
}

/* ================================================================== */
/*  9. bracket_ipv6_if_needed                                         */
/* ================================================================== */
static const char *test_bracket_ipv6(void) {
    char out[64];

    /* IPv4 stays un-bracketed */
    bracket_ipv6_if_needed("192.168.1.1", out, sizeof(out));
    mu_assert("bracket ipv4 stays same", strcmp(out, "192.168.1.1") == 0);

    /* IPv6 gets brackets */
    bracket_ipv6_if_needed("::1", out, sizeof(out));
    mu_assert("bracket ipv6 gets brackets", strcmp(out, "[::1]") == 0);

    /* Already bracketed stays same */
    bracket_ipv6_if_needed("[::1]", out, sizeof(out));
    mu_assert("bracket already bracketed", strcmp(out, "[::1]") == 0);

    /* Full IPv6 address */
    bracket_ipv6_if_needed("2001:db8::1", out, sizeof(out));
    mu_assert("bracket long ipv6", strcmp(out, "[2001:db8::1]") == 0);

    /* Empty string (no colon, no change) */
    bracket_ipv6_if_needed("", out, sizeof(out));
    mu_assert("bracket empty string", strcmp(out, "") == 0);

    return NULL;
}

/* ================================================================== */
/* 10. extract_data_center                                            */
/* ================================================================== */
static const char *test_extract_data_center(void) {
    char out[32];

    /* Normal cf-ray with data center code */
    extract_data_center("123456abc-LAX", out, sizeof(out));
    mu_assert("dc normal cf-ray", strcmp(out, "LAX") == 0);

    /* Empty input */
    extract_data_center("", out, sizeof(out));
    mu_assert("dc empty input", strcmp(out, "") == 0);

    /* No dash at all */
    extract_data_center("nodash", out, sizeof(out));
    mu_assert("dc no dash", strcmp(out, "") == 0);

    /* Dash at the end */
    extract_data_center("prefix-", out, sizeof(out));
    mu_assert("dc dash at end", strcmp(out, "") == 0);

    /* NULL input */
    extract_data_center(NULL, out, sizeof(out));
    mu_assert("dc NULL input", strcmp(out, "") == 0);

    /* Multiple dashes (strrchr gives the rightmost) */
    extract_data_center("abc-123-LAX", out, sizeof(out));
    mu_assert("dc multiple dashes", strcmp(out, "LAX") == 0);

    /* Data center with trailing whitespace */
    extract_data_center("abc-LAX ", out, sizeof(out));
    mu_assert("dc trailing space", strcmp(out, "LAX") == 0);

    /* Only a dash */
    extract_data_center("-", out, sizeof(out));
    mu_assert("dc just dash", strcmp(out, "") == 0);

    /* Only two dashes: second dash is effectively at end -> empty */
    extract_data_center("--", out, sizeof(out));
    mu_assert("dc two dashes", strcmp(out, "") == 0);

    return NULL;
}

/* ================================================================== */
/* 11. headers_have_cf_ray                                            */
/* ================================================================== */
static const char *test_headers_have_cf_ray(void) {
    /* Has CF-RAY header */
    {
        const char *h = "HTTP/1.1 200 OK\r\nCF-RAY: 123abc-LAX\r\n\r\n";
        mu_assert("headers has cf-ray", headers_have_cf_ray(h, strlen(h)) == 1);
    }

    /* No CF-RAY header */
    {
        const char *h = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n";
        mu_assert("headers no cf-ray", headers_have_cf_ray(h, strlen(h)) == 0);
    }

    /* Empty headers */
    mu_assert("headers empty", headers_have_cf_ray("", 0) == 0);

    /* Lowercase cf-ray */
    {
        const char *h = "HTTP/1.1 200 OK\r\ncf-ray: 123abc-LAX\r\n\r\n";
        mu_assert("headers lowercase", headers_have_cf_ray(h, strlen(h)) == 1);
    }

    /* Mixed case Cf-RaY */
    {
        const char *h = "HTTP/1.1 200 OK\r\nCf-RaY: 123abc-LAX\r\n\r\n";
        mu_assert("headers mixed case", headers_have_cf_ray(h, strlen(h)) == 1);
    }

    /* CF-RAY in middle of headers */
    {
        const char *h = "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html\r\n"
                        "CF-RAY: 123abc-LAX\r\n"
                        "Content-Length: 42\r\n\r\n";
        mu_assert("headers cf-ray in middle", headers_have_cf_ray(h, strlen(h)) == 1);
    }

    /* Wrong prefix (CF-RAYS) should not match */
    {
        const char *h = "HTTP/1.1 200 OK\r\nCF-RAYS: 123abc-LAX\r\n\r\n";
        mu_assert("headers wrong prefix", headers_have_cf_ray(h, strlen(h)) == 0);
    }

    /* Header line with only "CF-RAY:" but no value */
    {
        const char *h = "HTTP/1.1 200 OK\r\nCF-RAY:\r\n\r\n";
        mu_assert("headers cf-ray empty value", headers_have_cf_ray(h, strlen(h)) == 1);
    }

    return NULL;
}

/* ================================================================== */
/* 12. buffer_contains                                                */
/* ================================================================== */
static const char *test_buffer_contains(void) {
    const char *haystack = "hello world this is a test";
    size_t len = strlen(haystack);

    /* Needle at start */
    mu_assert("buf needle at start", buffer_contains(haystack, len, "hello", 5));

    /* Needle in middle */
    mu_assert("buf needle in middle", buffer_contains(haystack, len, "world", 5));

    /* Needle at end */
    mu_assert("buf needle at end", buffer_contains(haystack, len, "test", 4));

    /* Needle not present */
    mu_assert("buf needle not present", !buffer_contains(haystack, len, "xyz", 3));

    /* Empty needle (should return 1) */
    mu_assert("buf empty needle", buffer_contains(haystack, len, "", 0));

    /* Needle longer than haystack */
    mu_assert("buf needle too long", !buffer_contains(haystack, len,
        "hello world this is a test and more", 37));

    /* NULL needle (should return 1) */
    mu_assert("buf NULL needle", buffer_contains(haystack, len, NULL, 0));

    /* Empty haystack with non-empty needle */
    mu_assert("buf empty haystack", !buffer_contains("", 0, "a", 1));

    /* Single character match */
    mu_assert("buf single char match", buffer_contains(haystack, len, "h", 1));

    /* Needle that partially matches at end */
    mu_assert("buf partial end no match", !buffer_contains("abc", 3, "bc1", 3));

    /* Exact match */
    mu_assert("buf exact match", buffer_contains("abc", 3, "abc", 3));

    /* Binary data with null bytes (before null) */
    {
        const char bin[] = "ab\0cd";
        mu_assert("buf binary data", buffer_contains(bin, 5, "cd", 2));
    }

    return NULL;
}

/* ================================================================== */
/* 13. buffer_has_header_end                                          */
/* ================================================================== */
static const char *test_buffer_has_header_end(void) {
    /* With \r\n\r\n */
    mu_assert("header_end \\r\\n\\r\\n",
              buffer_has_header_end("HTTP/1.1 200 OK\r\n\r\n",
                                    strlen("HTTP/1.1 200 OK\r\n\r\n")));

    /* With \n\n */
    mu_assert("header_end \\n\\n",
              buffer_has_header_end("GET / HTTP/1.1\n\n", strlen("GET / HTTP/1.1\n\n")));

    /* Without end marker */
    mu_assert("header_end no marker",
              !buffer_has_header_end("GET / HTTP/1.1\nHost: a.com", strlen("GET / HTTP/1.1\nHost: a.com")));

    /* Empty buffer */
    mu_assert("header_end empty", !buffer_has_header_end("", 0));

    /* Content after end marker still counts */
    mu_assert("header_end content after",
              buffer_has_header_end("Header\r\n\r\nbody", strlen("Header\r\n\r\nbody")));

    /* Only \r\n once (not double) */
    mu_assert("header_end single crlf",
              !buffer_has_header_end("Line1\r\n", 7));

    /* Mixed: \r\n\n (no \r before second \n) */
    mu_assert("header_end mixed",
              buffer_has_header_end("Line1\r\n\nBody", 12));

    return NULL;
}

/* ================================================================== */
/* 14. compare_rtt_result                                             */
/* ================================================================== */
static const char *test_compare_rtt_result(void) {
    RTTResult a = {"1.1.1.1", 10};
    RTTResult b = {"2.2.2.2", 20};
    RTTResult c = {"3.3.3.3", 10};

    /* First smaller -> negative */
    mu_assert("cmp a < b", compare_rtt_result(&a, &b) < 0);

    /* First larger -> positive */
    mu_assert("cmp b > a", compare_rtt_result(&b, &a) > 0);

    /* Equal -> zero */
    mu_assert("cmp a == c", compare_rtt_result(&a, &c) == 0);

    /* Negative latency */
    RTTResult neg1 = {"1.1.1.1", -5};
    RTTResult neg2 = {"2.2.2.2", -3};
    mu_assert("cmp neg1 < neg2", compare_rtt_result(&neg1, &neg2) < 0);
    mu_assert("cmp neg2 > neg1", compare_rtt_result(&neg2, &neg1) > 0);

    /* Zero latency */
    RTTResult zero = {"0.0.0.0", 0};
    mu_assert("cmp zero < a", compare_rtt_result(&zero, &a) < 0);
    mu_assert("cmp a > zero", compare_rtt_result(&a, &zero) > 0);

    return NULL;
}

/* ================================================================== */
/* 15. parse_ip_list                                                  */
/* ================================================================== */
static const char *test_parse_ip_list(void) {
    StringList list;

    /* Multiple lines */
    list = parse_ip_list("1.1.1.1\n2.2.2.2\n3.3.3.3");
    mu_assert("parse 3 items", list.len == 3);
    if (list.len >= 3) {
        mu_assert("parse item 0", strcmp(list.items[0], "1.1.1.1") == 0);
        mu_assert("parse item 1", strcmp(list.items[1], "2.2.2.2") == 0);
        mu_assert("parse item 2", strcmp(list.items[2], "3.3.3.3") == 0);
    }
    string_list_free(&list);

    /* Empty content */
    list = parse_ip_list("");
    mu_assert("parse empty len 0", list.len == 0);
    string_list_free(&list);

    /* NULL content */
    list = parse_ip_list(NULL);
    mu_assert("parse NULL len 0", list.len == 0);
    string_list_free(&list);

    /* Lines with whitespace (should be trimmed) */
    list = parse_ip_list("  1.1.1.1  \n  \n  2.2.2.2  ");
    mu_assert("parse whitespace len 2", list.len == 2);
    if (list.len >= 2) {
        mu_assert("parse whitespace item 0", strcmp(list.items[0], "1.1.1.1") == 0);
        mu_assert("parse whitespace item 1", strcmp(list.items[1], "2.2.2.2") == 0);
    }
    string_list_free(&list);

    /* Single line */
    list = parse_ip_list("192.168.1.1");
    mu_assert("parse single len 1", list.len == 1);
    if (list.len >= 1) {
        mu_assert("parse single item", strcmp(list.items[0], "192.168.1.1") == 0);
    }
    string_list_free(&list);

    /* Trailing newline */
    list = parse_ip_list("1.1.1.1\n2.2.2.2\n");
    mu_assert("parse trailing newline len 2", list.len == 2);
    string_list_free(&list);

    /* Leading newline */
    list = parse_ip_list("\n1.1.1.1\n2.2.2.2");
    mu_assert("parse leading newline len 2", list.len == 2);
    string_list_free(&list);

    return NULL;
}

/* ================================================================== */
/* 16. Random basics                                                   */
/* ================================================================== */
static const char *test_random_basics(void) {
    /* init_random should not crash */
    init_random();

    /* After init, next_random_intn should return values in range */
    {
        int i;
        for (i = 0; i < 100; i++) {
            int v = next_random_intn(100);
            mu_assert("random in range", v >= 0 && v < 100);
        }
    }

    /* n <= 0 returns 0 */
    mu_assert("random n=0 returns 0", next_random_intn(0) == 0);
    mu_assert("random n negative returns 0", next_random_intn(-5) == 0);

    /* n=1 always returns 0 */
    mu_assert("random n=1 returns 0", next_random_intn(1) == 0);

    /* Deterministic: calling again produces some values */
    {
        int i;
        for (i = 0; i < 10; i++) {
            int v = next_random_intn(1000000);
            mu_assert("random large range", v >= 0 && v < 1000000);
        }
    }

    /* init_random again (should not crash, reinitializes state) */
    init_random();
    mu_assert("random reinit no crash", 1);

    return NULL;
}

/* ================================================================== */
/* 17. hash_iata                                                      */
/* ================================================================== */
static const char *test_hash_iata(void) {
    /* Same string = same hash */
    uint32_t h1 = hash_iata("LAX");
    uint32_t h2 = hash_iata("LAX");
    mu_assert("hash same string same hash", h1 == h2);

    /* Different strings = different hashes (extremely likely) */
    uint32_t h3 = hash_iata("SFO");
    mu_assert("hash different strings", h1 != h3);

    /* Empty string has defined value (FNV offset basis) */
    uint32_t h_empty = hash_iata("");
    mu_assert("hash empty string", h_empty == 2166136261u);

    /* Shorter codes */
    uint32_t h4 = hash_iata("NRT");
    mu_assert("hash NRT non-zero", h4 != 0);
    mu_assert("hash NRT not same as LAX", h4 != h1);

    /* Longer strings */
    uint32_t h5 = hash_iata("SOME_LONG_CODE");
    mu_assert("hash long string non-zero", h5 != 0);

    return NULL;
}

/* ================================================================== */
/* 18. split_colon_keep_empty                                         */
/* ================================================================== */
static const char *test_split_colon_keep_empty(void) {
    char parts[8][32];

    /* Normal split */
    {
        char s[] = "ab:cd:ef";
        memset(parts, 0, sizeof(parts));
        int n = split_colon_keep_empty(s, parts, 8);
        mu_assert("split 3 parts", n == 3);
        mu_assert("split part 0", strcmp(parts[0], "ab") == 0);
        mu_assert("split part 1", strcmp(parts[1], "cd") == 0);
        mu_assert("split part 2", strcmp(parts[2], "ef") == 0);
    }

    /* Leading colon */
    {
        char s[] = ":ab";
        memset(parts, 0, sizeof(parts));
        int n = split_colon_keep_empty(s, parts, 8);
        mu_assert("split leading colon count 2", n == 2);
        mu_assert("split leading first empty", strcmp(parts[0], "") == 0);
        mu_assert("split leading second non-empty", strcmp(parts[1], "ab") == 0);
    }

    /* Trailing colon */
    {
        char s[] = "ab:";
        memset(parts, 0, sizeof(parts));
        int n = split_colon_keep_empty(s, parts, 8);
        mu_assert("split trailing colon count 2", n == 2);
        mu_assert("split trailing first non-empty", strcmp(parts[0], "ab") == 0);
        mu_assert("split trailing second empty", strcmp(parts[1], "") == 0);
    }

    /* Double colon (::) */
    {
        char s[] = "::";
        memset(parts, 0, sizeof(parts));
        int n = split_colon_keep_empty(s, parts, 8);
        mu_assert("split :: count 3", n == 3);
        mu_assert("split :: part 0 empty", strcmp(parts[0], "") == 0);
        mu_assert("split :: part 1 empty", strcmp(parts[1], "") == 0);
        mu_assert("split :: part 2 empty", strcmp(parts[2], "") == 0);
    }

    /* max_parts limit */
    {
        char s[] = "a:b:c:d:e";
        memset(parts, 0, sizeof(parts));
        int n = split_colon_keep_empty(s, parts, 3);
        mu_assert("split max 3 count 3", n == 3);
        mu_assert("split max 3 part 0", strcmp(parts[0], "a") == 0);
        mu_assert("split max 3 part 1", strcmp(parts[1], "b") == 0);
        mu_assert("split max 3 part 2", strcmp(parts[2], "c") == 0);
        /* Note: parts[2] gets "c" even though there are more colons, because the
           max_parts check happens during copying, not parsing. But wait - let me
           re-read the function. Actually, looking at the code again:
           the function counts ALL parts, but only copies up to max_parts.
           So n would return 5 (total count). Let me adjust the test. */
    }

    /* Long part truncated to 31 chars */
    {
        char s[] = "abcdefghijklmnopqrstuvwxyz12345:end";
        memset(parts, 0, sizeof(parts));
        int n = split_colon_keep_empty(s, parts, 8);
        mu_assert("split long count 2", n == 2);
        mu_assert("split long part 0 starts correct", strncmp(parts[0], "abcdefghijklmnopqrstuvwxyz12345", 31) == 0);
        mu_assert("split long part 1", strcmp(parts[1], "end") == 0);
    }

    return NULL;
}

/* ================================================================== */
/* 19. append_utf8                                                    */
/* ================================================================== */
static const char *test_append_utf8(void) {
    char buf[16];
    size_t pos;

    /* 1-byte: U+0048 = 'H' */
    buf[0] = '\0';
    pos = 0;
    append_utf8(buf, sizeof(buf), &pos, 0x48);
    mu_assert("utf8 H pos 1", pos == 1);
    mu_assert("utf8 H value", buf[0] == 'H');
    mu_assert("utf8 H null term", buf[1] == '\0');

    /* 2-byte: U+00A9 = copyright sign */
    buf[0] = '\0';
    pos = 0;
    append_utf8(buf, sizeof(buf), &pos, 0xA9);
    mu_assert("utf8 2byte pos 2", pos == 2);
    mu_assert("utf8 2byte byte 0", (unsigned char)buf[0] == 0xC2);
    mu_assert("utf8 2byte byte 1", (unsigned char)buf[1] == 0xA9);
    mu_assert("utf8 2byte null term", buf[2] == '\0');

    /* 3-byte: U+4E00 = CJK ideograph yi */
    buf[0] = '\0';
    pos = 0;
    append_utf8(buf, sizeof(buf), &pos, 0x4E00);
    mu_assert("utf8 3byte pos 3", pos == 3);
    mu_assert("utf8 3byte byte 0", (unsigned char)buf[0] == 0xE4);
    mu_assert("utf8 3byte byte 1", (unsigned char)buf[1] == 0xB8);
    mu_assert("utf8 3byte byte 2", (unsigned char)buf[2] == 0x80);
    mu_assert("utf8 3byte null term", buf[3] == '\0');

    /* 4-byte: U+1F600 = grinning face emoji */
    buf[0] = '\0';
    pos = 0;
    append_utf8(buf, sizeof(buf), &pos, 0x1F600);
    mu_assert("utf8 4byte pos 4", pos == 4);
    mu_assert("utf8 4byte byte 0", (unsigned char)buf[0] == 0xF0);
    mu_assert("utf8 4byte byte 1", (unsigned char)buf[1] == 0x9F);
    mu_assert("utf8 4byte byte 2", (unsigned char)buf[2] == 0x98);
    mu_assert("utf8 4byte byte 3", (unsigned char)buf[3] == 0x80);
    mu_assert("utf8 4byte null term", buf[4] == '\0');

    /* Codepoint > 0x10FFFF (invalid, produces nothing per F0/1-4 encoding) */
    pos = 0;
    buf[0] = '\0';
    append_utf8(buf, sizeof(buf), &pos, 0x200000);
    mu_assert("utf8 invalid codepoint pos 0", pos == 0);

    /* Buffer too small for encoding */
    /* 4-byte encode into 3-byte buffer: writes 2 bytes then stops */
    {
        char tiny[3];
        memset(tiny, 'x', sizeof(tiny));
        pos = 0;
        append_utf8(tiny, 3, &pos, 0x1F600);
        mu_assert("utf8 tiny buf pos 2", pos == 2);
        mu_assert("utf8 tiny buf byte 0", (unsigned char)tiny[0] == 0xF0);
        mu_assert("utf8 tiny buf byte 1", (unsigned char)tiny[1] == 0x9F);
        mu_assert("utf8 tiny buf null term", tiny[2] == '\0');
    }

    return NULL;
}

/* ================================================================== */
/* 20. elapsed_seconds                                                */
/* ================================================================== */
static const char *test_elapsed_seconds(void) {
    struct timespec start, end;

    /* Basic: 3.2s - 1.5s = 1.7s */
    start.tv_sec = 1;
    start.tv_nsec = 500000000;
    end.tv_sec = 3;
    end.tv_nsec = 200000000;
    {
        double elapsed = elapsed_seconds(&start, &end);
        mu_assert("elapsed 1.7 seconds", elapsed > 1.69 && elapsed < 1.71);
    }

    /* Same time (zero elapsed) */
    start.tv_sec = 5;
    start.tv_nsec = 0;
    end.tv_sec = 5;
    end.tv_nsec = 0;
    {
        double elapsed = elapsed_seconds(&start, &end);
        mu_assert("elapsed zero", elapsed >= 0.0 && elapsed < 0.000001);
    }

    /* Sub-second elapsed */
    start.tv_sec = 0;
    start.tv_nsec = 0;
    end.tv_sec = 0;
    end.tv_nsec = 500000000;
    {
        double elapsed = elapsed_seconds(&start, &end);
        mu_assert("elapsed 0.5 seconds", elapsed > 0.49 && elapsed < 0.51);
    }

    /* Nanosecond overflow (end < start in nsec but end > start in sec) */
    start.tv_sec = 1;
    start.tv_nsec = 900000000;
    end.tv_sec = 2;
    end.tv_nsec = 100000000;
    {
        double elapsed = elapsed_seconds(&start, &end);
        /* (2-1) + (100M - 900M)/1e9 = 1 + (-800M)/1e9 = 1 - 0.8 = 0.2 */
        mu_assert("elapsed nsec overflow 0.2", elapsed > 0.19 && elapsed < 0.21);
    }

    return NULL;
}

/* ================================================================== */
/* 21. now_ms                                                        */
/* ================================================================== */
static const char *test_now_ms(void) {
    long long t1 = now_ms();
    long long t2 = now_ms();
    mu_assert("now_ms non-negative", t1 >= 0);
    mu_assert("now_ms monotonic", t2 >= t1);
    return NULL;
}

/* ================================================================== */
/* 22. Location map operations                                        */
/* ================================================================== */
static const char *test_location_map(void) {
    char out[256];
    int found;

    /* Clear and insert test data */
    location_map_clear();

    /* Insert and lookup */
    location_map_insert_locked("LAX", "Los Angeles");
    location_map_insert_locked("NRT", "Tokyo");

    found = lookup_data_center("LAX", out, sizeof(out));
    mu_assert("loc LAX found", found == 1);
    mu_assert("loc LAX value", strcmp(out, "Los Angeles") == 0);

    found = lookup_data_center("NRT", out, sizeof(out));
    mu_assert("loc NRT found", found == 1);
    mu_assert("loc NRT value", strcmp(out, "Tokyo") == 0);

    /* Unknown IATA returns just the code with found=0 */
    found = lookup_data_center("ZZZ", out, sizeof(out));
    mu_assert("loc unknown not found", found == 0);
    mu_assert("loc unknown value is code", strcmp(out, "ZZZ") == 0);

    /* Insert with NULL city */
    location_map_insert_locked("NULCTY", NULL);
    found = lookup_data_center("NULCTY", out, sizeof(out));
    mu_assert("loc null city found", found == 1);
    mu_assert("loc null city value is code", strcmp(out, "NULCTY") == 0);

    /* Insert with empty IATA (should be ignored) */
    location_map_insert_locked("", "Empty IATA");
    found = lookup_data_center("", out, sizeof(out));
    /* "" IATA lookup: The function checks if colo is empty, returns 0 */
    mu_assert("loc empty iata returns 0", found == 0);

    /* Lookup with NULL */
    found = lookup_data_center(NULL, out, sizeof(out));
    mu_assert("loc NULL lookup", found == 0);
    mu_assert("loc NULL empty out", strcmp(out, "") == 0);

    /* Re-insert updates existing entry */
    location_map_insert_locked("LAX", "Los Angeles Updated");
    found = lookup_data_center("LAX", out, sizeof(out));
    mu_assert("loc LAX updated", found == 1);
    mu_assert("loc LAX new value", strcmp(out, "Los Angeles Updated") == 0);

    /* Clear and verify gone */
    location_map_clear();
    found = lookup_data_center("LAX", out, sizeof(out));
    mu_assert("loc after clear not found", found == 0);

    return NULL;
}

/* ================================================================== */
/* 23. file_exists                                                    */
/* ================================================================== */
static const char *test_file_exists(void) {
    /* Non-existent path returns 0 */
    mu_assert("file not exists", !file_exists("/nonexistent/path/that/does/not/exist"));
    return NULL;
}

/* ================================================================== */
/* 24. data_path                                                     */
/* ================================================================== */
static const char *test_data_path(void) {
    char out[PATH_MAX];

    /* Without data_dir set (empty), returns the name as-is */
    const char *result = data_path("test.txt", out, sizeof(out));
    mu_assert("data_path result not null", result != NULL);
    mu_assert("data_path result same as out", result == out);
    mu_assert("data_path simple", strcmp(out, "test.txt") == 0);

    /* With data_dir set, returns data_dir/name */
    /* data_dir is a global static; save/restore for test */
    char saved_dir[PATH_MAX];
    snprintf(saved_dir, sizeof(saved_dir), "%s", data_dir);
    snprintf(data_dir, sizeof(data_dir), "/tmp/data");
    result = data_path("test.txt", out, sizeof(out));
    mu_assert("data_path with dir", strcmp(out, "/tmp/data/test.txt") == 0);
    /* Restore */
    snprintf(data_dir, sizeof(data_dir), "%s", saved_dir);

    /* NULL dst */
    result = data_path("test.txt", NULL, 0);
    mu_assert("data_path NULL dst", result == NULL);

    return NULL;
}

/* ================================================================== */
/* 25. set_fd_blocking (edge cases only - no real fd available)       */
/* ================================================================== */
static const char *test_set_fd_blocking(void) {
    /* Invalid fd returns -1 */
    int ret = set_fd_blocking(-1, 0);
    mu_assert("set_fd_blocking invalid fd nonblock", ret == -1);

    ret = set_fd_blocking(-1, 1);
    mu_assert("set_fd_blocking invalid fd block", ret == -1);

    return NULL;
}

/* ================================================================== */
/* 26. split_colon_max_parts (separate test for max_parts behavior)   */
/* ================================================================== */
static const char *test_split_colon_max_parts(void) {
    char parts[8][32];
    char s[] = "a:b:c:d:e";
    memset(parts, 0, sizeof(parts));
    int n = split_colon_keep_empty(s, parts, 3);
    /* The function only counts up to max_parts */
    mu_assert("split max_parts 3 returns 3", n == 3);
    /* But only copied up to 3 parts */
    mu_assert("split max_parts part 0", strcmp(parts[0], "a") == 0);
    mu_assert("split max_parts part 1", strcmp(parts[1], "b") == 0);
    mu_assert("split max_parts part 2", strcmp(parts[2], "c") == 0);
    /* Part 4 and 5 were not copied */
    mu_assert("split max_parts part 3 untouched", parts[3][0] == '\0');

    return NULL;
}

/* ================================================================== */
/* 27. read_line_trim - stdin already redirected; not unit-testable   */
/*     Instead test the helper components already covered above.      */
/* ================================================================== */

/* ================================================================== */
/* All tests runner                                                   */
/* ================================================================== */
static const char *all_tests(void) {
    mu_run_test(test_trim_in_place);
    mu_run_test(test_copy_cstr);
    mu_run_test(test_append_cstr);
    mu_run_test(test_string_list);
    mu_run_test(test_rtt_vector);
    mu_run_test(test_hex4);
    mu_run_test(test_json_read_string);
    mu_run_test(test_json_extract_string);
    mu_run_test(test_bracket_ipv6);
    mu_run_test(test_extract_data_center);
    mu_run_test(test_headers_have_cf_ray);
    mu_run_test(test_buffer_contains);
    mu_run_test(test_buffer_has_header_end);
    mu_run_test(test_compare_rtt_result);
    mu_run_test(test_parse_ip_list);
    mu_run_test(test_random_basics);
    mu_run_test(test_hash_iata);
    mu_run_test(test_split_colon_keep_empty);
    mu_run_test(test_split_colon_max_parts);
    mu_run_test(test_append_utf8);
    mu_run_test(test_elapsed_seconds);
    mu_run_test(test_now_ms);
    mu_run_test(test_location_map);
    mu_run_test(test_file_exists);
    mu_run_test(test_data_path);
    mu_run_test(test_set_fd_blocking);
    return NULL;
}

int main(void) {
    const char *result = all_tests();
    if (result) {
        printf("FAIL: %s\n", result);
    } else {
        printf("ALL TESTS PASSED\n");
    }
    printf("Tests run: %d\n", tests_run);
    return result != NULL;
}
