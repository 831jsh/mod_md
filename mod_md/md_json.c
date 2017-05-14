/* Copyright 2017 greenbytes GmbH (https://www.greenbytes.de)
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_buckets.h>

#include <jansson_config.h>
/* jansson thinks everyone compiles with the platform's cc in its fullest capabilities */
#undef   JSON_INLINE
#define JSON_INLINE 
#include <jansson.h>

#include "md_json.h"
#include "md_http.h"

struct md_json {
    apr_pool_t *p;
    json_t *j;
};

struct md_jsel {
    const char **keys;
    size_t nelts;
};

static void init_dummy()
{
    /* jansson wants to inline static function that we never call and this,
     * -Wunused-function triggers and generated unnecessary warnings. */
    (void)json_decrefp;
    (void)json_object_set_nocheck;
    (void)json_object_iter_set;
    (void)json_array_set;
    (void)json_array_append;
    (void)json_array_insert;
}

/**************************************************************************************************/
/* lifecylce */

static apr_status_t json_pool_cleanup(void *data)
{
    md_json *json = data;
    if (json) {
        md_json_destroy(json);
    }
    return APR_SUCCESS;
}

static apr_status_t json_create(md_json **pjson, apr_pool_t *pool, json_t *j)
{
    md_json *json;
    
    (void)init_dummy;
    json = apr_pcalloc(pool, sizeof(*json));
    if (json == NULL) {
        json_decref(j);
        return APR_ENOMEM;
    }
    
    json->p = pool;
    json->j = j;
    apr_pool_cleanup_register(pool, json, json_pool_cleanup, apr_pool_cleanup_null);    

    *pjson = json;
    
    return APR_SUCCESS;
}

apr_status_t md_json_create(md_json **pjson, apr_pool_t *pool)
{
    return json_create(pjson, pool, json_object());
}

void md_json_destroy(md_json *json)
{
    if (json->j) {
        json_decref(json->j);
        json->j = NULL;
    }
}

/**************************************************************************************************/
/* selectors */

md_jsel *md_jsel_create(apr_pool_t *pool, const char *key)
{
    md_jsel *jsel;
    
    jsel = apr_pcalloc(pool, sizeof(*jsel));
    if (jsel) {
        jsel->keys = apr_pcalloc(pool, sizeof(const char *));
        jsel->keys[0] = key; 
        jsel->nelts = 1;
    }

    return jsel;
}

md_jsel *md_jsel_createvn(apr_pool_t *pool, ...)
{
    md_jsel *jsel;
    const char *key;
    int i;
    va_list ap;
    
    jsel = apr_pcalloc(pool, sizeof(*jsel));
    if (!jsel) {
        return NULL;
    }
    
    va_start(ap, pool);
    key = va_arg(ap, char *);
    while (key) {
        ++jsel->nelts;
        key = va_arg(ap, char *);
    }
    va_end(ap);
        
    jsel->keys = apr_pcalloc(pool, sizeof(const char *)*jsel->nelts);
    va_start(ap, pool);
    for (i = 0; i < jsel->nelts; ++i) {
        key = va_arg(ap, char *);
        jsel->keys[i] = key; 
    }
    va_end(ap);
    
    return jsel;
}

static const char *last_key(md_jsel *sel)
{
    return (sel->nelts  > 0)? sel->keys[sel->nelts-1] : NULL; 
}

static json_t *select(md_json *json, md_jsel *sel)
{
    json_t *j;
    int i;
    
    j = json->j;
    for (i = 0; j && i < sel->nelts; ++i) {
        j = json_object_get(j, sel->keys[i]);
    }
    
    return j;
}

static json_t *select_parent(md_json *json, md_jsel *sel, int create)
{
    json_t *j, *jn;
    const char *key;
    int i;
    
    j = json->j;
    for (i = 0; j && i < sel->nelts-1; ++i) {
        key = sel->keys[i];
        jn = json_object_get(j, key);
        if (!jn && create) {
            jn = json_object();
            json_object_set(j, key, jn);
            json_decref(jn);
        }
        j = jn;
    }
    
    return j;
}

static apr_status_t select_set_new(md_json *json, md_jsel *sel, json_t *val)
{
    json_t *j = select_parent(json, sel, 1);
    
    if (!j || !json_is_object(j)) {
        json_decref(val);
        return APR_EINVAL;
    }
    
    json_object_set_new(j, last_key(sel), val);

    return APR_SUCCESS;
}

/**************************************************************************************************/
/* booleans */

int md_json_getb(md_json *json, md_jsel *sel)
{
    json_t *j = select(json, sel);
    if (j) {
        return json_is_true(j);
    }
    return 0;
}

apr_status_t md_json_setb(md_json *json, md_jsel *sel, int value)
{
    return select_set_new(json, sel, json_boolean(value));
}

