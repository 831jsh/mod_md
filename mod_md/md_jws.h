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

#ifndef mod_md_md_jws_h
#define mod_md_md_jws_h

struct apr_table_t;
struct md_json;
struct md_pkey;

apr_status_t md_jws_sign(md_json **pmsg, apr_pool_t *p,
                         const char *payload, size_t len, struct apr_table_t *protected, 
                         struct md_pkey *pkey, const char *key_id);

#endif /* md_jws_h */
