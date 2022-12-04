#include "gc.h"
#include "pquv.hpp"
#include "event-loop.hpp"
#include "apply-pap.hpp"


#ifdef __cplusplus
extern "C" {
#endif

union number {
  char chars[8];
  int64_t longNum;
};

pquv_t *madpostgres__connect(char *connectionString) {
  return pquv_init(connectionString, getLoop());
}

void madpostgres__disconnect(pquv_t *connection) {
  pquv_free(connection);
}


int64_t ntoh64(int64_t *input) {
  uint64_t rval;
  uint8_t *data = (uint8_t *)&rval;

  data[0] = *input >> 56;
  data[1] = *input >> 48;
  data[2] = *input >> 40;
  data[3] = *input >> 32;
  data[4] = *input >> 24;
  data[5] = *input >> 16;
  data[6] = *input >> 8;
  data[7] = *input >> 0;

  return rval;
}

void handleQueryResult(void *callback, PGresult* res) {
  union number num;
  char *first = PQgetvalue(res, 0 /* row */, 0 /* col */);
//   num.chars = (char[8])first;
  memcpy(num.chars, first, 8);

  int64_t firstI = ntoh64(&num.longNum);
  size_t firstLength = PQgetlength(res, 0, 0);
  printf("%ld\n", num.longNum);
  printf("%ld\n", firstI);
  char *firstCopy = (char*)GC_MALLOC_ATOMIC(firstLength + 1);
  memcpy(firstCopy, first, firstLength);
  firstCopy[firstLength] = '\0';
  __applyPAP__(callback, 1, firstCopy);
}

void madpostgres__query(pquv_t *connection, char *query, PAP_t *callback) {
  pquv_query(connection, query, handleQueryResult, callback);
}

#ifdef __cplusplus
}
#endif
