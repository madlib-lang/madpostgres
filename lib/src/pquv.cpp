#include "pquv.hpp"

#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "gc.h"
#include "libpq-fe.h"
#include "pquvutils.hpp"
#include "uv.h"

static unsigned int statementIndex = 0;

enum pquv_req_kind_t {
  PQUV_NORMAL_STATEMENT = 0,
  PQUV_PREPARE_STATEMENT,
  PQUV_PREPARED_STATEMENT,
};

typedef struct req_ts {
  int kind;
  const char* q;
  const char* name;
  int nParams;
  const Oid* paramTypes;
  const char* const* paramValues;
  const int* paramLengths;
  const int* paramFormats;
  uint32_t flags;
  void* opaque;
  req_cb cb;
  struct req_ts* next;
} req_t;

typedef struct {
  req_t* head;
  req_t* tail;
} queue_t;

enum pquv_state_t {
  PQUV_NEW = 0,
  PQUV_CONNECTING,
  PQUV_RESETTING,
  PQUV_CONNECTED,
  PQUV_BAD_CONNECTION,
  PQUV_BAD_RESET,
};

enum pquv_error_t {
  PQUV_ERROR_NONE = 0,
  PQUV_ERROR_BAD_CONNECTION,
  PQUV_ERROR_BAD_QUERY,
};

struct pquv_st {
  uv_loop_t* loop;
  uv_poll_t poll;
  uv_timer_t reconnect_timer;
  int reconnect_timer_ms;
  enum pquv_state_t state;
  char* conninfo;
  PGconn* conn;
  req_t* live;
  queue_t queue;
  int fd;
  int eventmask;
  enum pquv_error_t err;
  char* errMessage;
  init_cb connectionCB;
  void* connectionOpaque;
  bool alreadyDisconnected;
};

static req_t* dequeue(queue_t* queue) {
  if (queue->head == NULL) return NULL;

  req_t* t = queue->head;
  queue->head = t->next;
  t->next = NULL;
  return t;
}

static void poll_cb(uv_poll_t* handle, int status, int events);

void setError(pquv_t* pquv, pquv_error_t err) {
  char* errMessage = PQerrorMessage(pquv->conn);
  size_t errMessageLength = strlen(errMessage);
  pquv->err = err;
  pquv->errMessage = (char*)GC_MALLOC_ATOMIC(errMessageLength + 1);
  memcpy(pquv->errMessage, errMessage, errMessageLength + 1);
  pquv->errMessage[errMessageLength] = '\0';
}

static void update_poll_eventmask(pquv_t* pquv, int eventmask) {
  if (pquv->eventmask != eventmask) {
    int r = uv_poll_start(&pquv->poll, eventmask, poll_cb);
    if (r != 0) {
      // failwith("uv_poll_start: %s\n", uv_strerror(r));
    }
    pquv->eventmask = eventmask;
  }
}

static bool maybe_send_req(pquv_t* pquv) {
  if (pquv->live != NULL) {
    return false;
  }

  if (PQstatus(pquv->conn) == CONNECTION_BAD) {
    // TODO: reset connection
    NULL;  // failwith("connection is bad");
  }

  req_t* r = dequeue(&pquv->queue);
  if (r == NULL) {
    return false;
  }

  switch (r->kind) {
    case PQUV_NORMAL_STATEMENT: {
      // char *statementName = (char *) GC_MALLOC(64);
      // sprintf(statementName, "%d", statementIndex);
      // statementIndex += 1;
      // PGresult *res = PQprepare(pquv->conn, statementName, r->q, 0, NULL);

      // if (PQresultStatus(res) > 6) {
      //   char *resultErrorMessage = PQresultErrorMessage(res);
      //   r->cb(r->opaque, res);
      //   setError(pquv, PQUV_ERROR_BAD_QUERY);
      //   return false;
      // } else {
      //   PQsendQueryPrepared(pquv->conn, statementName, 0, NULL, NULL, NULL, 1);
      // }
      // TODO: verify that this is correct, but since we don't use the params
      // at the moment it should be fine
      // if (!PQsendQuery(pquv->conn, r->q)) {
      if (!PQsendQueryParams(pquv->conn, r->q, r->nParams, r->paramTypes, r->paramValues, r->paramLengths,
                             r->paramFormats, 1)) {
        NULL;  // failwith("PQsendQuery: %s\n", PQerrorMessage(pquv->conn));
      }
      break;
    }
    case PQUV_PREPARE_STATEMENT:
      if (!PQsendPrepare(pquv->conn, r->name, r->q, r->nParams, r->paramTypes)) {
        NULL;  // failwith("PQSendPerpare: %s\n", PQerrorMessage(pquv->conn));
      }
      break;
    case PQUV_PREPARED_STATEMENT:
      if (!PQsendQueryPrepared(pquv->conn, r->name, r->nParams, r->paramValues, r->paramLengths, r->paramFormats, 1)) {
        NULL;  // failwith("PQsendQueryPrepared: %s\n", PQerrorMessage(pquv->conn));
      }
      break;
  }

  pquv->live = r;
  return true;
}

