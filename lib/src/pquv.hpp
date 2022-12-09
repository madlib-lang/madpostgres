#pragma once

#include "libpq-fe.h"
#include "uv.h"

struct pquv_st;
typedef struct pquv_st pquv_t;

/* its up to the receiver of the callback to call PQclear on `res` */
typedef void (*req_cb)(void* opaque, PGresult* res);
typedef void (*init_cb)(void* opaque, pquv_t* connection);


#define MAX_CONNINFO_LENGTH 1048
void pquv_init(const char* conninfo, uv_loop_t* loop, void *opaque, init_cb cb);
void pquv_free(pquv_t* pquv);

int pquv_get_error(pquv_t *connection);
char *pquv_get_errorMessage(pquv_t *connection);


#define MAX_QUERY_LENGTH 2048
#define MAX_NAME_LENGTH 512

/* pre-condition: the pointers contained in the `paramValues`
 * array are assumed to be valid until the call to `cb`,
 * the actual array however needs only to be valid until the
 * return of the corresponding function call.
 */

void pquv_query_params(
        pquv_t* pquv,
        const char* q,
        int nParams,
        const Oid* paramTypes,
        const char* const* paramValues,
        const int* paramLengths,
        const int* paramFormats,
        req_cb cb, void* opaque,
        uint32_t flags);

void pquv_prepare(
        pquv_t* pquv,
        const char* q,
        const char* name,
        int nParams,
        const Oid* paramTypes,
        req_cb cb, void* opaque,
        uint32_t flags);

void pquv_prepared(
        pquv_t* pquv,
        const char* name,
        int nParams,
        const char* const* paramValues,
        const int* paramLengths,
        const int* paramFormats,
        req_cb cb, void* opaque,
        uint32_t flags);

/* the query string given to `pquv_query_params` is guaranteed to be accessible
 * until the callback is called */
#define PQUV_NON_VOLATILE_QUERY_STRING 0x00000001
/* the name string given to `pquv_prepare` is guaranteed to be accessible
 * until the callback is called */
#define PQUV_NON_VOLATILE_NAME_STRING  0x00000002

static inline void pquv_query(
        pquv_t* pquv,
        const char* q,
        req_cb cb, void* opaque)
{
    pquv_query_params(pquv, q, 0, NULL, NULL, NULL, NULL, cb, opaque, 0);
}