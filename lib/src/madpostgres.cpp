#include "gc.h"
#include "pquv.hpp"
#include "event-loop.hpp"
#include "apply-pap.hpp"
#include "list.hpp"
#include "date.hpp"
#include "number.hpp"
#include "catalog/pg_type_d.h"
#include "madpostgres.hpp"



#ifdef __cplusplus
extern "C" {
#endif

union int4Value {
  char bytes[4];
  int32_t num;
};

union int8Value {
  char bytes[8];
  int64_t num;
};

union float8Value {
  char bytes[8];
  double num;
};


typedef struct madpostgres__Callbacks {
  void *badCB;
  void *goodCB;
  pquv_t *connection;
} madpostgres__Callbacks_t;


void madpostgres__handleConnection(void *callbacks, pquv_t* connection) {
  madpostgres__Callbacks_t *typedCallbacks = (madpostgres__Callbacks_t*)callbacks;
  int err = pquv_get_error(connection);
  char *errMessage = pquv_get_errorMessage(connection);

  if (err > 0) {
    pquv_free(connection);
    __applyPAP__(typedCallbacks->badCB, 2, err, errMessage);
  } else {
    __applyPAP__(typedCallbacks->goodCB, 1, connection);
  }
}


void madpostgres__connect(char *connectionString, PAP_t *badCB, PAP_t *goodCB) {
  madpostgres__Callbacks_t *callbacks = (madpostgres__Callbacks_t*) GC_MALLOC(sizeof(madpostgres__Callbacks_t));
  callbacks->badCB = badCB;
  callbacks->goodCB = goodCB;
  callbacks->connection = NULL;
  pquv_init(connectionString, getLoop(), callbacks, madpostgres__handleConnection);
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

double ntoh_float8(char *input) {
  double rval;
  uint8_t *data = (uint8_t *)&rval;

  data[0] = input[7];
  data[1] = input[6];
  data[2] = input[5];
  data[3] = input[4];
  data[4] = input[3];
  data[5] = input[2];
  data[6] = input[1];
  data[7] = input[0];

  return rval;
}


madpostgres__Value_t *madpostgres__buildInt8Value(char *pqValue) {
  union int8Value num;
  memcpy(num.bytes, pqValue, 8);
  int64_t hostOrdered = ntoh64(&num.num);

  madpostgres__Value_t *res = (madpostgres__Value_t*) GC_MALLOC(sizeof(madpostgres__Value_t));
  res->index = madpostgres__Value_Integer;
  res->data1 = (void*)hostOrdered;
  return res;
}


madpostgres__Value_t *madpostgres__buildMoneyValue(char *pqValue) {
  union int8Value num;
  memcpy(num.bytes, pqValue, 8);
  int64_t hostOrdered = ntoh64(&num.num);

  madpostgres__Value_t *res = (madpostgres__Value_t*) GC_MALLOC(sizeof(madpostgres__Value_t));
  res->index = madpostgres__Value_Money;
  res->data1 = (void*) (hostOrdered / 100);
  res->data2 = (void*) (hostOrdered - ((hostOrdered / 100) * 100));
  return (madpostgres__Value_t*)res;
}


madpostgres__Value_t *madpostgres__buildTimestampValue(char *pqValue) {
  union int8Value num;
  memcpy(num.bytes, pqValue, 8);
  int64_t hostOrdered = (ntoh64(&num.num) + 946684800000000) / 1000;
  madpostgres__MadlibADT_t *dateTime = (madpostgres__MadlibADT_t*) GC_MALLOC(sizeof(madpostgres__MadlibADT_t));
  dateTime->index = 0;
  dateTime->data = (void*)hostOrdered;

  madpostgres__Value_t *res = (madpostgres__Value_t*) GC_MALLOC(sizeof(madpostgres__Value_t));
  res->index = madpostgres__Value_Timestamp;
  res->data1 = (void*)dateTime;
  return res;
}


madpostgres__Value_t *madpostgres__buildDateValue(char *pqValue) {
  union int4Value num;
  memcpy(num.bytes, pqValue, 4);

  int64_t hostOrdered = (int64_t) ntohl(num.num) * 24 * 60 * 60 * 1000 + 946684800000;

  madpostgres__MadlibADT_t *dateTime = (madpostgres__MadlibADT_t*) GC_MALLOC(sizeof(madpostgres__MadlibADT_t));
  dateTime->index = 0;
  dateTime->data = (void*)hostOrdered;

  madpostgres__Value_t *res = (madpostgres__Value_t*) GC_MALLOC(sizeof(madpostgres__Value_t));
  res->index = madpostgres__Value_Timestamp;
  res->data1 = (void*)dateTime;
  return res;
}


madpostgres__Value_t *madpostgres__buildFloat8Value(char *pqValue) {
  union float8Value num;
  memcpy(num.bytes, pqValue, 8);

  madpostgres__Value_t *res = (madpostgres__Value_t*) GC_MALLOC(sizeof(madpostgres__Value_t));
  res->index = madpostgres__Value_Float;
  res->data1 = (void*)boxDouble(ntoh_float8((char*)&num.num));
  return res;
}


madpostgres__Value_t *madpostgres__buildBooleanValue(char *pqValue) {
  madpostgres__Value_t *res = (madpostgres__Value_t*) GC_MALLOC(sizeof(madpostgres__Value_t));
  res->index = madpostgres__Value_Boolean;
  res->data1 = (void*) (*pqValue > 0 ? 1 : 0);
  return res;
}


madpostgres__Value_t *madpostgres__buildTextValue(char *pqValue) {
  size_t length = strlen(pqValue);
  char *copy = (char*)GC_MALLOC_ATOMIC(length + 1);
  memcpy(copy, pqValue, length);
  copy[length] = '\0';

  madpostgres__Value_t *res = (madpostgres__Value_t*) GC_MALLOC(sizeof(madpostgres__Value_t));
  res->index = madpostgres__Value_String;
  res->data1 = (void*)copy;
  return res;
}


madpostgres__Value_t *madpostgres__buildNotImplemented(char *pqValue) {
  madpostgres__Value_t *res = (madpostgres__Value_t*) GC_MALLOC(sizeof(madpostgres__Value_t));
  res->index = madpostgres__Value_NotImplemented;
  return res;
}


madpostgres__ValueParser *madpostgres__buildValueParserArray(int colCount, PGresult *res) {
  madpostgres__ValueParser *result = (madpostgres__ValueParser*)GC_MALLOC_ATOMIC(sizeof(madpostgres__ValueParser) * colCount);

  for (int i=0; i<colCount; i++) {
    switch(PQftype(res, i)) {
      case INT8OID:
        result[i] = madpostgres__buildInt8Value;
        break;

      case FLOAT8OID:
        result[i] = madpostgres__buildFloat8Value;
        break;

      case VARCHAROID:
        result[i] = madpostgres__buildTextValue;
        break;

      case TEXTOID:
        result[i] = madpostgres__buildTextValue;
        break;

      case TIMESTAMPOID:
        result[i] = madpostgres__buildTimestampValue;
        break;

      case TIMESTAMPTZOID:
        result[i] = madpostgres__buildTimestampValue;
        break;

      case DATEOID:
        result[i] = madpostgres__buildDateValue;
        break;

      case BOOLOID:
        result[i] = madpostgres__buildBooleanValue;
        break;

      case MONEYOID:
        result[i] = madpostgres__buildMoneyValue;
        break;

      default:
        result[i] = madpostgres__buildNotImplemented;
        break;
    }
  }

  return result;
}


void madpostgres__handleQueryResult(void *callbacks, PGresult* res) {
  madpostgres__Callbacks_t *typedCallbacks = (madpostgres__Callbacks_t*)callbacks;
  int err = pquv_get_error(typedCallbacks->connection);
  char *errMessage = pquv_get_errorMessage(typedCallbacks->connection);
  if (err > 0) {
    __applyPAP__(typedCallbacks->badCB, 2, err, errMessage);
    return;
  }

  int rowCount = PQntuples(res);
  int colCount = PQnfields(res);
  madpostgres__ValueParser *valueParsers = madpostgres__buildValueParserArray(colCount, res);


  madlib__list__Node_t *result = madlib__list__empty();

  for (int row = rowCount - 1; row >= 0; row--) {
    madlib__list__Node_t *rowValues = madlib__list__empty();

    for (int col = colCount - 1; col >= 0; col--) {
      char *pqValue = PQgetvalue(res, row, col);
      void *madlibValue = valueParsers[col](pqValue);
      rowValues = madlib__list__push(madlibValue, rowValues);
    }

    result = madlib__list__push(rowValues, result);
  }

  __applyPAP__(typedCallbacks->goodCB, 1, result);
}


void madpostgres__query(pquv_t *connection, char *query, PAP_t *badCB, PAP_t *goodCB) {
  int err = pquv_get_error(connection);
  if (err) {
    __applyPAP__(badCB, 1, err);
  } else {
    madpostgres__Callbacks_t *callbacks = (madpostgres__Callbacks_t*) GC_MALLOC(sizeof(madpostgres__Callbacks_t));
    callbacks->badCB = badCB;
    callbacks->goodCB = goodCB;
    callbacks->connection = connection;
    pquv_query(connection, query, madpostgres__handleQueryResult, (void*)callbacks);
  }
}


#ifdef __cplusplus
}
#endif