static void enqueue_req(pquv_t* pquv, enum pquv_req_kind_t kind, const char* q, const char* name, int nParams,
                        const Oid* paramTypes, const char* const* paramValues, const int* paramLengths,
                        const int* paramFormats, req_cb cb, void* opaque, uint32_t flags) {
  req_t* r = (req_t*)GC_MALLOC_UNCOLLECTABLE(sizeof(*r));
  r->flags = flags;
  r->kind = kind;
  r->nParams = nParams;

  if (flags & PQUV_NON_VOLATILE_QUERY_STRING) {
    r->q = q;
  } else {
    r->q = q ? strndup(q, MAX_QUERY_LENGTH) : NULL;
  }

  if (flags & PQUV_NON_VOLATILE_NAME_STRING) {
    r->name = name;
  } else {
    r->name = name ? strndup(name, MAX_NAME_LENGTH) : NULL;
  }

  if (paramTypes != NULL && nParams) {
    size_t n = sizeof(const char*) * nParams;
    r->paramTypes = (Oid*)GC_MALLOC_UNCOLLECTABLE(n);
    memcpy((void*)r->paramTypes, paramTypes, n);
  } else {
    r->paramTypes = NULL;
  }

  if (paramValues != NULL && nParams) {
    size_t n = sizeof(const char*) * nParams;
    r->paramValues = (const char* const*)GC_MALLOC_UNCOLLECTABLE(n);
    memcpy((void*)r->paramValues, paramValues, n);
  } else {
    r->paramValues = NULL;
  }

  if (paramLengths != NULL && nParams) {
    size_t n = sizeof(const int*) * nParams;
    r->paramLengths = (const int*)GC_MALLOC_UNCOLLECTABLE(n);
    memcpy((void*)r->paramLengths, paramLengths, n);
  } else {
    r->paramLengths = NULL;
  }

  if (paramFormats != NULL && nParams) {
    size_t n = sizeof(const int*) * nParams;
    r->paramFormats = (const int*)GC_MALLOC_UNCOLLECTABLE(n);
    memcpy((void*)r->paramFormats, paramFormats, n);
  } else {
    r->paramFormats = NULL;
  }

  r->cb = cb;
  r->opaque = opaque;
  r->next = NULL;

  if (pquv->queue.head == NULL) {
    pquv->queue.head = r;
    pquv->queue.tail = r;
  } else {
    pquv->queue.tail->next = r;
    pquv->queue.tail = r;
  }

  if (pquv->state == PQUV_CONNECTED && pquv->live == NULL) {
    update_poll_eventmask(pquv, pquv->eventmask | UV_WRITABLE);
  }
}

void pquv_query_params(pquv_t* pquv, const char* q, int nParams, const Oid* paramTypes, const char* const* paramValues,
                       const int* paramLengths, const int* paramFormats, req_cb cb, void* opaque, uint32_t flags) {
  enqueue_req(pquv, PQUV_NORMAL_STATEMENT, q, NULL, nParams, paramTypes, paramValues, paramLengths, paramFormats, cb,
              opaque, flags);
}

void pquv_prepare(pquv_t* pquv, const char* q, const char* name, int nParams, const Oid* paramTypes, req_cb cb,
                  void* opaque, uint32_t flags) {
  enqueue_req(pquv, PQUV_PREPARE_STATEMENT, q, name, nParams, paramTypes, NULL, NULL, NULL, cb, opaque, flags);
}

void pquv_prepared(pquv_t* pquv, const char* name, int nParams, const char* const* paramValues, const int* paramLengths,
                   const int* paramFormats, req_cb cb, void* opaque, uint32_t flags) {
  enqueue_req(pquv, PQUV_PREPARED_STATEMENT, NULL, name, nParams, NULL, paramValues, paramLengths, paramFormats, cb,
              opaque, flags);
}

