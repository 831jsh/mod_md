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
#include <stdlib.h>

#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_buckets.h>
#include <apr_hash.h>
#include <apr_uri.h>

#include "../md.h"
#include "../md_crypt.h"
#include "../md_json.h"
#include "../md_jws.h"
#include "../md_http.h"
#include "../md_log.h"
#include "../md_reg.h"
#include "../md_store.h"
#include "../md_util.h"

#include "md_acme.h"
#include "md_acme_acct.h"
#include "md_acme_authz.h"


typedef struct acme_problem_status_t acme_problem_status_t;

struct acme_problem_status_t {
    const char *type;
    apr_status_t rv;
};

static acme_problem_status_t Problems[] = {
    { "acme:error:badCSR",                       APR_EINVAL },
    { "acme:error:badNonce",                     APR_EGENERAL },
    { "acme:error:badSignatureAlgorithm",        APR_EINVAL },
    { "acme:error:invalidContact",               APR_BADARG },
    { "acme:error:unsupportedContact",           APR_EGENERAL },
    { "acme:error:malformed",                    APR_EINVAL },
    { "acme:error:rateLimited",                  APR_BADARG },
    { "acme:error:rejectedIdentifier",           APR_BADARG },
    { "acme:error:serverInternal",               APR_EGENERAL },
    { "acme:error:unauthorized",                 APR_EACCES },
    { "acme:error:unsupportedIdentifier",        APR_BADARG },
    { "acme:error:userActionRequired",           APR_EAGAIN },
    { "acme:error:badRevocationReason",          APR_EINVAL },
    { "acme:error:caa",                          APR_EGENERAL },
    { "acme:error:dns",                          APR_EGENERAL },
    { "acme:error:connection",                   APR_EGENERAL },
    { "acme:error:tls",                          APR_EGENERAL },
    { "acme:error:incorrectResponse",            APR_EGENERAL },
};

static apr_status_t problem_status_get(const char *type) {
    int i;

    if (strstr(type, "urn:ietf:params:") == type) {
        type += strlen("urn:ietf:params:");
    }
    else if (strstr(type, "urn:") == type) {
        type += strlen("urn:");
    }
     
    for(i = 0; i < (sizeof(Problems)/sizeof(Problems[0])); ++i) {
        if (!apr_strnatcasecmp(type, Problems[i].type)) {
            return Problems[i].rv;
        }
    }
    return APR_EGENERAL;
}

apr_status_t md_acme_init(apr_pool_t *p)
{
    return md_crypt_init(p);
}

apr_status_t md_acme_create(md_acme_t **pacme, apr_pool_t *p, const char *url,
                            struct md_store_t *store)
{
    md_acme_t *acme;
    apr_uri_t uri;
    apr_status_t rv;
    size_t len;
    
    if (!url) {
        md_log_perror(MD_LOG_MARK, MD_LOG_ERR, APR_EINVAL, p, "create ACME without url");
        return APR_EINVAL;
    }
    
    if (APR_SUCCESS != (rv = apr_uri_parse(p, url, &uri))) {
        md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, p, "cannot parse ACME url: %s", url);
        return rv;
    }
    else if (!uri.hostname) {
        md_log_perror(MD_LOG_MARK, MD_LOG_ERR, APR_EINVAL, p, "ACME url hostname missing: %s", url);
        return APR_EINVAL;
    }
    
    acme = apr_pcalloc(p, sizeof(*acme));
    acme->url = url;
    acme->pool = p;
    acme->store = store;
    acme->pkey_bits = 4096;
    
    len = strlen(uri.hostname);
    acme->sname = (len <= 16)? uri.hostname : apr_pstrdup(p, uri.hostname + len - 16);
    
    *pacme = acme;
    return md_http_create(&acme->http, acme->pool);
}

