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

#include <assert.h>

#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_time.h>

#include "md.h"
#include "md_json.h"
#include "md_log.h"
#include "md_util.h"


int md_contains(const md_t *md, const char *domain)
{
   return md_array_str_case_index(md->domains, domain, 0) >= 0;
}

const char *md_common_name(const md_t *md1, const md_t *md2)
{
    int i;
    
    if (md1 == NULL || md1->domains == NULL
        || md2 == NULL || md2->domains == NULL) {
        return NULL;
    }
    
    for (i = 0; i < md1->domains->nelts; ++i) {
        const char *name1 = APR_ARRAY_IDX(md1->domains, i, const char*);
        if (md_contains(md2, name1)) {
            return name1;
        }
    }
    return NULL;
}

int md_domains_overlap(const md_t *md1, const md_t *md2)
{
    return md_common_name(md1, md2) != NULL;
}

md_t *md_create_empty(apr_pool_t *p)
{
    md_t *md = apr_pcalloc(p, sizeof(*md));
    if (md) {
        md->domains = apr_array_make(p, 5, sizeof(const char *));
        md->chain = apr_array_make(p, 5, sizeof(void *));
        if (md->domains && md->chain) {
            md->state = MD_S_INCOMPLETE;
            md->defn_name = "unknown";
            md->defn_line_number = 0;
        }
    }
    return md;
}

const char *md_create(md_t **pmd, apr_pool_t *p, apr_array_header_t *domains)
{
    md_t *md;
    const char *domain, **np;
    int i;
    
    if (domains->nelts <= 0) {
        return "needs at least one domain name";
    }
    
    md = md_create_empty(p);
    if (!md) {
        return "not enough memory";
    }

    for (i = 0; i < domains->nelts; ++i) {
        domain = APR_ARRAY_IDX(domains, i, const char *);
        if (md_array_str_case_index(md->domains, domain, 0) < 0) {
            np = (const char **)apr_array_push(md->domains);
            md_util_str_tolower(apr_pstrdup(p, domain));
            *np = domain;
        }
    }
    md->name = APR_ARRAY_IDX(md->domains, 0, const char *);
 
    *pmd = md;
    return NULL;   
}

md_t *md_copy(apr_pool_t *p, md_t *src)
{
    md_t *md;
    
    md = apr_pcalloc(p, sizeof(*md));
    if (md) {
        memcpy(md, src, sizeof(*md));
        md->domains = apr_array_copy(p, src->domains);
    }    
    return md;   
}

md_t *md_clone(apr_pool_t *p, md_t *src)
{
    md_t *md;
    
    md = apr_pcalloc(p, sizeof(*md));
    if (md) {
        md->state = src->state;
        md->name = apr_pstrdup(p, src->name);
        md->domains = md_array_str_clone(p, src->domains);
        if (src->ca_url) md->ca_url = apr_pstrdup(p, src->ca_url);
        if (src->ca_proto) md->ca_proto = apr_pstrdup(p, src->ca_proto);
        if (src->defn_name) md->defn_name = apr_pstrdup(p, src->defn_name);
        md->defn_line_number = src->defn_line_number;
    }    
    return md;   
}

md_json *md_to_json(const md_t *md, apr_pool_t *p)
{
    md_json *json = md_json_create(p);
    if (json) {
        md_json_sets(md->name, json, MD_KEY_NAME, NULL);
        md_json_setsa(md->domains, json, MD_KEY_DOMAINS, NULL);
        md_json_sets(md->ca_proto, json, MD_KEY_CA, MD_KEY_PROTO, NULL);
        md_json_sets(md->ca_url, json, MD_KEY_CA, MD_KEY_URL, NULL);
        md_json_setl(md->state, json, MD_KEY_STATE, NULL);
        return json;
    }
    return NULL;
}

md_t *md_from_json(md_json *json, apr_pool_t *p)
{
    md_t *md = md_create_empty(p);
    if (md) {
        md->name = md_json_dups(p, json, MD_KEY_NAME, NULL);            
        md_json_dupsa(md->domains, p, json, MD_KEY_DOMAINS, NULL);
        md->ca_proto = md_json_dups(p, json, MD_KEY_CA, MD_KEY_PROTO, NULL);
        md->ca_url = md_json_dups(p, json, MD_KEY_CA, MD_KEY_URL, NULL);
        md->state = (int)md_json_getl(json, MD_KEY_STATE, NULL);
        return md;
    }
    return NULL;
}
