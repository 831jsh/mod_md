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

#include <stdio.h>
#include <stdlib.h>

#include <apr_lib.h>
#include <apr_buckets.h>
#include <apr_getopt.h>
#include <apr_hash.h>
#include <apr_strings.h>

#include "md.h"
#include "acme/md_acme.h"
#include "acme/md_acme_acct.h"
#include "acme/md_acme_authz.h"
#include "md_json.h"
#include "md_http.h"
#include "md_log.h"
#include "md_reg.h"
#include "md_store.h"
#include "md_util.h"
#include "mod_md.h"
#include "md_version.h"
#include "md_cmd.h"
#include "md_cmd_acme.h"

/**************************************************************************************************/
/* command: acme newreg */

static apr_status_t cmd_acme_newreg(md_cmd_ctx *ctx, const md_cmd_t *cmd)
{
    apr_status_t rv = APR_SUCCESS;
    md_acme_acct_t *acct;
    int i;
    
    apr_array_header_t *contacts = apr_array_make(ctx->p, 5, sizeof(const char *));
    for (i = 0; i < ctx->argc; ++i) {
        APR_ARRAY_PUSH(contacts, const char *) = md_util_schemify(ctx->p, ctx->argv[i], "mailto");
    }
    if (apr_is_empty_array(contacts)) {
        return usage(cmd, "newreg needs at least one contact email as argument");
    }

    rv = md_acme_register(&acct, ctx->store, ctx->acme, contacts, ctx->tos);
    
    if (rv == APR_SUCCESS) {
        fprintf(stdout, "registered: %s\n", acct->id);
    }
    else {
        md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, ctx->p, "register new account");
    }
    
    return rv;
}

static md_cmd_t AcmeNewregCmd = {
    "newreg", MD_CTX_ACME, 
    NULL, cmd_acme_newreg, MD_NoOptions, NULL,
    "newreg contact-uri [contact-uri...]",
    "register a new account at ACME server with give contact uri (email)",
};

/**************************************************************************************************/
/* command: acme agree */

static apr_status_t acct_agree_tos(md_store_t *store, const char *name, 
                                   const char *tos, apr_pool_t *p) 
{
    md_http_t *http;
    md_acme_acct_t *acct;
    apr_status_t rv;
    long req_id;
    const char *data;
    md_json_t *json;
    
    if (APR_SUCCESS == (rv = md_acme_acct_load(&acct, store, name, p))) {
        if (!tos) {
            tos = acct->tos;
            if (!tos) {
                md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, rv, p, 
                              "terms-of-service not specified (--terms), using default %s", 
                              TOS_DEFAULT);
                tos = TOS_DEFAULT;
            }
        }
        rv = md_acme_acct_agree_tos(acct, tos);
        if (rv == APR_SUCCESS) {
            fprintf(stdout, "agreed terms-of-service: %s\n", acct->url);
        }
        else {
            md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, p, "agree to terms-of-service %s", tos);
        }
    }
    else if (APR_ENOENT == rv) {
        md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, p, "unknown account: %s", name);
    }

    return rv;
}

static apr_status_t cmd_acme_agree(md_cmd_ctx *ctx, const md_cmd_t *cmd)
{
    apr_status_t rv = APR_SUCCESS;
    int i;
    
    for (i = 0; i < ctx->argc; ++i) {
        rv = acct_agree_tos(ctx->store, ctx->argv[i], ctx->tos, ctx->p);
        if (rv != APR_SUCCESS) {
            break;
        }
    }
    return rv;
}

static md_cmd_t AcmeAgreeCmd = {
    "agree", MD_CTX_STORE, 
    NULL, cmd_acme_agree, MD_NoOptions, NULL,
    "agree account",
    "agree to ACME terms of service",
};

/**************************************************************************************************/
/* command: acme validate */

static apr_status_t acct_validate(md_store_t *store, const char *name, apr_pool_t *p) 
{
    md_http_t *http;
    md_acme_acct_t *acct;
    apr_status_t rv;
    long req_id;
    const char *data;
    md_json_t *json;
    
    if (APR_SUCCESS == (rv = md_acme_acct_load(&acct, store, name, p))) {
    
        rv = md_acme_acct_validate(acct);
        if (rv == APR_SUCCESS) {
            fprintf(stdout, "account valid: %s\n", acct->url);
        }
        else {
            md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, p, "validating account: %s", acct->url);
        }
    }
    else if (APR_ENOENT == rv) {
        fprintf(stderr, "unknown account: %s", name);
    }

    return rv;
}

