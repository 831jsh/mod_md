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

#include <apr_lib.h>
#include <apr_strings.h>
#include <apr_buckets.h>

#include "md_acme.h"
#include "md_acme_acct.h"
#include "md_crypt.h"
#include "md_json.h"
#include "md_jws.h"
#include "md_http.h"
#include "md_log.h"

typedef struct acme_problem_status_t acme_problem_status_t;
struct acme_problem_status_t {
    const char *type;
    apr_status_t status;
};

static acme_problem_status_t Problems[] = {
};

static apr_status_t problem_status_get(const char *type) {
    int i;
    
    for(i = 0; i < (sizeof(Problems)/sizeof(Problems[0])); ++i) {
        if (!apr_strnatcasecmp(type, Problems[i].type)) {
            return Problems[i].status;
        }
    }
    return APR_EGENERAL;
}

apr_status_t md_acme_init(apr_pool_t *p)
{
    return md_crypt_init(p);
}

apr_status_t md_acme_create(md_acme **pacme, apr_pool_t *p, const char *url)
{
    md_acme *acme;
    
    acme = apr_pcalloc(p, sizeof(*acme));
    if (!acme) {
        return APR_ENOMEM;
    }
    
    acme->url = url;
    acme->state = MD_ACME_S_INIT;
    acme->pool = p;
    *pacme = acme;
    
    return md_http_create(&acme->http, p);
}

apr_status_t md_acme_setup(md_acme *acme)
{
    apr_status_t status;
    md_json *json;
    
    md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, 0, acme->pool, "get directory from %s", acme->url);
    
    status = md_json_http_get(&json, acme->pool, acme->http, acme->url);
    if (status == APR_SUCCESS) {
        acme->new_authz = md_json_gets(json, "new-authz", NULL);
        acme->new_cert = md_json_gets(json, "new-cert", NULL);
        acme->new_reg = md_json_gets(json, "new-reg", NULL);
        acme->revoke_cert = md_json_gets(json, "revoke-cert", NULL);
        if (acme->new_authz && acme->new_cert && acme->new_reg && acme->revoke_cert) {
            acme->state = MD_ACME_S_LIVE;
            return APR_SUCCESS;
        }
        acme->state = MD_ACME_S_INIT;
        status = APR_EINVAL;
    }
    return status;
}

/**************************************************************************************************/
/* acme accounts */
apr_status_t md_acme_add_acct(md_acme *acme, struct md_acme_acct *acct)
{
    /* TODO: n accounts */
    acme->acct = acct;
    return APR_SUCCESS;
}

md_acme_acct *md_acme_get_acct(md_acme *acme, const char *url)
{
    md_acme_acct *acct = acme->acct;
    
    if (acct && acct->url && !strcmp(url, acct->url)) {
        return acct;
    }
    return NULL;
}

apr_status_t md_acme_remove_acct(md_acme *acme, struct md_acme_acct *acct)
{
    if (acct == acme->acct) {
        acme->acct = NULL;
        return APR_SUCCESS;
    }
    return APR_NOTFOUND;
}

/**************************************************************************************************/
/* acme requests */

static void req_update_nonce(md_acme_req *req)
{
    if (req->resp_hdrs) {
        const char *nonce = apr_table_get(req->resp_hdrs, "Replay-Nonce");
        if (nonce) {
            req->acme->nonce = nonce;
        }
    }
}

static apr_status_t http_update_nonce(const md_http_response *res)
{
    if (res->headers) {
        const char *nonce = apr_table_get(res->headers, "Replay-Nonce");
        if (nonce) {
            md_acme *acme = res->req->baton;
            acme->nonce = nonce;
        }
    }
    return res->rv;
}

static apr_status_t md_acme_new_nonce(md_acme *acme)
{
    apr_status_t status;
    long id;
    
    status = md_http_HEAD(acme->http, acme->new_reg, NULL, http_update_nonce, acme, &id);
    md_http_await(acme->http, id);
    return status;
}

