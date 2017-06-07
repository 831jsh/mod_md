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

#ifndef mod_md_md_acme_authz_h
#define mod_md_md_acme_authz_h

struct apr_array_header_t;
struct md_acme_t;
struct md_acme_acct_t;
struct md_json_t;
struct md_store_t;

typedef struct md_acme_challenge_t md_acme_challenge_t;

typedef struct md_acme_authz_t md_acme_authz_t;

struct md_acme_authz_t {
    const char *domain;
    const char *location;
    struct md_json_t *resource;
    apr_time_t expires;
};

typedef struct md_acme_authz_set_t md_acme_authz_set_t;

struct md_acme_authz_set_t {
    const char *acct_id;
    struct apr_array_header_t *authzs;
};

struct md_json_t *md_acme_authz_set_to_json(md_acme_authz_set_t *set, apr_pool_t *p);
md_acme_authz_set_t *md_acme_authz_set_from_json(struct md_json_t *json, apr_pool_t *p);

apr_status_t md_acme_authz_register(struct md_acme_authz_t **pauthz, md_acme_t *acme, 
                                    const char *domain, md_acme_acct_t *acct, apr_pool_t *p);

apr_status_t md_acme_authz_set_load(struct md_store_t *store, const char *name, 
                                    md_acme_authz_set_t **pauthz_set, apr_pool_t *p);

#endif /* md_acme_authz_h */