apr_status_t md_acme_setup(md_acme_t *acme)
{
    apr_status_t rv;
    md_json_t *json;
    
    assert(acme->url);
    assert(acme->http);
    
    md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, 0, acme->pool, "get directory from %s", acme->url);
    
    rv = md_acme_get_json(&json, acme, acme->url, acme->pool);
    if (APR_SUCCESS == rv) {
        acme->new_authz = md_json_gets(json, "new-authz", NULL);
        acme->new_cert = md_json_gets(json, "new-cert", NULL);
        acme->new_reg = md_json_gets(json, "new-reg", NULL);
        acme->revoke_cert = md_json_gets(json, "revoke-cert", NULL);
        if (acme->new_authz && acme->new_cert && acme->new_reg && acme->revoke_cert) {
            return APR_SUCCESS;
        }
        rv = APR_EINVAL;
    }
    return rv;
}

/**************************************************************************************************/
/* acme requests */

static void req_update_nonce(md_acme_t *acme, apr_table_t *hdrs)
{
    if (hdrs) {
        const char *nonce = apr_table_get(hdrs, "Replay-Nonce");
        if (nonce) {
            acme->nonce = nonce;
        }
    }
}

static apr_status_t http_update_nonce(const md_http_response_t *res)
{
    if (res->headers) {
        const char *nonce = apr_table_get(res->headers, "Replay-Nonce");
        if (nonce) {
            md_acme_t *acme = res->req->baton;
            acme->nonce = nonce;
        }
    }
    return res->rv;
}

static apr_status_t md_acme_new_nonce(md_acme_t *acme)
{
    apr_status_t rv;
    long id;
    
    rv = md_http_HEAD(acme->http, acme->new_reg, NULL, http_update_nonce, acme, &id);
    md_http_await(acme->http, id);
    return rv;
}

static md_acme_req_t *md_acme_req_create(md_acme_t *acme, const char *url)
{
    apr_pool_t *pool;
    md_acme_req_t *req;
    apr_status_t rv;
    
    rv = apr_pool_create(&pool, acme->pool);
    if (rv != APR_SUCCESS) {
        return NULL;
    }
    
    req = apr_pcalloc(pool, sizeof(*req));
    if (!req) {
        apr_pool_destroy(pool);
        return NULL;
    }
        
    req->acme = acme;
    req->pool = pool;
    req->url = url;
    req->prot_hdrs = apr_table_make(pool, 5);
    if (!req->prot_hdrs) {
        apr_pool_destroy(pool);
        return NULL;
    }
    return req;
}
 
apr_status_t md_acme_req_body_init(md_acme_req_t *req, md_json_t *jpayload, md_pkey_t *key)
{
    const char *payload = md_json_writep(jpayload, MD_JSON_FMT_COMPACT, req->pool);
    size_t payload_len = strlen(payload);
    
    md_log_perror(MD_LOG_MARK, MD_LOG_TRACE1, 0, req->pool, 
                  "acct payload(len=%d): %s", payload_len, payload);
    return md_jws_sign(&req->req_json, req->pool, payload, payload_len,
                       req->prot_hdrs, key, NULL);
} 


static apr_status_t inspect_problem(md_acme_req_t *req, const md_http_response_t *res)
{
    const char *ctype;
    md_json_t *problem;
    
    ctype = apr_table_get(req->resp_hdrs, "content-type");
    if (ctype && !strcmp(ctype, "application/problem+json")) {
        /* RFC 7807 */
        md_json_read_http(&problem, req->pool, res);
        if (problem) {
            const char *ptype, *pdetail;
            
            req->resp_json = problem;
            ptype = md_json_gets(problem, "type", NULL); 
            pdetail = md_json_gets(problem, "detail", NULL);
            req->rv = problem_status_get(ptype);
             
            md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, req->rv, req->pool,
                          "acme problem %s: %s", ptype, pdetail);
            return req->rv;
        }
    }
    
    switch (res->status) {
        case 400:
            return APR_EINVAL;
        case 403:
            return APR_EACCES;
        case 404:
            return APR_ENOENT;
        default:
            md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, 0, req->pool,
                          "acme problem unknonw: http status %d", res->status);
            return APR_EGENERAL;
    }
}

