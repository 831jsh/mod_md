/* Copyright 2017 greenbytes GmbH (https://www.greenbytes.de)
 *
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

#ifndef mod_md_md_json_h
#define mod_md_md_json_h

struct md_http;


typedef struct md_json md_json;
typedef struct md_jsel md_jsel;

typedef enum {
    MD_JSON_FMT_COMPACT,
    MD_JSON_FMT_INDENT,
} md_json_fmt_t;

apr_status_t md_json_create(md_json **pjson, apr_pool_t *pool);
void md_json_destroy(md_json *json);


apr_status_t md_jsel_create(md_jsel **pjsel, apr_pool_t *pool, const char *key);
apr_status_t md_jsel_add(md_jsel *jsel, const char *key);

/* boolean manipulation */
int md_json_getb(md_json *json, md_jsel *sel);
apr_status_t md_json_setb(md_json *json, md_jsel *sel, int value);

/* number manipulation */
double md_json_getn(md_json *json, md_jsel *sel);
apr_status_t md_json_setn(md_json *json, md_jsel *sel, double value);

/* string manipulation */
const char *md_json_gets(md_json *json, md_jsel *sel);
apr_status_t md_json_sets(md_json *json, md_jsel *sel, const char *value);

/* Array/Object manipulation */
apr_status_t md_json_clr(md_json *json, md_jsel *sel);
apr_status_t md_json_del(md_json *json, md_jsel *sel);

/* Manipulating Object String values */
apr_status_t md_json_gets_dict(md_json *json, md_jsel *sel, apr_table_t *dict);
apr_status_t md_json_sets_dict(md_json *json, md_jsel *sel, apr_table_t *dict);

/* Manipulating String Arrays */
apr_status_t md_json_getsa(md_json *json, md_jsel *sel, apr_array_header_t *a);
apr_status_t md_json_setsa(md_json *json, md_jsel *sel, apr_array_header_t *a);

/* serialization & parsing */
apr_status_t md_json_writeb(md_json *json, md_json_fmt_t fmt, apr_bucket_brigade *bb);
const char *md_json_writep(md_json *json, md_json_fmt_t fmt, apr_pool_t *pool);

apr_status_t md_json_readb(md_json **pjson, apr_pool_t *pool, apr_bucket_brigade *bb);
apr_status_t md_json_readd(md_json **pjson, apr_pool_t *pool, const char *data, size_t data_len);

/* http retrieval */
apr_status_t md_json_http_get(md_json **pjson, apr_pool_t *pool,
                              struct md_http *http, const char *url);

#endif /* md_json_h */
