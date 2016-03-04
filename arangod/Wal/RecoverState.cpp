////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#include "RecoverState.h"
#include "Basics/FileUtils.h"
#include "Basics/conversions.h"
#include "Basics/files.h"
#include "Basics/Exceptions.h"
#include "Basics/memory-map.h"
#include "Basics/tri-strings.h"
#include "Basics/VelocyPackHelper.h"
#include "Utils/SingleCollectionTransaction.h"
#include "Utils/StandaloneTransactionContext.h"
#include "VocBase/collection.h"
#include "VocBase/DatafileHelper.h"
#include "Wal/LogfileManager.h"
#include "Wal/Slots.h"

#include <velocypack/Collection.h>
#include <velocypack/Parser.h>
#include <velocypack/velocypack-aliases.h>

using namespace arangodb;
using namespace arangodb::wal;

////////////////////////////////////////////////////////////////////////////////
/// @brief convert a number slice into its numeric equivalent
////////////////////////////////////////////////////////////////////////////////

template <typename T>
static inline T NumericValue(VPackSlice const& slice, char const* attribute) {
  if (!slice.isObject()) {
    LOG(ERR) << "invalid value type when looking for attribute '" << attribute << "': expecting object";
    THROW_ARANGO_EXCEPTION(TRI_ERROR_INTERNAL);
  }
  VPackSlice v = slice.get(attribute);
  if (v.isString()) {
    return static_cast<T>(std::stoull(v.copyString()));
  }
  if (v.isNumber()) {
    return v.getNumber<T>();
  }
  
  LOG(ERR) << "invalid value for attribute '" << attribute << "'";
  THROW_ARANGO_EXCEPTION(TRI_ERROR_INTERNAL);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief whether or not a collection is volatile
////////////////////////////////////////////////////////////////////////////////

static inline bool IsVolatile(
    TRI_transaction_collection_t const* trxCollection) {
  return trxCollection->_collection->_collection->_info.isVolatile();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the directory for a database
////////////////////////////////////////////////////////////////////////////////

static std::string GetDatabaseDirectory(TRI_server_t* server,
                                        TRI_voc_tick_t databaseId) {
  char* idString = TRI_StringUInt64(databaseId);
  char* dname = TRI_Concatenate2String("database-", idString);
  TRI_FreeString(TRI_CORE_MEM_ZONE, idString);
  char* filename = TRI_Concatenate2File(server->_databasePath, dname);
  TRI_FreeString(TRI_CORE_MEM_ZONE, dname);

  std::string result(filename);
  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief wait until a database directory disappears
////////////////////////////////////////////////////////////////////////////////

static int WaitForDeletion(TRI_server_t* server, TRI_voc_tick_t databaseId,
                           int statusCode) {
  std::string const result = GetDatabaseDirectory(server, databaseId);

  int iterations = 0;
  // wait for at most 30 seconds for the directory to be removed
  while (TRI_IsDirectory(result.c_str())) {
    if (iterations == 0) {
      LOG(TRACE) << "waiting for deletion of database directory '" << result << "', called with status code " << statusCode;

      if (statusCode != TRI_ERROR_FORBIDDEN &&
          (statusCode == TRI_ERROR_ARANGO_DATABASE_NOT_FOUND ||
           statusCode != TRI_ERROR_NO_ERROR)) {
        LOG(WARN) << "forcefully deleting database directory '" << result << "'";
        TRI_RemoveDirectory(result.c_str());
      }
    } else if (iterations >= 30 * 10) {
      LOG(WARN) << "unable to remove database directory '" << result << "'";
      return TRI_ERROR_INTERNAL;
    }

    if (iterations == 5 * 10) {
      LOG(INFO) << "waiting for deletion of database directory '" << result << "'";
    }

    ++iterations;
    usleep(100000);
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates the recover state
////////////////////////////////////////////////////////////////////////////////

RecoverState::RecoverState(TRI_server_t* server, bool ignoreRecoveryErrors)
    : server(server),
      failedTransactions(),
      lastTick(0),
      logfilesToProcess(),
      openedCollections(),
      openedDatabases(),
      emptyLogfiles(),
      policy(TRI_DOC_UPDATE_ONLY_IF_NEWER, 0, nullptr),
      ignoreRecoveryErrors(ignoreRecoveryErrors),
      errorCount(0),
      lastDatabaseId(0),
      lastCollectionId(0) {}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys the recover state
////////////////////////////////////////////////////////////////////////////////

RecoverState::~RecoverState() { releaseResources(); }

////////////////////////////////////////////////////////////////////////////////
/// @brief release opened collections and databases so they can be shut down
/// etc.
////////////////////////////////////////////////////////////////////////////////

void RecoverState::releaseResources() {
  // release all collections
  for (auto it = openedCollections.begin(); it != openedCollections.end();
       ++it) {
    TRI_vocbase_col_t* collection = (*it).second;
    TRI_ReleaseCollectionVocBase(collection->_vocbase, collection);
  }

  openedCollections.clear();

  // release all databases
  for (auto it = openedDatabases.begin(); it != openedDatabases.end(); ++it) {
    TRI_vocbase_t* vocbase = (*it).second;
    TRI_ReleaseDatabaseServer(server, vocbase);
  }

  openedDatabases.clear();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief gets a database (and inserts it into the cache if not in it)
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_t* RecoverState::useDatabase(TRI_voc_tick_t databaseId) {
  auto it = openedDatabases.find(databaseId);

  if (it != openedDatabases.end()) {
    return (*it).second;
  }

  TRI_vocbase_t* vocbase = TRI_UseDatabaseByIdServer(server, databaseId);

  if (vocbase == nullptr) {
    return nullptr;
  }

  openedDatabases.emplace(databaseId, vocbase);
  return vocbase;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief release a database (so it can be dropped)
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_t* RecoverState::releaseDatabase(TRI_voc_tick_t databaseId) {
  auto it = openedDatabases.find(databaseId);

  if (it == openedDatabases.end()) {
    return nullptr;
  }

  TRI_vocbase_t* vocbase = (*it).second;

  TRI_ASSERT(vocbase != nullptr);

  // release all collections we ourselves have opened for this database
  auto it2 = openedCollections.begin();

  while (it2 != openedCollections.end()) {
    TRI_vocbase_col_t* collection = (*it2).second;

    TRI_ASSERT(collection != nullptr);

    if (collection->_vocbase->_id == databaseId) {
      // correct database, now release the collection
      TRI_ASSERT(vocbase == collection->_vocbase);

      TRI_ReleaseCollectionVocBase(vocbase, collection);
      // get new iterator position
      it2 = openedCollections.erase(it2);
    } else {
      // collection not found, advance in the loop
      ++it2;
    }
  }

  TRI_ReleaseDatabaseServer(server, vocbase);
  openedDatabases.erase(databaseId);

  return vocbase;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief release a collection (so it can be dropped)
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* RecoverState::releaseCollection(TRI_voc_cid_t collectionId) {
  auto it = openedCollections.find(collectionId);

  if (it == openedCollections.end()) {
    return nullptr;
  }

  TRI_vocbase_col_t* collection = (*it).second;

  TRI_ASSERT(collection != nullptr);
  TRI_ReleaseCollectionVocBase(collection->_vocbase, collection);
  openedCollections.erase(collectionId);

  return collection;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief gets a collection (and inserts it into the cache if not in it)
////////////////////////////////////////////////////////////////////////////////

TRI_vocbase_col_t* RecoverState::useCollection(TRI_vocbase_t* vocbase,
                                               TRI_voc_cid_t collectionId,
                                               int& res) {
  auto it = openedCollections.find(collectionId);

  if (it != openedCollections.end()) {
    res = TRI_ERROR_NO_ERROR;
    return (*it).second;
  }

  TRI_set_errno(TRI_ERROR_NO_ERROR);
  TRI_vocbase_col_status_e status;  // ignored here
  TRI_vocbase_col_t* collection =
      TRI_UseCollectionByIdVocBase(vocbase, collectionId, status);

  if (collection == nullptr) {
    res = TRI_errno();

    if (res == TRI_ERROR_ARANGO_CORRUPTED_COLLECTION) {
      LOG(WARN) << "unable to open collection " << collectionId << ". Please check the logs above for errors.";
    }

    return nullptr;
  }

  TRI_document_collection_t* document = collection->_collection;
  TRI_ASSERT(document != nullptr);

  // disable secondary indexes for the moment
  document->useSecondaryIndexes(false);

  openedCollections.emplace(collectionId, collection);
  res = TRI_ERROR_NO_ERROR;
  return collection;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a collection
/// the collection will be opened after this call and inserted into a local
/// cache for faster lookups
/// returns nullptr if the collection does not exist
////////////////////////////////////////////////////////////////////////////////

TRI_document_collection_t* RecoverState::getCollection(
    TRI_voc_tick_t databaseId, TRI_voc_cid_t collectionId) {
  TRI_vocbase_t* vocbase = useDatabase(databaseId);

  if (vocbase == nullptr) {
    LOG(TRACE) << "database " << databaseId << " not found";
    return nullptr;
  }

  int res;
  TRI_vocbase_col_t* collection = useCollection(vocbase, collectionId, res);

  if (collection == nullptr) {
    LOG(TRACE) << "collection " << collectionId << " of database " << databaseId << " not found";
    return nullptr;
  }

  TRI_document_collection_t* document = collection->_collection;
  TRI_ASSERT(document != nullptr);

  return document;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a single operation inside a transaction
////////////////////////////////////////////////////////////////////////////////

int RecoverState::executeSingleOperation(
    TRI_voc_tick_t databaseId, TRI_voc_cid_t collectionId,
    TRI_df_marker_t const* marker, TRI_voc_fid_t fid,
    std::function<int(SingleCollectionTransaction*, Marker*)> func) {
  // first find the correct database
  TRI_vocbase_t* vocbase = useDatabase(databaseId);

  if (vocbase == nullptr) {
    LOG(TRACE) << "database " << databaseId << " not found";
    return TRI_ERROR_ARANGO_DATABASE_NOT_FOUND;
  }

  int res;
  TRI_vocbase_col_t* collection = useCollection(vocbase, collectionId, res);

  if (collection == nullptr || collection->_collection == nullptr) {
    if (res == TRI_ERROR_ARANGO_CORRUPTED_COLLECTION) {
      return res;
    }

    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }

  TRI_voc_tick_t tickMax = collection->_collection->_tickMax;
  if (marker->getTick() <= tickMax) {
    // already transferred this marker
    return TRI_ERROR_NO_ERROR;
  }

  res = TRI_ERROR_INTERNAL;

  try {
    SingleCollectionTransaction trx(arangodb::StandaloneTransactionContext::Create(vocbase), collectionId, TRI_TRANSACTION_WRITE);

    trx.addHint(TRI_TRANSACTION_HINT_SINGLE_OPERATION, false);
    trx.addHint(TRI_TRANSACTION_HINT_NO_BEGIN_MARKER, false);
    trx.addHint(TRI_TRANSACTION_HINT_NO_ABORT_MARKER, false);
    trx.addHint(TRI_TRANSACTION_HINT_NO_THROTTLING, false);
    trx.addHint(TRI_TRANSACTION_HINT_LOCK_NEVER, false);

    res = trx.begin();

    if (res != TRI_ERROR_NO_ERROR) {
      THROW_ARANGO_EXCEPTION(res);
    }

    auto envelope = std::make_unique<MarkerEnvelope>(marker, fid);

    // execute the operation
    res = func(&trx, envelope.get());

    if (res != TRI_ERROR_NO_ERROR) {
      THROW_ARANGO_EXCEPTION(res);
    }

    // commit the operation
    res = trx.commit();
  } catch (arangodb::basics::Exception const& ex) {
    res = ex.code();
  } catch (...) {
    res = TRI_ERROR_INTERNAL;
  }

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief callback to handle one marker during recovery
/// this function only builds up state and does not change any data
////////////////////////////////////////////////////////////////////////////////

bool RecoverState::InitialScanMarker(TRI_df_marker_t const* marker, void* data,
                                     TRI_datafile_t* datafile) {
  RecoverState* state = reinterpret_cast<RecoverState*>(data);

  TRI_ASSERT(marker != nullptr);

  // note the marker's tick
  TRI_voc_tick_t const tick = marker->getTick();

  TRI_ASSERT(tick >= state->lastTick);

  if (tick > state->lastTick) {
    state->lastTick = tick;
  }

  char const* p = reinterpret_cast<char const*>(marker) + sizeof(TRI_df_marker_t);
  // TODO
  VPackSlice const slice(p);

  switch (marker->getType()) {
    case TRI_DF_MARKER_VPACK_BEGIN_TRANSACTION: {
      // insert this transaction into the list of failed transactions
      // we do this because if we don't find a commit marker for this
      // transaction,
      // we'll have it in the failed list at the end of the scan and can ignore
      // it
      TRI_voc_tick_t databaseId = NumericValue<TRI_voc_tick_t>(slice, "database");
      TRI_voc_tid_t tid = NumericValue<TRI_voc_tid_t>(slice, "tid");
      state->failedTransactions.emplace(tid, std::make_pair(databaseId, false));
      break;
    }

    case TRI_DF_MARKER_VPACK_COMMIT_TRANSACTION: {
      // remove this transaction from the list of failed transactions
      TRI_voc_tid_t tidValue = NumericValue<TRI_voc_tid_t>(slice, "tid");
      state->failedTransactions.erase(tidValue);
      break;
    }

    case TRI_DF_MARKER_VPACK_ABORT_TRANSACTION: {
      // insert this transaction into the list of failed transactions
      TRI_voc_tick_t databaseId = NumericValue<TRI_voc_tick_t>(slice, "database");
      TRI_voc_tid_t tid = NumericValue<TRI_voc_tid_t>(slice, "tid");
      state->failedTransactions[tid] = std::make_pair(databaseId, true);
      break;
    }

    case TRI_DF_MARKER_BEGIN_REMOTE_TRANSACTION: {
      transaction_remote_begin_marker_t const* m =
          reinterpret_cast<transaction_remote_begin_marker_t const*>(marker);
      // insert this transaction into the list of failed transactions
      state->failedTransactions.emplace(m->_transactionId,
                                        std::make_pair(m->_databaseId, false));
      break;
    }

    case TRI_DF_MARKER_COMMIT_REMOTE_TRANSACTION: {
      transaction_remote_commit_marker_t const* m =
          reinterpret_cast<transaction_remote_commit_marker_t const*>(marker);
      // remove this transaction from the list of failed transactions
      state->failedTransactions.erase(m->_transactionId);
      break;
    }

    case TRI_DF_MARKER_ABORT_REMOTE_TRANSACTION: {
      transaction_remote_abort_marker_t const* m =
          reinterpret_cast<transaction_remote_abort_marker_t const*>(marker);
      // insert this transaction into the list of failed transactions
      // the transaction is treated the same as a regular local transaction that
      // is aborted
      auto it = state->failedTransactions.find(m->_transactionId);

      if (it != state->failedTransactions.end()) {
        state->failedTransactions.erase(m->_transactionId);
      }

      // and (re-)insert
      state->failedTransactions.emplace(m->_transactionId,
                                        std::make_pair(m->_databaseId, true));
      break;
    }

    case TRI_DF_MARKER_VPACK_DROP_COLLECTION: {
      // note that the collection was dropped and doesn't need to be recovered
      TRI_voc_cid_t cid = NumericValue<TRI_voc_cid_t>(slice, "cid");
      state->droppedIds.emplace(cid);
      break;
    }

    default: {
      // do nothing
    }
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief callback to replay one marker during recovery
/// this function modifies indexes etc.
////////////////////////////////////////////////////////////////////////////////

bool RecoverState::ReplayMarker(TRI_df_marker_t const* marker, void* data,
                                TRI_datafile_t* datafile) {
  RecoverState* state = reinterpret_cast<RecoverState*>(data);

#ifdef ARANGODB_ENABLE_FAILURE_TESTS
  LOG(TRACE) << "replaying marker of type " << TRI_NameMarkerDatafile(marker);
#endif
      
  char const* p = reinterpret_cast<char const*>(marker);
  // TODO
  VPackSlice const markerData(p + sizeof(TRI_df_marker_t));

  try {
    switch (marker->getType()) {
      case TRI_DF_MARKER_PROLOGUE: {
        // simply note the last state
        auto const* m = reinterpret_cast<TRI_df_prologue_marker_t const*>(marker);
        state->resetCollection(m->_databaseId, m->_collectionId);
        return true;
      }

      // -----------------------------------------------------------------------------
      // crud operations
      // -----------------------------------------------------------------------------

      case TRI_DF_MARKER_VPACK_DOCUMENT: {
        // re-insert the document/edge into the collection
        TRI_voc_tick_t databaseId = state->lastDatabaseId; // from prologue
        TRI_voc_cid_t collectionId = state->lastCollectionId; // from prologue

        if (state->isDropped(databaseId, collectionId)) {
          return true;
        }

        TRI_voc_tid_t transactionId = *reinterpret_cast<TRI_voc_tid_t const*>(p + sizeof(TRI_df_marker_t));

        if (state->ignoreTransaction(transactionId)) {
          // transaction was aborted
          return true;
        }

        int res = state->executeSingleOperation(
            databaseId, collectionId, marker, datafile->_fid,
            [&](SingleCollectionTransaction* trx, Marker* envelope) -> int {
              if (IsVolatile(trx->trxCollection())) {
                return TRI_ERROR_NO_ERROR;
              }

              TRI_df_marker_t const* marker = static_cast<TRI_df_marker_t const*>(envelope->mem());

              std::string const collectionName = trx->documentCollection()->_info.name();
              uint8_t const* ptr = reinterpret_cast<uint8_t const*>(marker) + DatafileHelper::VPackOffset(marker->getType());

              OperationOptions options;
              options.silent = true;
              options.recoveryMarker = envelope;

              // try an insert first
              OperationResult opRes = trx->insert(collectionName, VPackSlice(ptr), options);
              int res = opRes.code;

              if (res == TRI_ERROR_ARANGO_UNIQUE_CONSTRAINT_VIOLATED) {
                // document/edge already exists, now make it an update
                opRes = trx->update(collectionName, VPackSlice(ptr), VPackSlice(ptr), options);
                res = opRes.code;
              }

              return res;
            });

        if (res != TRI_ERROR_NO_ERROR && res != TRI_ERROR_ARANGO_CONFLICT &&
            res != TRI_ERROR_ARANGO_DATABASE_NOT_FOUND &&
            res != TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
          LOG(WARN) << "unable to insert document in collection " << collectionId << " of database " << databaseId << ": " << TRI_errno_string(res);
          ++state->errorCount;
          return state->canContinue();
        }
        break;
      }

      case TRI_DF_MARKER_VPACK_REMOVE: {
        // re-apply the remove operation
        TRI_voc_tick_t databaseId = state->lastDatabaseId; // from prologue
        TRI_voc_cid_t collectionId = state->lastCollectionId; // from prologue

        TRI_ASSERT(databaseId > 0);
        TRI_ASSERT(collectionId > 0);

        if (state->isDropped(databaseId, collectionId)) {
          return true;
        }

        TRI_voc_tid_t transactionId = *reinterpret_cast<TRI_voc_tid_t const*>(p + sizeof(TRI_df_marker_t));
        
        if (state->ignoreTransaction(transactionId)) {
          return true;
        }

        int res = state->executeSingleOperation(
            databaseId, collectionId, marker, datafile->_fid,
            [&](SingleCollectionTransaction* trx, Marker* envelope) -> int {
              if (IsVolatile(trx->trxCollection())) {
                return TRI_ERROR_NO_ERROR;
              }
              
              TRI_df_marker_t const* marker = static_cast<TRI_df_marker_t const*>(envelope->mem());

              std::string const collectionName = trx->documentCollection()->_info.name();
              uint8_t const* ptr = reinterpret_cast<uint8_t const*>(marker) + DatafileHelper::VPackOffset(marker->getType());

              OperationOptions options;
              options.silent = true;
              options.recoveryMarker = envelope;

              OperationResult opRes = trx->remove(collectionName, VPackSlice(ptr), options);
              int res = opRes.code;

              return res;
            });

        if (res != TRI_ERROR_NO_ERROR && res != TRI_ERROR_ARANGO_CONFLICT &&
            res != TRI_ERROR_ARANGO_DATABASE_NOT_FOUND &&
            res != TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
          LOG(WARN) << "unable to remove document in collection " << collectionId << " of database " << databaseId << ": " << TRI_errno_string(res);
          ++state->errorCount;
          return state->canContinue();
        }
        break;
      }

      // -----------------------------------------------------------------------------
      // ddl
      // -----------------------------------------------------------------------------

      case TRI_DF_MARKER_VPACK_RENAME_COLLECTION: {
        TRI_voc_tick_t databaseId = NumericValue<TRI_voc_tick_t>(markerData, "database");
        TRI_voc_cid_t collectionId = NumericValue<TRI_voc_cid_t>(markerData, "cid");
        VPackSlice const payloadSlice = markerData.get("data");
        
        if (!payloadSlice.isObject()) {
          LOG(WARN) << "cannot rename collection: invalid marker";
          ++state->errorCount;
          return state->canContinue();
        }

        if (state->isDropped(databaseId)) {
          return true;
        }

        TRI_vocbase_t* vocbase = state->useDatabase(databaseId);

        if (vocbase == nullptr) {
          // if the underlying database is gone, we can go on
          LOG(TRACE) << "cannot open database " << databaseId;
          return true;
        }

        TRI_vocbase_col_t* collection = state->releaseCollection(collectionId);

        if (collection == nullptr) {
          collection = TRI_LookupCollectionByIdVocBase(vocbase, collectionId);
        }

        if (collection == nullptr) {
          // if the underlying collection is gone, we can go on
          LOG(TRACE) << "cannot open collection " << collectionId;
          return true;
        }

        VPackSlice nameSlice = payloadSlice.get("name");
        if (!nameSlice.isString()) {
          LOG(WARN) << "cannot rename collection " << collectionId << " in database " << databaseId << ": name attribute is no string";
          ++state->errorCount;
          return state->canContinue();
        }
        std::string name = nameSlice.copyString();

        // check if other collection exist with target name
        TRI_vocbase_col_t* other =
            TRI_LookupCollectionByNameVocBase(vocbase, name.c_str());

        if (other != nullptr) {
          TRI_voc_cid_t otherCid = other->_cid;
          state->releaseCollection(otherCid);
          TRI_DropCollectionVocBase(vocbase, other, false);
        }

        int res =
            TRI_RenameCollectionVocBase(vocbase, collection, name.c_str(), true, false);

        if (res != TRI_ERROR_NO_ERROR) {
          LOG(WARN) << "cannot rename collection " << collectionId << " in database " << databaseId << " to '" << name << "': " << TRI_errno_string(res);
          ++state->errorCount;
          return state->canContinue();
        }
        break;
      }

      case TRI_DF_MARKER_VPACK_CHANGE_COLLECTION: {
        TRI_voc_tick_t databaseId = NumericValue<TRI_voc_tick_t>(markerData, "database");
        TRI_voc_cid_t collectionId = NumericValue<TRI_voc_cid_t>(markerData, "cid");
        VPackSlice const payloadSlice = markerData.get("data");
        
        if (!payloadSlice.isObject()) {
          LOG(WARN) << "cannot change properties of collection: invalid marker";
          ++state->errorCount;
          return state->canContinue();
        }

        if (state->isDropped(databaseId)) {
          return true;
        }

        TRI_vocbase_t* vocbase = state->useDatabase(databaseId);

        if (vocbase == nullptr) {
          // if the underlying database is gone, we can go on
          LOG(TRACE) << "cannot open database " << databaseId;
          return true;
        }

        TRI_document_collection_t* document =
            state->getCollection(databaseId, collectionId);

        if (document == nullptr) {
          // if the underlying collection is gone, we can go on
          LOG(TRACE) << "cannot change properties of collection " << collectionId << " in database " << databaseId << ": " << TRI_errno_string(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
          return true;
        }

        int res = TRI_UpdateCollectionInfo(
            vocbase, document, payloadSlice, vocbase->_settings.forceSyncProperties);

        if (res != TRI_ERROR_NO_ERROR) {
          LOG(WARN) << "cannot change collection properties for collection " << collectionId << " in database " << databaseId << ": " << TRI_errno_string(res);
          ++state->errorCount;
          return state->canContinue();
        }

        break;
      }

      case TRI_DF_MARKER_VPACK_CREATE_INDEX: {
        TRI_voc_tick_t databaseId = NumericValue<TRI_voc_tick_t>(markerData, "database");
        TRI_voc_cid_t collectionId = NumericValue<TRI_voc_cid_t>(markerData, "cid");
        VPackSlice const payloadSlice = markerData.get("data");
        
        if (!payloadSlice.isObject()) {
          LOG(WARN) << "cannot create index for collection: invalid marker";
          ++state->errorCount;
          return state->canContinue();
        }
        
        TRI_idx_iid_t indexId = NumericValue<TRI_idx_iid_t>(payloadSlice, "iid");

        if (state->isDropped(databaseId, collectionId)) {
          return true;
        }

        TRI_vocbase_t* vocbase = state->useDatabase(databaseId);

        if (vocbase == nullptr) {
          // if the underlying database is gone, we can go on
          LOG(TRACE) << "cannot create index for collection " << collectionId << " in database " << databaseId << ": " << TRI_errno_string(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
          return true;
        }

        TRI_document_collection_t* document =
            state->getCollection(databaseId, collectionId);

        if (document == nullptr) {
          // if the underlying collection is gone, we can go on
          LOG(TRACE) << "cannot create index for collection " << collectionId << " in database " << databaseId << ": " << TRI_errno_string(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
          return true;
        }
        
        TRI_vocbase_col_t* col = TRI_LookupCollectionByIdVocBase(vocbase, collectionId);

        if (col == nullptr) {
          // if the underlying collection gone, we can go on
          LOG(TRACE) << "cannot create index for collection " << collectionId << " in database " << databaseId << ": " << TRI_errno_string(TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
          return true;
        }

        std::string const indexName("index-" + std::to_string(indexId) + ".json");
        std::string const filename(arangodb::basics::FileUtils::buildFilename(col->path(), indexName));

        bool ok = arangodb::basics::VelocyPackHelper::velocyPackToFile(
            filename.c_str(), payloadSlice, vocbase->_settings.forceSyncProperties);

        if (!ok) {
          LOG(WARN) << "cannot create index " << indexId << ", collection " << collectionId << " in database " << databaseId;
          ++state->errorCount;
          return state->canContinue();
        } else {
          document->_indexFiles.emplace_back(filename);
        }

        break;
      }

      case TRI_DF_MARKER_VPACK_CREATE_COLLECTION: {
        TRI_voc_tick_t databaseId = NumericValue<TRI_voc_tick_t>(markerData, "database");
        TRI_voc_cid_t collectionId = NumericValue<TRI_voc_cid_t>(markerData, "cid");
        VPackSlice const payloadSlice = markerData.get("data");
        
        if (!payloadSlice.isObject()) {
          LOG(WARN) << "cannot create collection: invalid marker";
          ++state->errorCount;
          return state->canContinue();
        }
        
        // remove the drop marker
        state->droppedCollections.erase(collectionId);

        if (state->isDropped(databaseId)) {
          return true;
        }

        TRI_vocbase_t* vocbase = state->useDatabase(databaseId);

        if (vocbase == nullptr) {
          // if the underlying database is gone, we can go on
          LOG(TRACE) << "cannot open database " << databaseId;
          return true;
        }

        TRI_vocbase_col_t* collection = state->releaseCollection(collectionId);

        if (collection == nullptr) {
          collection = TRI_LookupCollectionByIdVocBase(vocbase, collectionId);
        }
        
        if (collection != nullptr) {
          // drop an existing collection
          TRI_DropCollectionVocBase(vocbase, collection, false);
        }

        // check if there is another collection with the same name as the one that
        // we attempt to create
        VPackSlice const nameSlice = payloadSlice.get("name");
        std::string name = "";

        if (nameSlice.isString()) {
          name = nameSlice.copyString();
          collection = TRI_LookupCollectionByNameVocBase(vocbase, name.c_str());

          if (collection != nullptr) {  
            TRI_voc_cid_t otherCid = collection->_cid;

            state->releaseCollection(otherCid);
            TRI_DropCollectionVocBase(vocbase, collection, false);
          }
        } else {
          LOG(WARN) << "empty name attribute in create collection marker for collection " << collectionId << " and database " << databaseId;
          ++state->errorCount;
          return state->canContinue();
        }

        // fiddle "isSystem" value, which is not contained in the JSON file
        bool isSystemValue = false;
        if (!name.empty()) {
          isSystemValue = name[0] == '_';
        }

        VPackBuilder bx;
        bx.openObject();
        bx.add("isSystem", VPackValue(isSystemValue));
        bx.close();
        VPackSlice isSystem = bx.slice();
        VPackBuilder b2 = VPackCollection::merge(payloadSlice, isSystem, false);

        arangodb::VocbaseCollectionInfo info(vocbase, name.c_str(), b2.slice());

        if (state->willBeDropped(collectionId)) {
          // in case we detect that this collection is going to be deleted anyway,
          // set
          // the sync properties to false temporarily
          bool oldSync = vocbase->_settings.forceSyncProperties;
          vocbase->_settings.forceSyncProperties = false;
          collection =
              TRI_CreateCollectionVocBase(vocbase, info, collectionId, false);
          vocbase->_settings.forceSyncProperties = oldSync;

        } else {
          // collection will be kept
          collection =
              TRI_CreateCollectionVocBase(vocbase, info, collectionId, false);
        }

        if (collection == nullptr) {
          LOG(WARN) << "cannot create collection " << collectionId << " in database " << databaseId << ": " << TRI_last_error();
          ++state->errorCount;
          return state->canContinue();
        }
        break;
      }

      case TRI_DF_MARKER_VPACK_CREATE_DATABASE: {
        TRI_voc_tick_t databaseId = NumericValue<TRI_voc_tick_t>(markerData, "database");
        VPackSlice const payloadSlice = markerData.get("data");
        
        if (!payloadSlice.isObject()) {
          LOG(WARN) << "cannot create database: invalid marker";
          ++state->errorCount;
          return state->canContinue();
        }
        
        // remove the drop marker
        state->droppedDatabases.erase(databaseId);
        TRI_vocbase_t* vocbase = state->releaseDatabase(databaseId);

        if (vocbase != nullptr) {
          // remove already existing database
          int statusCode =
              TRI_DropByIdDatabaseServer(state->server, databaseId, false, false);
          WaitForDeletion(state->server, databaseId, statusCode);
        }

        VPackSlice const nameSlice = payloadSlice.get("name");

        if (!nameSlice.isString()) {
          LOG(WARN) << "cannot unpack database properties for database " << databaseId;
          ++state->errorCount;
          return state->canContinue();
        }

        std::string nameString = nameSlice.copyString();

        // remove already existing database with same name
        vocbase =
            TRI_LookupDatabaseByNameServer(state->server, nameString.c_str());

        if (vocbase != nullptr) {
          TRI_voc_tick_t otherId = vocbase->_id;

          state->releaseDatabase(otherId);
          int statusCode = TRI_DropDatabaseServer(
              state->server, nameString.c_str(), false, false);
          WaitForDeletion(state->server, otherId, statusCode);
        }

        TRI_vocbase_defaults_t defaults;
        TRI_GetDatabaseDefaultsServer(state->server, &defaults);

        vocbase = nullptr;
        WaitForDeletion(state->server, databaseId,
                        TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
        int res = TRI_CreateDatabaseServer(state->server, databaseId,
                                          nameString.c_str(), &defaults,
                                          &vocbase, false);

        if (res != TRI_ERROR_NO_ERROR) {
          LOG(WARN) << "cannot create database " << databaseId << ": " << TRI_errno_string(res);
          ++state->errorCount;
          return state->canContinue();
        }
        break;
      }

      case TRI_DF_MARKER_VPACK_DROP_INDEX: {
        TRI_voc_tick_t databaseId = NumericValue<TRI_voc_tick_t>(markerData, "database");
        TRI_voc_cid_t collectionId = NumericValue<TRI_voc_cid_t>(markerData, "cid");
        VPackSlice const payloadSlice = markerData.get("data");
        
        if (!payloadSlice.isObject()) {
          LOG(WARN) << "cannot drop index for collection: invalid marker";
          ++state->errorCount;
          return state->canContinue();
        }
        
        TRI_idx_iid_t indexId = NumericValue<TRI_idx_iid_t>(payloadSlice, "id");

        if (state->isDropped(databaseId, collectionId)) {
          return true;
        }

        TRI_vocbase_t* vocbase = state->useDatabase(databaseId);

        if (vocbase == nullptr) {
          // if the underlying database is gone, we can go on
          LOG(TRACE) << "cannot open database " << databaseId;
          return true;
        }

        TRI_document_collection_t* document =
            state->getCollection(databaseId, collectionId);
        if (document == nullptr) {
          // if the underlying collection gone, we can go on
          return true;
        }

        TRI_vocbase_col_t* col = TRI_LookupCollectionByIdVocBase(vocbase, collectionId);

        if (col == nullptr) {
          // if the underlying collection gone, we can go on
          return true;
        }

        // ignore any potential error returned by this call
        TRI_DropIndexDocumentCollection(document, indexId, false);
        TRI_RemoveFileIndexCollection(document, indexId);

        // additionally remove the index file
        std::string const indexName("index-" + std::to_string(indexId) + ".json");
        std::string const filename(arangodb::basics::FileUtils::buildFilename(col->path(), indexName));

        TRI_UnlinkFile(filename.c_str());
        break;
      }

      case TRI_DF_MARKER_VPACK_DROP_COLLECTION: {
        TRI_voc_tick_t databaseId = NumericValue<TRI_voc_tick_t>(markerData, "database");
        TRI_voc_cid_t collectionId = NumericValue<TRI_voc_cid_t>(markerData, "cid");
        
        // insert the drop marker
        state->droppedCollections.emplace(collectionId);

        TRI_vocbase_t* vocbase = state->useDatabase(databaseId);

        if (vocbase == nullptr) {
          // database already deleted - do nothing
          return true;
        }

        // ignore any potential error returned by this call
        TRI_vocbase_col_t* collection = state->releaseCollection(collectionId);

        if (collection == nullptr) {
          collection = TRI_LookupCollectionByIdVocBase(vocbase, collectionId);
        }

        if (collection != nullptr) {
          TRI_DropCollectionVocBase(vocbase, collection, false);
        }
        break;
      }

      case TRI_DF_MARKER_VPACK_DROP_DATABASE: {
        TRI_voc_tick_t databaseId = NumericValue<TRI_voc_tick_t>(markerData, "database");

        // insert the drop marker
        state->droppedDatabases.emplace(databaseId);
        TRI_vocbase_t* vocbase = state->releaseDatabase(databaseId);

        if (vocbase != nullptr) {
          // ignore any potential error returned by this call
          TRI_DropByIdDatabaseServer(state->server, databaseId, false, false);
        }
        break;
      }
      
      case TRI_DF_MARKER_HEADER: 
      case TRI_DF_MARKER_FOOTER: {
        // new datafile or end of datafile. forget state!
        state->resetCollection();
        return true;
      }

      default: {
        // do nothing
      }
    }

    return true;
  }
  catch (...) {
    LOG(WARN) << "cannot replay marker";
    ++state->errorCount;
    return state->canContinue();
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replay a single logfile
////////////////////////////////////////////////////////////////////////////////

int RecoverState::replayLogfile(Logfile* logfile, int number) {
  std::string const logfileName = logfile->filename();

  int const n = static_cast<int>(logfilesToProcess.size());

  LOG(INFO) << "replaying WAL logfile '" << logfileName << "' (" << number + 1 << " of " << n << ")";

  // Advise on sequential use:
  TRI_MMFileAdvise(logfile->df()->_data, logfile->df()->_maximalSize,
                   TRI_MADVISE_SEQUENTIAL);
  TRI_MMFileAdvise(logfile->df()->_data, logfile->df()->_maximalSize,
                   TRI_MADVISE_WILLNEED);

  if (!TRI_IterateDatafile(logfile->df(), &RecoverState::ReplayMarker,
                           static_cast<void*>(this))) {
    LOG(WARN) << "WAL inspection failed when scanning logfile '" << logfileName << "'";
    return TRI_ERROR_ARANGO_RECOVERY;
  }

  // Advise on random access use:
  TRI_MMFileAdvise(logfile->df()->_data, logfile->df()->_maximalSize,
                   TRI_MADVISE_RANDOM);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replay all logfiles
////////////////////////////////////////////////////////////////////////////////

int RecoverState::replayLogfiles() {
  droppedCollections.clear();
  droppedDatabases.clear();

  int i = 0;
  for (auto& it : logfilesToProcess) {
    TRI_ASSERT(it != nullptr);
    int res = replayLogfile(it, i++);

    if (res != TRI_ERROR_NO_ERROR) {
      return res;
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief abort open transactions
////////////////////////////////////////////////////////////////////////////////

int RecoverState::abortOpenTransactions() {
  if (failedTransactions.empty()) {
    // nothing to do
    return TRI_ERROR_NO_ERROR;
  }

  LOG(TRACE) << "writing abort markers for still open transactions";
  int res = TRI_ERROR_NO_ERROR;
      
  VPackBuilder builder;

  try {
    // write abort markers for all transactions
    for (auto it = failedTransactions.begin(); it != failedTransactions.end();
         ++it) {
      TRI_voc_tid_t transactionId = (*it).first;

      if ((*it).second.second) {
        // already handled
        continue;
      }

      TRI_voc_tick_t databaseId = (*it).second.first;
     
      builder.openObject();
      builder.add("database", VPackValue(databaseId)); 
      builder.add("tid", VPackValue(transactionId));
      builder.close();

      Marker marker(TRI_DF_MARKER_VPACK_ABORT_TRANSACTION, builder.slice());
      SlotInfoCopy slotInfo =
          arangodb::wal::LogfileManager::instance()->allocateAndWrite(
              marker.mem(), marker.size(), false);

      if (slotInfo.errorCode != TRI_ERROR_NO_ERROR) {
        THROW_ARANGO_EXCEPTION(slotInfo.errorCode);
      }

      // recycle builder
      builder.clear();
    }
  } catch (arangodb::basics::Exception const& ex) {
    res = ex.code();
  } catch (...) {
    res = TRI_ERROR_INTERNAL;
  }

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove all empty logfiles found during logfile inspection
////////////////////////////////////////////////////////////////////////////////

int RecoverState::removeEmptyLogfiles() {
  if (emptyLogfiles.empty()) {
    return TRI_ERROR_NO_ERROR;
  }

  LOG(TRACE) << "removing empty WAL logfiles";

  for (auto it = emptyLogfiles.begin(); it != emptyLogfiles.end(); ++it) {
    auto filename = (*it);

    if (basics::FileUtils::remove(filename, 0)) {
      LOG(TRACE) << "removing empty WAL logfile '" << filename << "'";
    }
  }

  emptyLogfiles.clear();

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief fill the secondary indexes of all collections used in recovery
////////////////////////////////////////////////////////////////////////////////

int RecoverState::fillIndexes() {
  // release all collections
  for (auto it = openedCollections.begin(); it != openedCollections.end();
       ++it) {
    TRI_vocbase_col_t* collection = (*it).second;
    TRI_document_collection_t* document = collection->_collection;

    TRI_ASSERT(document != nullptr);

    // activate secondary indexes
    document->useSecondaryIndexes(true);

    arangodb::SingleCollectionTransaction trx(arangodb::StandaloneTransactionContext::Create(collection->_vocbase),
        document->_info.id(), TRI_TRANSACTION_WRITE);

    int res = TRI_FillIndexesDocumentCollection(&trx, collection, document);

    if (res != TRI_ERROR_NO_ERROR) {
      return res;
    }
  }

  return TRI_ERROR_NO_ERROR;
}