/**************************************************************************************************/
/* GET JSON */

typedef struct {
    apr_status_t rv;
    apr_pool_t *pool;
    md_acme_t *acme;
    md_json_t *json;
} resp_data;

static apr_status_t json_resp_cb(const md_http_response_t *res)
{
    resp_data *resp = res->req->baton;
    req_update_nonce(resp->acme, res->headers);
    return md_json_read_http(&resp->json, resp->pool, res);
}

apr_status_t md_acme_get_json(struct md_json_t **pjson, md_acme_t *acme, 
                              const char *url, apr_pool_t *p)
{
    long req_id;
    apr_status_t rv;
    resp_data resp;
    
    memset(&resp, 0, sizeof(resp));
    resp.pool = p;
    resp.acme = acme;
    
    rv = md_http_GET(acme->http, url, NULL, json_resp_cb, &resp, &req_id);
    
    if (rv == APR_SUCCESS) {
        md_http_await(acme->http, req_id);
        *pjson = resp.json;
        return resp.rv;
    }
    *pjson = NULL;
    return rv;
}

/**************************************************************************************************/
/* POST requests */

static apr_status_t md_acme_req_done(md_acme_req_t *req)
{
    apr_status_t rv = req->rv;
    if (req->pool) {
        apr_pool_destroy(req->pool);
    }
    return rv;
}

static apr_status_t on_response(const md_http_response_t *res)
{
    md_acme_req_t *req = res->req->baton;
    apr_status_t rv = res->rv;
    
    if (rv != APR_SUCCESS) {
        md_log_perror(MD_LOG_MARK, MD_LOG_TRACE1, rv, res->req->pool, "req failed");
        return rv;
    }
    
    req->resp_hdrs = apr_table_clone(req->pool, res->headers);
    req_update_nonce(req->acme, res->headers);
    
    /* TODO: Redirect Handling? */
    if (res->status >= 200 && res->status < 300) {
        rv = md_json_read_http(&req->resp_json, req->pool, res);
        if (rv != APR_SUCCESS) {
                md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, rv, req->pool, 
                              "unable to parse JSON response body");
                return APR_EINVAL;
        }
        
        if (md_log_is_level(req->pool, MD_LOG_TRACE2)) {
            md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, rv, req->pool,
                          "acme response: %s", md_json_writep(req->resp_json, 
                                                              MD_JSON_FMT_INDENT, req->pool));
        }
    
        if (req->on_success) {
            req->rv = req->on_success(req->acme, req->resp_hdrs, req->resp_json, req->baton);
        }
    }
    else {
        req->rv = rv;
        rv = inspect_problem(req, res);
    }
    
    md_acme_req_done(req);
    return rv;
}

static apr_status_t md_acme_req_send(md_acme_req_t *req)
{
    apr_status_t rv;
    md_acme_t *acme = req->acme;

    assert(acme->url);
    
    if (!acme->new_authz) {
        if (APR_SUCCESS != (rv = md_acme_setup(acme))) {
            return rv;
        }
    }
    if (!acme->nonce) {
        if (APR_SUCCESS != (rv = md_acme_new_nonce(acme))) {
            return rv;
        }
    }
    
    apr_table_set(req->prot_hdrs, "nonce", acme->nonce);
    acme->nonce = NULL;

    rv = req->on_init(req, req->baton);
    
    if (rv == APR_SUCCESS) {
        long id;
        const char *body = NULL;
    
        if (req->req_json) {
            body = md_json_writep(req->req_json, MD_JSON_FMT_INDENT, req->pool);
        }
        
        if (body && md_log_is_level(req->pool, MD_LOG_TRACE2)) {
            md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, 0, req->pool, 
                          "req: POST %s, body:\n%s", req->url, body);
        }
        else {
            md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, 0, req->pool, 
                          "req: POST %s\n", req->url);
        }
        rv = md_http_POSTd(req->acme->http, req->url, NULL, "application/json",  
                               body, body? strlen(body) : 0, on_response, req, &id);
        req = NULL;
        md_http_await(acme->http, id);
    }

    if (req) {
        md_acme_req_done(req);
    }
    return rv;
}

