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

#ifndef mod_md_md_crypt_h
#define mod_md_md_crypt_h

struct apr_array_header_t;
struct md_t;
struct md_http_response_t;

typedef struct md_cert_t md_cert_t;
typedef struct md_pkey_t md_pkey_t;

apr_status_t md_crypt_init(apr_pool_t *pool);

apr_status_t md_pkey_fload(md_pkey_t **ppkey, apr_pool_t *p, const char *fname);
apr_status_t md_pkey_fload_rsa(md_pkey_t **ppkey, apr_pool_t *p, const char *fname);

void md_pkey_free(md_pkey_t *pkey);

apr_status_t md_pkey_fsave(md_pkey_t *pkey, apr_pool_t *p, const char *fname);

apr_status_t md_pkey_gen_rsa(md_pkey_t **ppkey, apr_pool_t *p, int bits);

const char *md_pkey_get_rsa_e64(md_pkey_t *pkey, apr_pool_t *p);
const char *md_pkey_get_rsa_n64(md_pkey_t *pkey, apr_pool_t *p);

apr_status_t md_crypt_sign64(const char **psign64, md_pkey_t *pkey, apr_pool_t *p, 
                             const char *d, size_t dlen);

apr_status_t md_crypt_sha256_digest64(const char **pdigest64, apr_pool_t *p, 
                                      const char *d, size_t dlen);

typedef enum {
    MD_CERT_UNKNOWN,
    MD_CERT_VALID,
    MD_CERT_EXPIRED
} md_cert_state_t;

void md_cert_free(md_cert_t *cert);

apr_status_t md_cert_fload(md_cert_t **pcert, apr_pool_t *p, const char *fname);
apr_status_t md_cert_fsave(md_cert_t *cert, apr_pool_t *p, const char *fname);

apr_status_t md_cert_read_http(md_cert_t **pcert, apr_pool_t *pool, 
                               const struct md_http_response_t *res);

md_cert_state_t md_cert_state_get(md_cert_t *cert);

apr_status_t md_chain_fload(struct apr_array_header_t **pcerts, 
                            apr_pool_t *p, const char *fname);
apr_status_t md_chain_fsave(struct apr_array_header_t *certs, 
                            apr_pool_t *p, const char *fname);

apr_status_t md_cert_req_create(const char **pcsr_der_64, const struct md_t *md, 
                                md_pkey_t *pkey, apr_pool_t *p);

int md_cert_is_valid_now(const md_cert_t *cert);
int md_cert_has_expired(const md_cert_t *cert);
int md_cert_covers_md(md_cert_t *cert, const struct md_t *md);

apr_status_t md_cert_get_issuers_uri(const char **puri, md_cert_t *cert, apr_pool_t *p);
apr_status_t md_cert_get_alt_names(apr_array_header_t **pnames, md_cert_t *cert, apr_pool_t *p);

#endif /* md_crypt_h */
