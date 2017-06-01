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
#include <stdio.h>
#include <stdlib.h>

#include <apr_lib.h>
#include <apr_file_io.h>
#include <apr_strings.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include "md_crypt.h"
#include "md_log.h"
#include "md_util.h"

static int initialized;

struct md_pkey_t {
    apr_pool_t *pool;
    EVP_PKEY   *pkey;
};

apr_status_t md_crypt_init(apr_pool_t *pool)
{
    char seed[64];
    (void)pool;

    if (!initialized) {
        ERR_load_crypto_strings();
    
        md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, 0, pool, "initializing RAND"); 
        while (!RAND_status()) {
            arc4random_buf(seed, sizeof(seed));
            RAND_seed(seed, sizeof(seed));
	}

        initialized = 1;
    }
    return APR_SUCCESS;
}

/**************************************************************************************************/
/* private keys */

static md_pkey_t *make_pkey(apr_pool_t *p) 
{
    md_pkey_t *pkey = apr_pcalloc(p, sizeof(*pkey));
    pkey->pool = p;
    return pkey;
}

static apr_status_t pkey_cleanup(void *data)
{
    md_pkey_t *pkey = data;
    if (pkey->pkey) {
        EVP_PKEY_free(pkey->pkey);
        pkey->pkey = NULL;
    }
    return APR_SUCCESS;
}

void md_pkey_free(md_pkey_t *pkey)
{
    pkey_cleanup(pkey);
}

apr_status_t md_pkey_load(md_pkey_t **ppkey, apr_pool_t *p, const char *fname)
{
    FILE *f;
    apr_status_t rv;
    md_pkey_t *pkey;
    
    pkey =  make_pkey(p);
    rv = md_util_fopen(&f, fname, "r");
    if (rv == APR_SUCCESS) {
        rv = APR_EINVAL;
        pkey->pkey = PEM_read_PrivateKey(f, NULL, NULL, NULL);
        if (pkey->pkey != NULL) {
            rv = APR_SUCCESS;
            apr_pool_cleanup_register(p, pkey, pkey_cleanup, apr_pool_cleanup_null);
        }
        fclose(f);
    }

    *ppkey = (APR_SUCCESS == rv)? pkey : NULL;
    return rv;
}

apr_status_t md_pkey_load_rsa(md_pkey_t **ppkey, apr_pool_t *p, const char *fname)
{
    apr_status_t rv;
    
    if ((rv = md_pkey_load(ppkey, p, fname)) == APR_SUCCESS) {
        if (EVP_PKEY_id((*ppkey)->pkey) != EVP_PKEY_RSA) {
            md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, 0, p, "key is not RSA: %s", fname); 
            md_pkey_free(*ppkey);
            *ppkey = NULL;
            rv = APR_EINVAL;
        }
    }
    return rv;
}

apr_status_t md_pkey_save(md_pkey_t *pkey, apr_pool_t *p, const char *fname)
{
    FILE *f;
    apr_status_t rv;
    
    rv = md_util_fopen(&f, fname, "w");
    if (rv == APR_SUCCESS) {
        rv = apr_file_perms_set(fname, MD_FPROT_F_UONLY);
        if (rv == APR_ENOTIMPL) {
            /* TODO: Windows, OS2 do not implement this. Do we have other
             * means to secure the file? */
            rv = APR_SUCCESS;
        }

        if (rv == APR_SUCCESS) {
            if (PEM_write_PrivateKey(f, pkey->pkey, NULL, NULL, 0, NULL, NULL) < 0) {
                md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, 0, p, "error writing key: %s", fname); 
                rv = APR_EGENERAL;
            }
        }
        
        fclose(f);
    }
    return rv;
}

apr_status_t md_pkey_gen_rsa(md_pkey_t **ppkey, apr_pool_t *p, int bits)
{
    EVP_PKEY_CTX *ctx = NULL;
    apr_status_t rv;
    
    *ppkey = make_pkey(p);
    ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (ctx 
        && EVP_PKEY_keygen_init(ctx) >= 0
        && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) >= 0
        && EVP_PKEY_keygen(ctx, &(*ppkey)->pkey) >= 0) {
        rv = APR_SUCCESS;
    }
    else {
        md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, 0, p, "unable to generate new key"); 
        *ppkey = NULL;
        rv = APR_EGENERAL;
    }
    
    if (ctx != NULL) {
        EVP_PKEY_CTX_free(ctx);
    }
    return rv;
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L

static void RSA_get0_key(const RSA *r,
                         const BIGNUM **n, const BIGNUM **e, const BIGNUM **d)
{
    if (n != NULL)
        *n = r->n;
    if (e != NULL)
        *e = r->e;
    if (d != NULL)
        *d = r->d;
}

#endif

static const char *bn64(const BIGNUM *b, apr_pool_t *p) 
{
    if (b) {
         int len = BN_num_bytes(b);
         char *buffer = apr_pcalloc(p, len);
         if (buffer) {
            BN_bn2bin(b, (unsigned char *)buffer);
            return md_util_base64url_encode(buffer, len, p);
         }
    }
    return NULL;
}

const char *md_pkey_get_rsa_e64(md_pkey_t *pkey, apr_pool_t *p)
{
    const BIGNUM *e;
    RSA *rsa = EVP_PKEY_get1_RSA(pkey->pkey);
    
    if (!rsa) {
        return NULL;
    }
    RSA_get0_key(rsa, NULL, &e, NULL);
    return bn64(e, p);
}

const char *md_pkey_get_rsa_n64(md_pkey_t *pkey, apr_pool_t *p)
{
    const BIGNUM *n;
    RSA *rsa = EVP_PKEY_get1_RSA(pkey->pkey);
    
    if (!rsa) {
        return NULL;
    }
    RSA_get0_key(rsa, &n, NULL, NULL);
    return bn64(n, p);
}