apr_status_t md_acme_req_do(md_acme_t *acme, const char *url,
                            md_acme_req_init_cb *on_init,
                            md_acme_req_success_cb *on_success,
                            void *baton)
{
    md_acme_req_t *req;
    
    assert(url);
    md_log_perror(MD_LOG_MARK, MD_LOG_TRACE1, 0, acme->pool, "add acme req: %s", url);
    req = md_acme_req_create(acme, url);
    req->on_init = on_init;
    req->on_success = on_success;
    req->baton = baton;
    
    return md_acme_req_send(req);
}

/**************************************************************************************************/
/* protocol driver */

typedef struct {
    md_proto_driver_t *driver;
    md_acme_t *acme;
    md_acme_acct_t *acct;
    md_t *md;
    
    md_acme_authz_set_t *authz_set;
    
} md_acme_driver_t;

/**************************************************************************************************/
/* account setup */

static apr_status_t ad_acct_validate(md_proto_driver_t *d, md_acme_acct_t **pacct)
{
    md_acme_driver_t *ad = d->baton;
    md_acme_acct_t *acct = *pacct;
    apr_status_t rv;
    
    if (APR_SUCCESS != (rv = md_acme_acct_validate(ad->acme, *pacct))) {
        if (APR_ENOENT == rv || APR_EACCES == rv) {
            *pacct = NULL;
            rv = md_acme_acct_disable(acct);
        }
    }
    return rv;
}

static apr_status_t ad_set_acct(md_proto_driver_t *d) 
{
    md_acme_driver_t *ad = d->baton;
    md_t *md = ad->md;
    md_acme_acct_t *acct = NULL;
    apr_status_t rv = APR_SUCCESS;

    ad->acct = NULL;
    md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, d->p, "%s: finding account",
                  d->proto->protocol);
    
    /* Get an account for the ACME server for this MD */
    if (ad->md->ca_account) {
        md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, d->p, "%s: checking previous account %s",
                      d->proto->protocol, md->ca_account);
        if (APR_SUCCESS == (rv = md_acme_acct_load(&acct, d->store, md->ca_account, d->p))) {
            rv = ad_acct_validate(d, &acct);
        }
        else if (APR_ENOENT == rv) {
            rv = APR_SUCCESS;
        }
    }
    
    /* If MD has no account, find a local account for server, store at MD */ 
    if (APR_SUCCESS == rv && NULL == acct) {
        md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, d->p, "%s: looking at existing accounts",
                      d->proto->protocol);
        while (NULL == acct 
               && APR_SUCCESS == (rv = md_acme_acct_find(&acct, d->store, ad->acme, d->p))) {
            rv = ad_acct_validate(d, &acct);
        }
        if (!acct && APR_ENOENT == rv) {
            rv = APR_SUCCESS;
        }
    }
    
    if (APR_SUCCESS == rv && NULL == acct) {
        /* 2.2 No local account exists, create a new one */
        md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, d->p, "%s: creating new account", 
                      d->proto->protocol);

        if (!ad->md->contacts || apr_is_empty_array(md->contacts)) {
            md_log_perror(MD_LOG_MARK, MD_LOG_ERR, APR_EINVAL, d->p, 
                "no contact information for md %s", md->name);            
            return APR_EINVAL;
        }
        
        rv = md_acme_register(&acct, d->store, ad->acme, md->contacts, md->ca_agreement);
        if (APR_SUCCESS != rv) {
            md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, d->p, "register new account");
        }
    }
    
    if (APR_SUCCESS == rv) {
        ad->acct = acct;
        /* Persist the account chosen at the md so we use the same on future runs */
        if (!md->ca_account || strcmp(md->ca_account, acct->id)) {
            md->ca_account = acct->id;
            rv = md_save(d->store, md, 0);
        }
    }

    return rv;
}