static void free_req(req_t* r) {
  if (!(r->flags & PQUV_NON_VOLATILE_NAME_STRING)) GC_FREE((void*)r->name);

  GC_FREE((void*)r->paramTypes);
  GC_FREE((void*)r->paramValues);
  GC_FREE((void*)r->paramLengths);
  GC_FREE((void*)r->paramFormats);
  GC_FREE(r);
}

static void poll_cb(uv_poll_t* handle, int status, int events) {
  pquv_t* pquv = container_of(handle, pquv_t, poll);
  int eventmask = pquv->eventmask;

  if (status < 0) {
    NULL;  // failwith("unexpected status %d\n", status);
  }

  if (events & UV_WRITABLE) {
    int r = PQflush(pquv->conn);
    if (r == 0) {
      if (maybe_send_req(pquv)) {
        r = PQflush(pquv->conn);
        if (r == 0) {
          eventmask &= ~UV_WRITABLE;
        }
      } else {
        eventmask &= ~UV_WRITABLE;
      }
    }

    if (r == -1) {
      NULL;  // failwith("PQflush failed\n");
    } else if (r == 1) {
      eventmask |= UV_READABLE | UV_WRITABLE;
    } else if (r != 0) {
      NULL;  // failwith("PQflush unexpected return code: %d\n", r);
    }
  }

  if (events & UV_READABLE) {
    if (!PQconsumeInput(pquv->conn)) {
      if (pquv->err == 0 || strcmp(pquv->errMessage, "") == 0) {
        setError(pquv, PQUV_ERROR_BAD_CONNECTION);
      }

      eventmask = UV_DISCONNECT;
    }

    if (!PQisBusy(pquv->conn)) {
      if (pquv->live == NULL) {
        // failwith("received orphan result\n");
      }
    }

    PGresult* r = PQgetResult(pquv->conn);

    int res = PQresultStatus(r);
    if (res != PGRES_TUPLES_OK && res != PGRES_COMMAND_OK && res != PGRES_EMPTY_QUERY) {
      setError(pquv, PQUV_ERROR_BAD_QUERY);
    }

    if (pquv->live != NULL) {
      pquv->live->cb(pquv->live->opaque, r);
      free_req(pquv->live);
      pquv->live = NULL;
    }

    /* TODO: handle more results */
    if (PQgetResult(pquv->conn) != NULL) {
      //failwith("bad con");
    }

    /* if we have enqueued reqs, wait for writeable state */
    if (pquv->queue.head != NULL) eventmask |= UV_WRITABLE;
  } else {
    /* noop */
  }
  update_poll_eventmask(pquv, eventmask);
}


/* Non-blocking connection request
 *
 * Quoting from: https://www.postgresql.org/docs/9.6/static/libpq-connect.html
 *
 * To begin a nonblocking connection request,
 * call conn = PQconnectStart("connection_info_string"). If conn is null, then
 * libpq has been unable to allocate a new PGconn structure. Otherwise, a valid
 * PGconn pointer is returned (though not yet representing a valid connection
 * to the database). On return from PQconnectStart, call status =
 * PQstatus(conn). If status equals CONNECTION_BAD, PQconnectStart has failed.
 *
 * If PQconnectStart succeeds, the next stage is to poll libpq so that it can
 * proceed with the connection sequence. Use PQsocket(conn) to obtain the
 * descriptor of the socket underlying the database connection. Loop thus: If
 * PQconnectPoll(conn) last returned PGRES_POLLING_READING, wait until the
 * socket is ready to read (as indicated by select(), poll(), or similar system
 * function). Then call PQconnectPoll(conn) again. Conversely, if
 * PQconnectPoll(conn) last returned PGRES_POLLING_WRITING, wait until the
 * socket is ready to write, then call PQconnectPoll(conn) again. If you have
 * yet to call PQconnectPoll, i.e., just after the call to PQconnectStart,
 * behave as if it last returned PGRES_POLLING_WRITING. Continue this loop
 * until PQconnectPoll(conn) returns PGRES_POLLING_FAILED, indicating the
 * connection procedure has failed, or PGRES_POLLING_OK, indicating the
 * connection has been successfully made.
 */

static void poll_connection(pquv_t* pquv);

static void connection_cb(uv_poll_t* handle, int status, int events) {
  if (status < 0) {
    NULL;  // failwith("unexpected status %d\n", status);
  }
  if ((events & ~(UV_READABLE | UV_WRITABLE)) != 0) {
    NULL;  // failwith("received unexpcted event: %d\n", events);
  }
  pquv_t* pquv = container_of(handle, pquv_t, poll);
  poll_connection(pquv);
}

static void start_connection(pquv_t* pquv);

