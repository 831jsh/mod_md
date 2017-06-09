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

#ifndef mod_md_md_store_h
#define mod_md_md_store_h

struct apr_array_header_t;
struct md_cert_t;
struct md_pkey_t;

typedef struct md_store_t md_store_t;

typedef void md_store_destroy_cb(md_store_t *store);

typedef enum {
    MD_SV_TEXT,
    MD_SV_JSON,
    MD_SV_CERT,
    MD_SV_PKEY,
    MD_SV_CHAIN,
} md_store_vtype_t;

typedef enum {
    MD_SG_ACCOUNTS,
    MD_SG_CHALLENGES,
    MD_SG_DOMAINS,
} md_store_group_t;

typedef apr_status_t md_store_load_cb(md_store_t *store, md_store_group_t group, 
                                      const char *name, const char *aspect, 
                                      md_store_vtype_t vtype, void **pvalue, 
                                      apr_pool_t *p);
typedef apr_status_t md_store_save_cb(md_store_t *store, md_store_group_t group, 
                                      const char *name, const char *aspect, 
                                      md_store_vtype_t vtype, void *value, 
                                      int create);
typedef apr_status_t md_store_remove_cb(md_store_t *store, md_store_group_t group, 
                                        const char *name, const char *aspect,  
                                        apr_pool_t *p, int force);

typedef apr_status_t md_store_load_all_cb(struct apr_array_header_t *values, apr_pool_t *p,
                                          md_store_t *store, md_store_group_t group, 
                                          const char *pattern, const char *aspect, 
                                          md_store_vtype_t vtype);

typedef int md_store_inspect(void *baton, const char *name, const char *aspect, 
                             md_store_vtype_t vtype, void *value);

typedef apr_status_t md_store_iter_cb(md_store_inspect *inspect, void *baton, md_store_t *store, 
                                      md_store_group_t group, const char *pattern,
                                      const char *aspect, md_store_vtype_t vtype);

struct md_store_t {
    apr_pool_t *p;
    md_store_destroy_cb *destroy;

    md_store_save_cb *save;
    md_store_load_cb *load;
    md_store_remove_cb *remove;
    md_store_iter_cb *iterate;
    
};

void md_store_destroy(md_store_t *store);

apr_status_t md_store_load_json(md_store_t *store, md_store_group_t group, 
                                const char *name, const char *aspect, 
                                struct md_json_t **pdata, apr_pool_t *p);
apr_status_t md_store_save_json(md_store_t *store, md_store_group_t group, 
                                const char *name, const char *aspect, 
                                struct md_json_t *data, int create);


apr_status_t md_store_load(md_store_t *store, md_store_group_t group, 
                           const char *name, const char *aspect, 
                           md_store_vtype_t vtype, void **pdata, 
                           apr_pool_t *p);
apr_status_t md_store_save(md_store_t *store, md_store_group_t group, 
                           const char *name, const char *aspect, 
                           md_store_vtype_t vtype, void *data, 
                           int create);
apr_status_t md_store_remove(md_store_t *store, md_store_group_t group, 
                             const char *name, const char *aspect, 
                             apr_pool_t *p, int force);


apr_status_t md_store_iter(md_store_inspect *inspect, void *baton, md_store_t *store, 
                           md_store_group_t group, const char *pattern, const char *aspect,
                           md_store_vtype_t vtype);

/**************************************************************************************************/
/* file system based store */

apr_status_t md_store_fs_init(md_store_t **pstore, apr_pool_t *p, const char *path);

#endif /* mod_md_md_store_h */