/**************************************************************************************************/
/* authz/challenge/cert setup */

/**
 * Pre-Req: we have an account for the ACME server that has accepted the current license agreement
 * For each domain in MD: 
 * - check if there already is a valid AUTHZ resource
 * - if ot, create an AUTHZ resource with challenge data 
 */
static apr_status_t ad_setup_authz(md_proto_driver_t *d)
{
    md_acme_driver_t *ad = d->baton;
    apr_status_t rv;
    md_t *md = ad->md;
    md_acme_authz_t *authz;
    int i, changed;
    
    assert(ad->md);
    assert(ad->acme);
    assert(ad->acct);

    /* For each domain in MD: AUTHZ setup
     * if an AUTHZ resource is known, check if it is still valid
     * if known AUTHZ resource is not valid, remove, goto 4.1.1
     * if no AUTHZ available, create a new one for the domain, store it
     */
    rv = md_acme_authz_set_load(d->store, md->name, &ad->authz_set, d->p);
    md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, d->p, "%s: loading authz data", md->name);
    if (APR_ENOENT == rv) {
        ad->authz_set = md_acme_authz_set_create(d->p, ad->acct->id);
        rv = APR_SUCCESS;
    }
    
    for (i = 0; i < md->domains->nelts && APR_SUCCESS == rv; ++i) {
        const char *domain = APR_ARRAY_IDX(md->domains, i, const char *);
        changed = 0;
        authz = md_acme_authz_set_get(ad->authz_set, domain);
        if (authz) {
            /* check valid */
            rv = md_acme_authz_update(authz, ad->acme, ad->acct, d->p);
            md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, d->p, "%s: updated authz for %s", 
                          md->name, domain);
            if (APR_SUCCESS != rv) {
                md_acme_authz_set_remove(ad->authz_set, domain);
                authz = NULL;
                changed = 1;
            }
        }
        if (!authz) {
            /* create new one */
            rv = md_acme_authz_register(&authz, ad->acme, domain, ad->acct, d->p);
            md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, d->p, "%s: created authz for %s", 
                          md->name, domain);
            if (APR_SUCCESS == rv) {
                rv = md_acme_authz_set_add(ad->authz_set, authz);
                changed = 1;
            }
        }
        if (APR_SUCCESS == rv && changed) {
            rv = md_acme_authz_set_save(d->store, md->name, ad->authz_set, 0);
        }
    }
    
    return rv;
}

/**
 * Pre-Req: all domains have a AUTHZ resources at the ACME server
 * For each domain in MD: 
 * - if AUTHZ resource is 'valid' -> continue
 * - if AUTHZ resource is 'pending':
 *   - find preferred challenge choice
 *   - calculate challenge data for httpd to find
 *   - POST challenge start to ACME server
 * For each domain in MD where AUTHZ is 'pending', until overall timeout: 
 *   - wait a certain time, check status again
 * If not all AUTHZ are valid, fail
 */
static apr_status_t ad_start_challenges(md_proto_driver_t *d)
{
    md_acme_driver_t *ad = d->baton;
    apr_status_t rv = APR_SUCCESS;
    md_acme_authz_t *authz;
    int i;
    
    assert(ad->md);
    assert(ad->acme);
    assert(ad->acct);
    assert(ad->authz_set);
    assert(ad->authz_set->authzs->nelts == ad->md->domains->nelts);

    for (i = 0; i < ad->authz_set->authzs->nelts && APR_SUCCESS == rv; ++i) {
        authz = APR_ARRAY_IDX(ad->authz_set->authzs, i, md_acme_authz_t*);
        md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, d->p, "%s: check AUTHZ for %s", 
                      ad->md->name, authz->domain);
        if (APR_SUCCESS == (rv = md_acme_authz_update(authz, ad->acme, ad->acct, d->p))) {
            switch (authz->state) {
                case MD_ACME_AUTHZ_S_VALID:
                    break;
                case MD_ACME_AUTHZ_S_PENDING:
                    rv = md_acme_authz_respond(authz, ad->acme, ad->acct, d->store, d->p);
                    break;
                default:
                    rv = APR_EINVAL;
                    md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, d->p, 
                                  "%s: unexpected AUTHZ state %d at %s", 
                                  authz->domain, authz->state, authz->location);
                    break;
            }
        }
    }
    
    return rv;
}