static void start_connection_close_poll_cb(uv_handle_t* h) {
  pquv_t* pquv = container_of(h, pquv_t, poll);

  if (0 != close(pquv->fd)) {
    NULL;  // failwith("unable to close fd %d\n", pquv->fd);
  }

  PQfinish(pquv->conn);
  pquv->fd = -1;
  pquv->state = PQUV_NEW;

  start_connection(pquv);
}


static void defaultNoticeProcessor(void *arg, const char *message) {
  // Do nothing
  // fprintf(stderr, "%s", message);
}


static void start_connection(pquv_t* pquv) {
  if (pquv->fd >= 0) {
    if (uv_poll_stop(&pquv->poll) != 0) {
      pquv->err = PQUV_ERROR_BAD_CONNECTION;
      if (pquv->err == 0 || strcmp(pquv->errMessage, "") == 0) {
        setError(pquv, PQUV_ERROR_BAD_CONNECTION);
      }
    }
    uv_close((uv_handle_t*)&pquv->poll, start_connection_close_poll_cb);
    return;
  }

  pquv->conn = PQconnectStart(pquv->conninfo);
  if (pquv->conn == NULL) {
    setError(pquv, PQUV_ERROR_BAD_CONNECTION);
  } else {
    PQsetNoticeProcessor(pquv->conn, defaultNoticeProcessor, NULL);
  }


  if (PQstatus(pquv->conn) == CONNECTION_BAD) {
    setError(pquv, PQUV_ERROR_BAD_CONNECTION);
  }

  int fd = PQsocket(pquv->conn);
  if (fd < 0) {
    setError(pquv, PQUV_ERROR_BAD_CONNECTION);
  }

  /*
   * From libuv documentation:
   * "The user should not close a file descriptor while it is being polled
   * by an active poll handle."
   * (http://docs.libuv.org/en/v1.x/poll.html)
   *
   * So: dup the fd so we can close it after we've called uv_poll_close
   *
   * From libpq documentation:
   * "On Unix, forking a process with open libpq connections can lead to
   * unpredictable results because the parent and child processes share the
   * same sockets and operating system resources.
   * (https://www.postgresql.org/docs/9.6/static/libpq-connect.html)
   *
   * So: make sure FD_CLOEXEC is set
   *
   * Note: this is also done internally in libpq, see:
   * https://github.com/postgres/postgres/blob/0ba99c84e8c7138143059b281063d4cca5a2bfea/src/interfaces/libpq/fe-connect.c#L2140
   */

  pquv->fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
  if (pquv->fd < 0) {
    setError(pquv, PQUV_ERROR_BAD_CONNECTION);
  }

  int r;
  if ((r = uv_poll_init(pquv->loop, &pquv->poll, pquv->fd)) != 0) {
    setError(pquv, PQUV_ERROR_BAD_CONNECTION);
  }

  pquv->state = PQUV_CONNECTING;

  poll_connection(pquv);
}

static void reconnect_timer_cb(uv_timer_t* h) {
  pquv_t* pquv = container_of(h, pquv_t, reconnect_timer);

  switch (pquv->state) {
    case PQUV_BAD_RESET:
      if (0 != PQresetStart(pquv->conn)) {
        NULL;  // failwith("PQresetStart failed");
      }
      pquv->state = PQUV_RESETTING;
      poll_connection(pquv);
      break;
    case PQUV_BAD_CONNECTION:
      start_connection(pquv);
      break;
    default:
      NULL;  // failwith("unexpected state: %d\n", pquv->state);
  }
}

