#include "pquv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// https://manpages.ubuntu.com/manpages/jammy/man3/pqt-specs.3.html

const int64_t madpostgres__Value_Boolean = 0;
const int64_t madpostgres__Value_Date = 1;
const int64_t madpostgres__Value_Float4 = 2;
const int64_t madpostgres__Value_Float8 = 3;
const int64_t madpostgres__Value_Int2 = 4;
const int64_t madpostgres__Value_Int4 = 5;
const int64_t madpostgres__Value_Int8 = 6;
const int64_t madpostgres__Value_Json = 7;
const int64_t madpostgres__Value_JsonB = 8;
const int64_t madpostgres__Value_Money = 9;
const int64_t madpostgres__Value_NotImplemented = 10;
const int64_t madpostgres__Value_Text = 11;
const int64_t madpostgres__Value_Timestamp = 12;
const int64_t madpostgres__Value_TimestampTz = 13;
const int64_t madpostgres__Value_VarChar = 14;

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