static apr_status_t ad_monitor_challenges(md_proto_driver_t *d)
{
    return APR_ENOTIMPL;
}

/**
 * Pre-Req: all domains have been validated by the ACME server, e.g. all have AUTHZ
 * resources that have status 'valid'
 * - Setup private key, if not already there
 * - Generate a CSR with org, contact, etc
 * - Optionally enable must-staple OCSP extension
 * - Submit CSR, expect 201 with location
 * - POLL location for certificate
 * - store certificate
 * - retrieve cert chain information from cert
 * - GET cert chain
 * - store cert chain
 */
static apr_status_t ad_setup_certificate(md_proto_driver_t *d)
{
    return APR_ENOTIMPL;
}

/**************************************************************************************************/
/* ACME driving */

static apr_status_t acme_driver_init(md_proto_driver_t *d)
{
    md_acme_driver_t *ad;
    
    ad = apr_pcalloc(d->p, sizeof(*ad));
    
    d->baton = ad;
    ad->driver = d;
    ad->md = md_copy(d->p, d->md);
    
    md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, 0, d->p, "%s: driving md %s", 
                  d->proto->protocol, ad->md->name);
    
    /* Find out where we're at with this managed domain */
    return md_acme_create(&ad->acme, d->p, ad->md->ca_url, d->store);
}

static apr_status_t acme_driver_run(md_proto_driver_t *d)
{
    apr_status_t rv = APR_ENOTIMPL;
    md_acme_driver_t *ad = d->baton;
    const char *step;

    assert(ad->md);
    assert(ad->acme);

    step = "ACME setup";
    rv = md_acme_setup(ad->acme);
    
    /* TODO: which challenge types do we support? 
     * Need to know if the server listens to the right ports */
    ad->acme->can_cha_http_01 = 1;

    /* Chose (or create) and ACME account to use */
    if (APR_SUCCESS == rv) {
        step = "choose account";
        rv = ad_set_acct(d);
    }
    
    /* Check that the account agreed to the terms-of-service, otherwise
     * requests for new authorizations are denied. ToS may change during the
     * lifetime of an account */
    if (APR_SUCCESS == rv) {
        step = "check agreement";
        rv = md_acme_acct_check_agreement(ad->acme, ad->acct, ad->md->ca_agreement);
    }
    
    /* Check that we have authz resources with challenge info for each domain */
    if (APR_SUCCESS == rv) {
        step = "check authz";
        rv = ad_setup_authz(d);
    }
    
    /* Start challenges */
    if (APR_SUCCESS == rv) {
        step = "setup challenges";
        rv = ad_start_challenges(d);
    }
    
    /* monitor authz status */
    if (APR_SUCCESS == rv) {
        step = "setup challenges";
        rv = ad_monitor_challenges(d);
    }
    
    /* Setup the certificate */
    if (APR_SUCCESS == rv) {
        step = "create certificate";
        rv = ad_setup_certificate(d);
    }
    
    /* Update MD expiration date */
    if (APR_SUCCESS == rv) {
        step = "completed";
    }
        
    md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, d->p, "%s drive %s, %s", 
                  d->proto->protocol, ad->md->name, step);
    return rv;
}

static md_proto_t ACME_PROTO = {
    MD_PROTO_ACME, acme_driver_init, acme_driver_run
};
 
apr_status_t md_acme_protos_add(apr_hash_t *protos, apr_pool_t *p)
{
    apr_hash_set(protos, MD_PROTO_ACME, sizeof(MD_PROTO_ACME)-1, &ACME_PROTO);
    return APR_SUCCESS;
}