/**************************************************************************************************/
/* numbers */

double md_json_getn(md_json *json, md_jsel *sel)
{
    json_t *j = select(json, sel);
    if (j && json_is_number(j)) {
        return json_number_value(j);
    }
    return 0.0;
}

apr_status_t md_json_setn(md_json *json, md_jsel *sel, double value)
{
    return select_set_new(json, sel, json_real(value));
}

/**************************************************************************************************/
/* strings */

const char *md_json_getsv(md_json *json, ...)
{
    json_t *j;
    const char *key;
    va_list ap;

    j = json->j;
    va_start(ap, json);
    key = va_arg(ap, char *);
    while (key && j) {
        j = json_object_get(j, key);
        key = va_arg(ap, char *);
    }
    va_end(ap);

    if (j && json_is_string(j)) {
        return json_string_value(j);
    }
    return NULL;
}

apr_status_t md_json_setsv(md_json *json, ...)
{
    const char *key, *val;
    json_t *j, *next, *parent;
    int argc, i;
    va_list ap;
    
    j = json->j;
    parent = NULL;
    
    va_start(ap, json);
    key = va_arg(ap, char *);
    argc = 0;
    while (key) {
        ++argc;
        key = va_arg(ap, char *);
    }
    va_end(ap);

    if (argc < 2) {
        return APR_NOTFOUND;
    }
    
    va_start(ap, json);
    for (i = 0; i < argc-2; ++i) {
        key = va_arg(ap, char *);
        next = json_object_get(j, key);
        if (!next) {
            next = json_object();
            json_object_set_new(j, key, next);
        }
        j = next;
    }
    
    key = va_arg(ap, char *);
    val = va_arg(ap, char *);
    json_object_set_new(j, key, json_string(val));
    va_end(ap);
    
    return APR_SUCCESS;
}

/**************************************************************************************************/
/* arrays / objects */

apr_status_t md_json_clr(md_json *json, md_jsel *sel)
{
    json_t *j = select(json, sel);
    if (j && json_is_object(j)) {
        json_object_clear(j);
    }
    else if (j && json_is_array(j)) {
        json_array_clear(j);
    }
    return APR_SUCCESS;
}

apr_status_t md_json_del(md_json *json, md_jsel *sel)
{
    json_t *j = select_parent(json, sel, 0);
    if (j && json_is_object(j)) {
        json_object_del(j, last_key(sel));
    }
    return APR_SUCCESS;
}

/**************************************************************************************************/
/* object strings */

apr_status_t md_json_gets_dict(md_json *json, md_jsel *sel, apr_table_t *dict)
{
    json_t *j = select(json, sel);
    if (j && json_is_object(j)) {
        const char *key;
        json_t *val;
        
        json_object_foreach(j, key, val) {
            if (json_is_string(val)) {
                apr_table_set(dict, key, json_string_value(val));
            }
        }
        return APR_SUCCESS;
    }
    return APR_NOTFOUND;
}

static int object_set(void *data, const char *key, const char *val)
{
    json_t *j = data, *nj = json_string(val);
    json_object_set(j, key, nj);
    json_decref(nj);
    return 1;
}
 
apr_status_t md_json_sets_dict(md_json *json, md_jsel *sel, apr_table_t *dict)
{
    json_t *nj, *j = select(json, sel);
    
    if (!j || !json_is_object(j)) {
        j = select_parent(json, sel, 1);
        if (!j || !json_is_object(j)) {
            return APR_EINVAL;
        }
        nj = json_object();
        json_object_set(j, last_key(sel), nj);
        json_decref(nj);
        j = nj; 
    }
    
    apr_table_do(object_set, j, dict, NULL);
    return APR_SUCCESS;
}

/**************************************************************************************************/
/* array strings */

apr_status_t md_json_getsa(md_json *json, md_jsel *sel, apr_array_header_t *a)
{
    json_t *j = select(json, sel);
    if (j && json_is_array(j)) {
        const char **np;
        size_t index;
        json_t *val;
        
        json_array_foreach(j, index, val) {
            if (json_is_string(val)) {
                np =(const char **)apr_array_push(a);
                *np = json_string_value(val);
            }
        }
        return APR_SUCCESS;
    }
    return APR_NOTFOUND;
}

apr_status_t md_json_setsa(md_json *json, md_jsel *sel, apr_array_header_t *a)
{
    json_t *nj, *j = select(json, sel);
    int i;
    
    if (!j || !json_is_array(j)) {
        j = select_parent(json, sel, 1);
        if (!j || !json_is_object(j)) {
            return APR_EINVAL;
        }
        nj = json_array();
        json_object_set(j, last_key(sel), nj);
        json_decref(nj);
        j = nj; 
    }

    json_array_clear(j);
    for (i = 0; i < a->nelts; ++i) {
        json_array_append_new(j, json_string(APR_ARRAY_IDX(a, i, const char*)));
    }
    return APR_SUCCESS;
}

