/*
 * Copyright 2013 10gen Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef BSON_CONTEXT_PRIVATE_H
#define BSON_CONTEXT_PRIVATE_H


#include "config.h"
#include "bson-context.h"
#include "bson-thread.h"


BSON_BEGIN_DECLS


struct _bson_context_t
{
   bson_context_flags_t   flags : 7;
   bson_bool_t            pidbe_once : 1;
   bson_uint8_t           pidbe[2];
   bson_uint8_t           md5[3];
   bson_uint32_t          seq32;
   bson_uint64_t          seq64;
#if defined WITH_OID32_PT
   bson_mutex_t           _m32;
#endif
#if defined WITH_OID64_PT
   bson_mutex_t           _m64;
#endif

   void (*oid_get_host)  (bson_context_t *context,
                          bson_oid_t     *oid);
   void (*oid_get_pid)   (bson_context_t *context,
                          bson_oid_t     *oid);
   void (*oid_get_seq32) (bson_context_t *context,
                          bson_oid_t     *oid);
   void (*oid_get_seq64) (bson_context_t *context,
                          bson_oid_t     *oid);
};


BSON_END_DECLS


#endif /* BSON_CONTEXT_PRIVATE_H */
