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

#ifndef mod_md_md_util_h
#define mod_md_md_util_h

#include <stdio.h>

#define MD_FPROT_F_UONLY      (APR_FPROT_UREAD|APR_FPROT_UWRITE)
#define MD_FPROT_D_UONLY      (APR_FPROT_UREAD|APR_FPROT_UWRITE|APR_FPROT_UEXECUTE)

/**************************************************************************************************/
/* pool utils */

typedef apr_status_t md_util_action(void *baton, apr_pool_t *p, apr_pool_t *ptemp);
typedef apr_status_t md_util_vaction(void *baton, apr_pool_t *p, apr_pool_t *ptemp, va_list ap);

apr_status_t md_util_pool_do(md_util_action *cb, void *baton, apr_pool_t *p); 
apr_status_t md_util_pool_vdo(md_util_vaction *cb, void *baton, apr_pool_t *p, ...); 

/**************************************************************************************************/
/* string related */
void md_util_str_tolower(char *s);

/**************************************************************************************************/
/* file system related */

struct apr_file_t;

apr_status_t md_util_fopen(FILE **pf, const char *fn, const char *mode);

apr_status_t md_util_fcreatex(struct apr_file_t **pf, const char *fn, apr_pool_t *p);

apr_status_t md_util_path_merge(const char **ppath, apr_pool_t *p, ...);

apr_status_t md_util_is_dir(const char *path, apr_pool_t *pool);

typedef apr_status_t md_util_file_cb(void *baton, struct apr_file_t *f, apr_pool_t *p);

apr_status_t md_util_freplace(const char *path, const char *name, apr_pool_t *p, 
                              md_util_file_cb *write, void *baton);

/**************************************************************************************************/
/* base64 url encodings */
const char *md_util_base64url_encode(const char *data, 
                                     apr_size_t len, apr_pool_t *pool);
apr_size_t md_util_base64url_decode(const char **decoded, const char *encoded, 
                                    apr_pool_t *pool);

/**************************************************************************************************/
/* http/url related */
const char *md_util_schemify(apr_pool_t *p, const char *s, const char *def_scheme);

const char *md_link_find_relation(const apr_table_t *headers, 
                                  apr_pool_t *pool, const char *relation);

#endif /* md_util_h */