/**************************************************************************************************/
/* formatting, parsing */

static int dump_cb(const char *buffer, size_t len, void *baton)
{
    apr_bucket_brigade *bb = baton;
    apr_status_t status;
    
    status = apr_brigade_write(bb, NULL, NULL, buffer, len);
    return (status == APR_SUCCESS)? 0 : -1;
}

apr_status_t md_json_writeb(md_json *json, md_json_fmt_t fmt, apr_bucket_brigade *bb)
{
    size_t flags = (fmt == MD_JSON_FMT_COMPACT)? JSON_COMPACT : JSON_INDENT(2); 
    int rv = json_dump_callback(json->j, dump_cb, bb, flags);
    return rv? APR_EGENERAL : APR_SUCCESS;
}

const char *md_json_writep(md_json *json, md_json_fmt_t fmt, apr_pool_t *pool)
{
    size_t flags = (fmt == MD_JSON_FMT_COMPACT)? JSON_COMPACT : JSON_INDENT(2); 
    size_t jlen = json_dumpb(json->j, NULL, 0, flags);
    char *s;
    
    if (jlen == 0) {
        return NULL;
    }
    s = apr_palloc(pool, jlen+1);
    jlen = json_dumpb(json->j, s, jlen, flags);
    s[jlen] = '\0';
    return s;
}

apr_status_t md_json_readd(md_json **pjson, apr_pool_t *pool, const char *data, size_t data_len)
{
    json_error_t error;
    json_t *j;
    
    j = json_loadb(data, data_len, 0, &error);
    if (!j) {
        return APR_EINVAL;
    }
    return json_create(pjson, pool, j);
}

static size_t load_cb(void *data, size_t max_len, void *baton)
{
    apr_bucket_brigade *body = baton;
    size_t blen, read_len = 0;
    const char *bdata;
    apr_bucket *b;
    apr_status_t status;
    
    while (body && !APR_BRIGADE_EMPTY(body) && max_len > 0) {
        b = APR_BRIGADE_FIRST(body);
        if (APR_BUCKET_IS_METADATA(b)) {
            if (APR_BUCKET_IS_EOS(b)) {
                body = NULL;
            }
        }
        else {
            status = apr_bucket_read(b, &bdata, &blen, APR_BLOCK_READ);
            if (status == APR_SUCCESS) {
                if (blen > max_len) {
                    apr_bucket_split(b, max_len);
                    blen = max_len;
                }
                memcpy(data, bdata, blen);
                read_len += blen;
                max_len -= blen;
            }
            else {
                body = NULL;
                if (!APR_STATUS_IS_EOF(status)) {
                    /* everything beside EOF is an error */
                    read_len = (size_t)-1;
                }
            }
        }
        APR_BUCKET_REMOVE(b);
        apr_bucket_delete(b);
    }
    
    return read_len;
}

apr_status_t md_json_readb(md_json **pjson, apr_pool_t *pool, apr_bucket_brigade *bb)
{
    json_error_t error;
    json_t *j;
    
    j = json_load_callback(load_cb, bb, 0, &error);
    if (!j) {
        return APR_EINVAL;
    }
    return json_create(pjson, pool, j);
}

/**************************************************************************************************/
/* http get */

apr_status_t md_json_read_http(md_json **pjson, apr_pool_t *pool, const md_http_response *res)
{
    apr_status_t status = APR_EINVAL;
    if (res->rv == APR_SUCCESS) {
        if (res->status >= 200 && res->status < 300) {
            const char *ctype = apr_table_get(res->headers, "content-type");
            if (ctype && !strcmp("application/json", ctype) && res->body) {
                status = md_json_readb(pjson, pool, res->body);
            }
        }
    }
    return status;
}

typedef struct {
    apr_status_t status;
    apr_pool_t *pool;
    md_json *json;
} resp_data;

static apr_status_t json_resp_cb(const md_http_response *res)
{
    resp_data *resp = res->req->baton;
    return md_json_read_http(&resp->json, resp->pool, res);
}

apr_status_t md_json_http_get(md_json **pjson, apr_pool_t *pool,
                              struct md_http *http, const char *url)
{
    long req_id;
    apr_status_t status;
    resp_data resp;
    
    memset(&resp, 0, sizeof(resp));
    resp.pool = pool;
    
    status = md_http_GET(http, url, NULL, json_resp_cb, &resp, &req_id);
    
    if (status == APR_SUCCESS) {
        md_http_await(http, req_id);
        *pjson = resp.json;
        return resp.status;
    }
    *pjson = NULL;
    return status;
}

