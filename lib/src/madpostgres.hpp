#include "pquv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// https://manpages.ubuntu.com/manpages/jammy/man3/pqt-specs.3.html

const int64_t madpostgres__Value_Boolean = 0;
const int64_t madpostgres__Value_Float = 1;
const int64_t madpostgres__Value_Integer = 2;
const int64_t madpostgres__Value_Money = 3;
const int64_t madpostgres__Value_NotImplemented = 4;
const int64_t madpostgres__Value_String = 5;
const int64_t madpostgres__Value_Timestamp = 6;

typedef struct madpostgres__MadlibADT {
  int64_t index;
  void *data;
} madpostgres__MadlibADT_t;

typedef struct madpostgres__Value {
  int64_t index;
  void *data1;
  void *data2;
} madpostgres__Value_t;

typedef madpostgres__Value_t* (*madpostgres__ValueParser)(char* pqValue);

void madpostgres__connect(char *connectionString, PAP_t *badCB, PAP_t *goodCB);
void madpostgres__disconnect(pquv_t *connection);
void madpostgres__query(pquv_t *connection, char *query, PAP_t *badCB, PAP_t *goodCB);

#ifdef __cplusplus
}
#endif