apr_status_t md_crypt_sign64(const char **psign64, md_pkey_t *pkey, apr_pool_t *p, 
                             const char *d, size_t dlen)
{
    EVP_MD_CTX *ctx = NULL;
    char *buffer;
    unsigned int blen;
    const char *sign64 = NULL;
    apr_status_t rv = APR_ENOMEM;
    
    buffer = apr_pcalloc(p, EVP_PKEY_size(pkey->pkey));
    if (buffer) {
        ctx = EVP_MD_CTX_create();
        if (ctx) {
            rv = APR_ENOTIMPL;
            if (EVP_SignInit_ex(ctx, EVP_sha256(), NULL)) {
                rv = APR_EGENERAL;
                if (EVP_SignUpdate(ctx, d, dlen)) {
                    if (EVP_SignFinal(ctx, (unsigned char*)buffer, &blen, pkey->pkey)) {
                        sign64 = md_util_base64url_encode(buffer, blen, p);
                        if (sign64) {
                            rv = APR_SUCCESS;
                        }
                    }
                }
            }
        }
        
        if (ctx) {
            EVP_MD_CTX_destroy(ctx);
        }
    }
    
    if (rv != APR_SUCCESS) {
        md_log_perror(MD_LOG_MARK, MD_LOG_WARNING, rv, p, "signing"); 
    }
    
    *psign64 = sign64;
    return rv;
}

/**************************************************************************************************/
/* certificates */

struct md_cert_t {
    apr_pool_t *pool;
    X509 *x509;
};

static apr_status_t cert_cleanup(void *data)
{
    md_cert_t *cert = data;
    if (cert->x509) {
        X509_free(cert->x509);
        cert->x509 = NULL;
    }
    return APR_SUCCESS;
}

static md_cert_t *make_cert(apr_pool_t *p, X509 *x509) 
{
    md_cert_t *cert = apr_pcalloc(p, sizeof(*cert));
    cert->pool = p;
    cert->x509 = x509;
    apr_pool_cleanup_register(p, cert, cert_cleanup, apr_pool_cleanup_null);
    
    return cert;
}

void md_cert_free(md_cert_t *cert)
{
    cert_cleanup(cert);
}

apr_status_t md_cert_load(md_cert_t **pcert, apr_pool_t *p, const char *fname)
{
    FILE *f;
    apr_status_t rv;
    md_cert_t *cert;
    X509 *x509;
    
    rv = md_util_fopen(&f, fname, "r");
    if (rv == APR_SUCCESS) {
    
        x509 = PEM_read_X509(f, NULL, NULL, NULL);
        rv = fclose(f);
        if (x509 != NULL) {
            cert =  make_cert(p, x509);
        }
        else {
            rv = APR_EINVAL;
        }
    }

    *pcert = (APR_SUCCESS == rv)? cert : NULL;
    return rv;
}


apr_status_t md_cert_save(md_cert_t *cert, apr_pool_t *p, const char *fname)
{
    FILE *f;
    apr_status_t rv;
    
    rv = md_util_fopen(&f, fname, "w");
    if (rv == APR_SUCCESS) {
        ERR_clear_error();
        
        PEM_write_X509(f, cert->x509);
        rv = fclose(f);
        
        if (ERR_get_error() > 0) {
            rv = APR_EINVAL;
        }
    }
    return rv;
}

md_cert_state_t md_cert_state_get(md_cert_t *cert)
{
    return cert->x509? MD_CERT_VALID : MD_CERT_UNKNOWN;
}

apr_status_t md_cert_load_chain(apr_array_header_t **pcerts, apr_pool_t *p, const char *fname)
{
    FILE *f;
    apr_status_t rv;
    apr_array_header_t *certs;
    X509 *x509;
    md_cert_t *cert, **pcert;
    unsigned long err;
    
    rv = md_util_fopen(&f, fname, "r");
    if (rv == APR_SUCCESS) {
        certs = apr_array_make(p, 5, sizeof(md_cert_t *));
        
        ERR_clear_error();
        while (NULL != (x509 = PEM_read_X509(f, NULL, NULL, NULL))) {
            
            cert = make_cert(p, x509);
            pcert = (md_cert_t **)apr_array_push(certs);
            *pcert = cert;
        }
        if (cert->x509 != NULL) {
            rv = APR_SUCCESS;
            apr_pool_cleanup_register(p, cert, cert_cleanup, apr_pool_cleanup_null);
        }
        rv = fclose(f);
        
        if (0 < (err =  ERR_get_error())
            && !(ERR_GET_LIB(err) == ERR_LIB_PEM && ERR_GET_REASON(err) == PEM_R_NO_START_LINE)) {
            /* not the expected one when no more PEM encodings are found */
            rv = APR_EINVAL;
        }
    }
    *pcerts = (APR_SUCCESS == rv)? certs : NULL;
    return rv;
}

apr_status_t md_cert_save_chain(apr_array_header_t *certs, apr_pool_t *p, const char *fname)
{
    FILE *f;
    apr_status_t rv;
    const md_cert_t *cert;
    unsigned long err = 0;
    int i;
    
    rv = md_util_fopen(&f, fname, "w");
    if (rv == APR_SUCCESS) {
        ERR_clear_error();
        for (i = 0; i < certs->nelts; ++i) {
            cert = APR_ARRAY_IDX(certs, i, const md_cert_t *);
            assert(cert->x509);
            
            PEM_write_X509(f, cert->x509);
            
            if (0 < (err = ERR_get_error())) {
                break;
            }
            
        }
        rv = fclose(f);
        if (err) {
            rv = APR_EINVAL;
        }
    }
    return rv;
}
