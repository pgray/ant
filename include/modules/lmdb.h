#ifndef ANT_LMDB_MODULE_H
#define ANT_LMDB_MODULE_H

#include "types.h"

typedef struct lmdb_env_handle lmdb_env_handle_t;
typedef struct lmdb_db_handle lmdb_db_handle_t;
typedef struct lmdb_txn_handle lmdb_txn_handle_t;

typedef struct lmdb_env_ref lmdb_env_ref_t;
typedef struct lmdb_db_ref lmdb_db_ref_t;
typedef struct lmdb_txn_ref lmdb_txn_ref_t;

ant_value_t lmdb_library(ant_t *js);
void cleanup_lmdb_module(ant_t *js);

#endif