static md_acme_req *md_acme_req_create(md_acme *acme, const char *url)
{
    apr_pool_t *pool;
    md_acme_req *req;
    apr_status_t status;
    
    status = apr_pool_create(&pool, acme->pool);
    if (status != APR_SUCCESS) {
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
 
static apr_status_t inspect_problem(md_acme_req *req, const md_http_response *res)
{
    const char *ctype;
    md_json *problem;
    
    ctype = apr_table_get(req->resp_hdrs, "content-type");
    if (ctype && !strcmp(ctype, "application/problem+json")) {
        /* RFC 7807 */
        md_json_read_http(&problem, req->pool, res);
        if (problem) {
            const char *ptype, *pdetail;
            
            req->resp_json = problem;
            ptype = md_json_gets(problem, "type", NULL); 
            pdetail = md_json_gets(problem, "detail", NULL);
            req->status = problem_status_get(ptype);
             
            md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, req->status, req->pool,
                          "acme problem %s: %s", ptype, pdetail);
            return req->status;
        }
    }
    md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, 0, req->pool,
                  "acme problem unknonw: http status %d", res->status);
    return APR_EGENERAL;
}

static apr_status_t md_acme_req_done(md_acme_req *req)
{
    apr_status_t status = req->status;
    if (req->pool) {
        apr_pool_destroy(req->pool);
    }
    return status;
}

static apr_status_t on_response(const md_http_response *res)
{
    md_acme_req *req = res->req->baton;
    const char *location;
    apr_status_t status = res->rv;
    
    if (status != APR_SUCCESS) {
        md_log_perror(MD_LOG_MARK, MD_LOG_TRACE1, status, res->req->pool, "req failed");
        return status;
    }
    
    req->resp_hdrs = apr_table_clone(req->pool, res->headers);
    req_update_nonce(req);
    
    /* TODO: Redirect Handling? */
    if (res->status >= 200 && res->status < 300) {
        location = apr_table_get(req->resp_hdrs, "location");
        if (!location) {
            if (res->status == 201) {
                md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, APR_EINVAL, req->pool, 
                              "201 response without location header");
                return APR_EINVAL;
            }
            location = req->url;
        }
        
        status = md_json_read_http(&req->resp_json, req->pool, res);
        if (status != APR_SUCCESS) {
                md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, status, req->pool, 
                              "unable to parse JSON response body");
                return APR_EINVAL;
        }
        
        if (md_log_is_level(req->pool, MD_LOG_TRACE2)) {
            md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, status, req->pool,
                          "acme response: %s", md_json_writep(req->resp_json, 
                                                              MD_JSON_FMT_INDENT, req->pool));
        }
    
        if (req->on_success) {
            req->status = status;
            req->on_success(req->acme, location, req->resp_json, req->baton);
        }
    }
    else {
        req->status = status;
        status = inspect_problem(req, res);
    }
    
    md_acme_req_done(req);
    return status;
}

static apr_status_t md_acme_req_send(md_acme_req *req)
{
    apr_status_t status;
    md_acme *acme = req->acme;

    if (!acme->nonce) {
        status = md_acme_new_nonce(acme);
        if (status != APR_SUCCESS) {
            return status;
        }
    }
    
    apr_table_set(req->prot_hdrs, "nonce", acme->nonce);
    acme->nonce = NULL;

    status = req->on_init(req, req->baton);
    
    if (status == APR_SUCCESS) {
        long id;
        const char *body = NULL;
    
        if (req->req_json) {
            body = md_json_writep(req->req_json, MD_JSON_FMT_INDENT, req->pool);
            if (!body) {
                status = APR_ENOMEM;
                goto out;
            }
        }
        
        if (body && md_log_is_level(req->pool, MD_LOG_TRACE2)) {
            md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, 0, req->pool, 
                          "req: POST %s, body:\n%s", req->url, body);
        }
        else {
            md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, 0, req->pool, 
                          "req: POST %s\n", req->url);
        }
        status = md_http_POSTd(req->acme->http, req->url, NULL, "application/json",  
                               body, body? strlen(body) : 0, on_response, req, &id);
        req = NULL;
        md_http_await(acme->http, id);
    }
out:
    if (req) {
        md_acme_req_done(req);
    }
    return status;
}

apr_status_t md_acme_req_add(md_acme *acme, const char *url,
                             md_acme_req_init_cb *on_init,
                             md_acme_req_success_cb *on_success,
                             void *baton)
{
    md_acme_req *req;
    
    md_log_perror(MD_LOG_MARK, MD_LOG_TRACE1, 0, acme->pool, "add acme req: %s", url);
    req = md_acme_req_create(acme, url);
    if (req) {
        req->on_init = on_init;
        req->on_success = on_success;
        req->baton = baton;
    
        return md_acme_req_send(req);
    }
    return APR_ENOMEM;
}

