/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Native networking follows the SDK http2_get sample (John Tornblom, GPL-3+). */
#include "cJSON.h"
#include "hltb.h"
#include "root_ca.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
int sceNetInit(void);
int sceNetPoolCreate(const char *, int, int);
int sceNetPoolDestroy(int);
int sceSslInit(size_t);
int sceSslTerm(int);
struct ssl_data {
    const char *ptr;
    size_t size;
};
int sceSslLoadCert(int, int, const struct ssl_data *const *, const void *, const void *);
int sceHttp2Init(int, int, size_t, int);
int sceHttp2Term(int);
int sceHttp2CreateTemplate(int, const char *, int, int);
int sceHttp2DeleteTemplate(int);
int sceHttp2CreateRequestWithURL(int, const char *, const char *, uint64_t);
int sceHttp2DeleteRequest(int);
int sceHttp2SendRequest(int, const void *, size_t);
int sceHttp2GetStatusCode(int, int *);
int sceHttp2ReadData(int, void *, size_t);
int sceHttp2AddRequestHeader(int, const char *, const char *, int);
#define RESPONSE_MAX (512 * 1024)
static char *request(int tmpl, const char *method, const char *url, const char *token,
                     const char *body, int *status) {
    int req = sceHttp2CreateRequestWithURL(tmpl, method, url, body ? strlen(body) : 0);
    if (req < 0) {
        printf("create request: %x\n", req);
        return NULL;
    }
    char *buf = NULL;
    if (sceHttp2AddRequestHeader(req, "Referer", "https://howlongtobeat.com/", 0) < 0 ||
        sceHttp2AddRequestHeader(req, "Origin", "https://howlongtobeat.com", 0) < 0 ||
        (body && sceHttp2AddRequestHeader(req, "Content-Type", "application/json", 0) < 0) ||
        (token && sceHttp2AddRequestHeader(req, "x-auth-token", token, 0) < 0))
        goto done;
    int r = sceHttp2SendRequest(req, body, body ? strlen(body) : 0);
    if (r < 0) {
        printf("send request: %x\n", r);
        goto done;
    }
    if (sceHttp2GetStatusCode(req, status) < 0 || *status != 200) {
        printf("HLTB HTTP %d\n", *status);
        goto done;
    }
    buf = malloc(RESPONSE_MAX + 1);
    if (!buf)
        goto done;
    size_t used = 0;
    int n;
    while ((n = sceHttp2ReadData(req, buf + used, RESPONSE_MAX - used)) > 0) {
        used += n;
        if (used == RESPONSE_MAX) {
            free(buf);
            buf = NULL;
            goto done;
        }
    }
    if (n < 0) {
        printf("read response: %x\n", n);
        free(buf);
        buf = NULL;
    } else
        buf[used] = 0;
done:
    sceHttp2DeleteRequest(req);
    return buf;
}
int hltb_lookup(const char *title, hltb_result *out) {
    int pool = -1, ssl = -1, http = -1, tmpl = -1, result = -1;
    char *body = hltb_search_body(title);
    if (!body)
        return -1;
    sceNetInit();
    if ((pool = sceNetPoolCreate("hltb", 128 * 1024, 0)) < 0)
        goto done;
    if ((ssl = sceSslInit(512 * 1024)) < 0)
        goto done;
    struct ssl_data ca = {hltb_root_ca, sizeof(hltb_root_ca) - 1};
    const struct ssl_data *cas[] = {&ca};
    int loaded = sceSslLoadCert(ssl, 1, cas, NULL, NULL);
    if (loaded < 0) {
        printf("load root CA: %x\n", loaded);
        goto done;
    }
    if ((http = sceHttp2Init(pool, ssl, 1024 * 1024, 1)) < 0)
        goto done;
    if ((tmpl = sceHttp2CreateTemplate(http, "Mozilla/5.0", 3, 1)) < 0)
        goto done;
    for (int attempt = 0; attempt < 2; attempt++) {
        char url[160];
        snprintf(url, sizeof url, "https://howlongtobeat.com/api/search/site/init?t=%llu",
                 (unsigned long long)time(NULL) * 1000);
        int status = 0;
        char *auth = request(tmpl, "GET", url, NULL, NULL, &status);
        if (!auth)
            break;
        cJSON *json = cJSON_Parse(auth);
        free(auth);
        cJSON *token = cJSON_GetObjectItemCaseSensitive(json, "token");
        if (!cJSON_IsString(token)) {
            cJSON_Delete(json);
            break;
        }
        char *response = request(tmpl, "POST", "https://howlongtobeat.com/api/search/site",
                                 token->valuestring, body, &status);
        cJSON_Delete(json);
        if (response) {
            result = hltb_parse_result(response, title, out);
            free(response);
            break;
        }
        if (status != 403)
            break;
    }
done:
    if (tmpl >= 0)
        sceHttp2DeleteTemplate(tmpl);
    if (http >= 0)
        sceHttp2Term(http);
    if (ssl >= 0)
        sceSslTerm(ssl);
    if (pool >= 0)
        sceNetPoolDestroy(pool);
    free(body);
    return result;
}