static apr_status_t cmd_acme_validate(md_cmd_ctx *ctx, const md_cmd_t *cmd)
{
    apr_status_t rv = APR_SUCCESS;
    int i;
    
    for (i = 0; i < ctx->argc; ++i) {
        rv = acct_validate(ctx->store, ctx->argv[i], ctx->p);
        if (rv != APR_SUCCESS) {
            break;
        }
    }
    return rv;
}

static md_cmd_t AcmeValidateCmd = {
    "validate", MD_CTX_STORE, 
    NULL, cmd_acme_validate, MD_NoOptions, NULL,
    "validate account",
    "validate account existence",
};

/**************************************************************************************************/
/* command: acme delreg */

static apr_status_t acme_delreg(md_store_t *store, const char *name, apr_pool_t *p) 
{
    md_http_t *http;
    md_acme_acct_t *acct;
    apr_status_t rv;
    long req_id;
    const char *data;
    md_json_t *json;
    
    if (APR_SUCCESS == (rv = md_acme_acct_load(&acct, store, name, p))) {
        rv = md_acme_acct_del(acct);
        if (rv == APR_SUCCESS) {
            fprintf(stdout, "deleted: %s\n", acct->url);
        }
        else {
            md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, p, "delete account");
        }
    }
    else if (APR_ENOENT == rv) {
        md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, p, "unknown account: %s", name);
    }
    else {
        md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, p, "loading account: %s", name);
    }
    return rv;
}

static apr_status_t cmd_acme_delreg(md_cmd_ctx *ctx, const md_cmd_t *cmd)
{
    apr_status_t rv = APR_SUCCESS;
    int i;
    
    for (i = 0; i < ctx->argc; ++i) {
        rv = acme_delreg(ctx->store, ctx->argv[i], ctx->p);
        if (rv != APR_SUCCESS) {
            break;
        }
    }
    return rv;
}

static md_cmd_t AcmeDelregCmd = {
    "delreg", MD_CTX_STORE, 
    NULL, cmd_acme_delreg, MD_NoOptions, NULL,
    "delreg account",
    "delete an existing ACME account",
};

/**************************************************************************************************/
/* command: acme authz */

static apr_status_t acme_newauthz(md_acme_acct_t *acct, const char *domain) 
{
    apr_status_t rv;
    long req_id;
    const char *data;
    md_json_t *json;
    md_acme_authz_t *authz;
    
    rv = md_acme_authz_register(&authz, domain, acct); 
    
    if (rv == APR_SUCCESS) {
        fprintf(stdout, "authz: %s %s\n", domain, authz->url);
    }
    else {
        md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, acct->pool, "register new authz");
    }
    return rv;
}

static apr_status_t cmd_acme_authz(md_cmd_ctx *ctx, const md_cmd_t *cmd)
{
    const char *s;
    md_acme_acct_t *acct;
    apr_status_t rv;
    int i;
    
    if (ctx->argc <= 0) {
        return usage(cmd, NULL);
    }
    s = ctx->argv[0];
    if (APR_SUCCESS == (rv = md_acme_acct_load(&acct, ctx->store, s, ctx->p))) {
        for (i = 1; i < ctx->argc; ++i) {
            rv = acme_newauthz(acct, ctx->argv[i]);
            if (rv != APR_SUCCESS) {
                break;
            }
        }
    }
    else if (APR_ENOENT == rv) {
        fprintf(stderr, "unknown account: %s\n", s);
        return APR_EGENERAL;
    }
    
    return rv;
}

static md_cmd_t AcmeAuthzCmd = {
    "authz", MD_CTX_STORE, 
    NULL, cmd_acme_authz, MD_NoOptions, NULL,
    "authz account domain",
    "request a new authorization for an account and domain",
};

/**************************************************************************************************/
/* command: acme */

static const md_cmd_t *AcmeSubCmds[] = {
    &AcmeNewregCmd,
    &AcmeDelregCmd,
    &AcmeAgreeCmd,
    &AcmeAuthzCmd,
    &AcmeValidateCmd,
    NULL
};

md_cmd_t MD_AcmeCmd = {
    "acme", MD_CTX_STORE,  
    NULL, NULL, MD_NoOptions, AcmeSubCmds,
    "acme cmd [opts] [args]", 
    "play with the ACME server", 
};
