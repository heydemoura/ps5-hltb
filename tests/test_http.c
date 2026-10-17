/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Run the real HTTP backend against deterministic SDK resource accounting. */
#include "../src/http_ps5.c"
#include <assert.h>
static int fail, pools, ssls, https, templates, requests, phase, read_count;
int sceNetInit(void) { return 0; }
int sceNetPoolCreate(const char *n,int size,int flags) {
    (void)n;(void)size;(void)flags; if(fail==1)return -1; pools++; return 1;
}
int sceNetPoolDestroy(int id) { assert(id==1 && pools==1 && !ssls); pools--;return 0; }
int sceSslInit(size_t size) { (void)size; if(fail==2)return -1; ssls++; return 2; }
int sceSslTerm(int id) { assert(id==2 && ssls==1 && !https); ssls--;return 0; }
int sceSslLoadCert(int a,int b,const struct ssl_data *const *c,const void *d,const void *e) {
    (void)a;(void)b;(void)c;(void)d;(void)e;return 0;
}
int sceHttp2Init(int a,int b,size_t c,int d) {
    (void)a;(void)b;(void)c;(void)d; if(fail==3)return -1; https++;return 3;
}
int sceHttp2Term(int id) { assert(id==3 && https==1 && !templates);https--;return 0; }
int sceHttp2CreateTemplate(int a,const char *b,int c,int d) {
    (void)a;(void)b;(void)c;(void)d;if(fail==4)return -1;templates++;return 4;
}
int sceHttp2DeleteTemplate(int id) { assert(id==4 && templates==1 && !requests);templates--;return 0; }
int sceHttp2CreateRequestWithURL(int a,const char *method,const char *url,uint64_t size) {
    (void)a;(void)url;(void)size; phase=!strcmp(method,"GET")?0:1;read_count=0;requests++;return 5;
}
int sceHttp2DeleteRequest(int id) { assert(id==5 && requests==1);requests--;return 0; }
int sceHttp2AddRequestHeader(int a,const char *b,const char *c,int d) {
    (void)a;(void)b;(void)c;(void)d;return fail==5?-1:0;
}
int sceHttp2SendRequest(int a,const void *b,size_t c) {(void)a;(void)b;(void)c;return 0;}
int sceHttp2GetStatusCode(int id,int *status) { assert(id==5);*status=200;return 0; }
int sceHttp2ReadData(int id,void *buf,size_t size) {
    assert(id==5);if(read_count++)return 0;
    const char *reply=phase ? "{\"data\":[{\"game_id\":1,\"game_name\":\"Game A\",\"comp_main\":3600,\"comp_plus\":7200,\"comp_100\":10800}]}" : "{\"token\":\"test\"}";
    size_t n=strlen(reply);assert(size>n);memcpy(buf,reply,n);return (int)n;
}
int main(void) {
    for(int i=0;i<1000;i++) {
        fail=i%6;hltb_result result={0};
        int rc=hltb_lookup("Game A",&result);
        assert(fail?rc!=0:rc==0);
        if(!fail)assert(result.main_seconds==3600 && result.completionist_seconds==10800);
        assert(!pools && !ssls && !https && !templates && !requests);
    }
    puts("1000 lookups including partial-init/header failures released all network resources");
}