static void poll_connection(pquv_t* pquv) {
  int events;
  uv_poll_cb cb;

  int r;

  switch (pquv->state) {
    case PQUV_CONNECTING:
      r = PQconnectPoll(pquv->conn);
      break;
    case PQUV_RESETTING:
      r = PQresetPoll(pquv->conn);
      break;
    default:
      if (pquv->err == 0 || strcmp(pquv->errMessage, "") == 0) {
        setError(pquv, PQUV_ERROR_BAD_CONNECTION);
      }
      pquv->err = PQUV_ERROR_BAD_CONNECTION;
  }

  switch (r) {
    case PGRES_POLLING_READING:
      events = UV_READABLE;
      cb = connection_cb;
      break;
    case PGRES_POLLING_WRITING:
      events = UV_WRITABLE;
      cb = connection_cb;
      break;
    case PGRES_POLLING_OK:
      pquv->state = PQUV_CONNECTED;
      pquv->eventmask = events = UV_WRITABLE | UV_READABLE;
      pquv->connectionCB(pquv->connectionOpaque, pquv);
      cb = poll_cb;
      break;
    case PGRES_POLLING_FAILED:
      switch (pquv->state) {
        case PQUV_CONNECTING: {
          // setError(pquv, PQUV_ERROR_BAD_CONNECTION);
          // char* errMessage = PQerrorMessage(pquv->conn);
          // size_t errMessageLength = strlen(errMessage);
          // printf("OH MSG 11: %s\n", PQerrorMessage(pquv->conn));
          // pquv->err = PQUV_ERROR_BAD_CONNECTION;
          // pquv->errMessage = (char*)GC_MALLOC_ATOMIC(errMessageLength + 1);
          // memcpy(pquv->errMessage, errMessage, errMessageLength + 1);
          // pquv->errMessage[errMessageLength] = '\0';
          pquv->connectionCB(pquv->connectionOpaque, pquv);
          return;
        }
        case PQUV_RESETTING:
          pquv->state = PQUV_BAD_RESET;
          break;
        default:
          NULL;  // failwith("unexpected state: %d\n", pquv->state);
      }

      r = uv_timer_start(&pquv->reconnect_timer, reconnect_timer_cb, pquv->reconnect_timer_ms, 0);
      if (r != 0) {
        NULL;  // failwith("uv_timer_start: %s\n", uv_strerror(r));
      }
      return;
    default:
      setError(pquv, PQUV_ERROR_BAD_CONNECTION);
      pquv->connectionCB(pquv->connectionOpaque, pquv);
      return;
  }

  if ((r = uv_poll_start(&pquv->poll, events, cb)) != 0) {
    NULL;  // failwith("uv_poll_start: %s\n", uv_strerror(r));
  }
}

void pquv_init(const char* conninfo, uv_loop_t* loop, void* opaque, init_cb cb) {
  pquv_t* pquv = (pquv_t*)GC_MALLOC(sizeof(*pquv));

  pquv->loop = loop;
  pquv->conninfo = strndup(conninfo, MAX_CONNINFO_LENGTH);
  pquv->queue.head = NULL;
  pquv->queue.tail = NULL;
  pquv->reconnect_timer_ms = 1000;
  pquv->state = PQUV_NEW;
  pquv->fd = -1;
  pquv->eventmask = 0;
  pquv->live = NULL;
  pquv->err = PQUV_ERROR_NONE;
  pquv->errMessage = (char*)"";
  pquv->connectionCB = cb;
  pquv->connectionOpaque = opaque;
  pquv->alreadyDisconnected = false;

  int r;
  if ((r = uv_timer_init(loop, &pquv->reconnect_timer)) != 0) {
    setError(pquv, PQUV_ERROR_BAD_CONNECTION);
  }

  start_connection(pquv);
}

static void pquv_free_close_poll_cb(uv_handle_t* h) {
  pquv_t* pquv = container_of(h, pquv_t, poll);

  if (close(pquv->fd) != 0) {
    NULL;  // failwith("unable to close fd %d: %s\n", pquv->fd, strerror(errno));
  }

  PQfinish(pquv->conn);

  if (pquv->live != NULL) free_req(pquv->live);

  req_t* r = pquv->queue.head;
  while (r != NULL) {
    req_t* n = r->next;
    free_req(r);
    r = n;
  }

  // GC_FREE(pquv);
}

static void pquv_close_timer_cb(uv_handle_t* h) {
  pquv_t* pquv = container_of(h, pquv_t, reconnect_timer);

  //   if (pquv && pquv->poll.u.fd != 0) {
  if (uv_is_active((uv_handle_t*)&pquv->poll)) {
    int r;
    if ((r = uv_poll_stop(&pquv->poll)) != 0) {
      NULL;  // failwith("uv_poll_stop: %s\n", uv_strerror(r));
    }
    uv_close((uv_handle_t*)&pquv->poll, pquv_free_close_poll_cb);
  } else {
    // GC_FREE(pquv);
  }
}

void pquv_free(pquv_t* pquv) {
  pquv->alreadyDisconnected = true;
  uv_close((uv_handle_t*)&pquv->reconnect_timer, pquv_close_timer_cb);
}

int pquv_get_error(pquv_t* connection) {
  if (PQstatus(connection->conn) == CONNECTION_BAD) {
    setError(connection, PQUV_ERROR_BAD_CONNECTION);
    return PQUV_ERROR_BAD_CONNECTION;
  }
  return connection->err;
}

bool pquv_get_disconnected(pquv_t* connection) { return connection->alreadyDisconnected; }

char* pquv_get_errorMessage(pquv_t* connection) { return connection->errMessage; }
