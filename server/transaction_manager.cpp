﻿/*
	Copyright (c) 2012 TOSHIBA CORPORATION.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as
	published by the Free Software Foundation, either version 3 of the
	License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*!
	@file
	@brief Implementation of TransactionManagement
*/
#include "transaction_manager.h"
#include "cluster_event_type.h"
#include "gs_error.h"
#include "transaction_context.h"

#include "base_container.h"

#define TM_THROW_TXN_CONTEXT_NOT_FOUND(message) \
	GS_THROW_CUSTOM_ERROR(                      \
		ContextNotFoundException, GS_ERROR_TM_SESSION_NOT_FOUND, message)

#define TM_THROW_REPL_CONTEXT_NOT_FOUND(message) \
	GS_THROW_CUSTOM_ERROR(                       \
		ContextNotFoundException, GS_ERROR_TM_REPLICATION_NOT_FOUND, message)

#define TM_THROW_STATEMENT_ALREADY_EXECUTED(message)         \
	GS_THROW_CUSTOM_ERROR(StatementAlreadyExecutedException, \
		GS_ERROR_TM_STATEMENT_ALREADY_EXECUTED, message)

ReplicationContext::ReplicationContext()
	: id_(TXN_UNDEF_REPLICATIONID),
	  stmtType_(0),
	  clientId_(TXN_EMPTY_CLIENTID),
	  pId_(UNDEF_PARTITIONID),
	  containerId_(UNDEF_CONTAINERID),
	  stmtId_(0),
	  clientNd_(NodeDescriptor::EMPTY_ND),
	  ackCounter_(0),
	  timeout_(0),
	  existFlag_(false)
{
}
ReplicationContext::ReplicationContext(const ReplicationContext &replContext)
	: id_(replContext.id_),
	  stmtType_(replContext.stmtType_),
	  clientId_(replContext.clientId_),
	  pId_(replContext.pId_),
	  containerId_(replContext.containerId_),
	  stmtId_(replContext.stmtId_),
	  clientNd_(replContext.clientNd_),
	  ackCounter_(replContext.ackCounter_),
	  timeout_(replContext.timeout_),
	  existFlag_(replContext.existFlag_)
{
}
ReplicationContext::~ReplicationContext() {}
ReplicationContext &ReplicationContext::operator=(
	const ReplicationContext &replContext) {
	if (this == &replContext) {
		return *this;
	}
	id_ = replContext.id_;
	stmtType_ = replContext.stmtType_;
	clientId_ = replContext.clientId_;
	pId_ = replContext.pId_;
	containerId_ = replContext.containerId_;
	stmtId_ = replContext.stmtId_;
	clientNd_ = replContext.clientNd_;
	ackCounter_ = replContext.ackCounter_;
	timeout_ = replContext.timeout_;
	existFlag_ = replContext.existFlag_;


	return *this;
}
ReplicationId ReplicationContext::getReplicationId() const {
	return id_;
}
int32_t ReplicationContext::getStatementType() const {
	return stmtType_;
}
const ClientId &ReplicationContext::getClientId() const {
	return clientId_;
}
PartitionId ReplicationContext::getPartitionId() const {
	return pId_;
}
ContainerId ReplicationContext::getContainerId() const {
	return containerId_;
}
StatementId ReplicationContext::getStatementId() const {
	return stmtId_;
}
const NodeDescriptor &ReplicationContext::getConnectionND() const {
	return clientNd_;
}
bool ReplicationContext::decrementAckCounter() {
	assert(ackCounter_ >= 0);
	if (ackCounter_ > 0) {
		return (--ackCounter_ == 0);
	}
	else {
		return true;
	}
}
void ReplicationContext::incrementAckCounter(uint32_t count) {
	ackCounter_ += count;
}
EventMonotonicTime ReplicationContext::getExpireTime() const {
	return timeout_;
}
bool ReplicationContext::getExistFlag() const {
	return existFlag_;
}
void ReplicationContext::setExistFlag(bool flag) {
	existFlag_ = flag;
}
void ReplicationContext::clear() {
	id_ = TXN_UNDEF_REPLICATIONID;
	stmtType_ = 0;
	clientId_ = TXN_EMPTY_CLIENTID;
	pId_ = UNDEF_PARTITIONID;
	containerId_ = UNDEF_CONTAINERID;
	stmtId_ = TXN_MIN_CLIENT_STATEMENTID, clientNd_ = NodeDescriptor::EMPTY_ND;
	ackCounter_ = 0;
	timeout_ = 0;
	existFlag_ = false;
}

TransactionManager::ContextSource::ContextSource()
	: stmtType_(-1),
	  isUpdateStmt_(false),
	  stmtId_(TXN_MIN_CLIENT_STATEMENTID),
	  containerId_(UNDEF_CONTAINERID),
	  txnTimeoutInterval_(TXN_DEFAULT_TRANSACTION_TIMEOUT_INTERVAL),
	  getMode_(AUTO),
	  txnMode_(AUTO_COMMIT) {}

TransactionManager::ContextSource::ContextSource(
	int32_t stmtType, bool isUpdateStmt)
	: stmtType_(stmtType),
	  isUpdateStmt_(isUpdateStmt),
	  stmtId_(TXN_MIN_CLIENT_STATEMENTID),
	  containerId_(UNDEF_CONTAINERID),
	  txnTimeoutInterval_(TXN_DEFAULT_TRANSACTION_TIMEOUT_INTERVAL),
	  getMode_(AUTO),
	  txnMode_(AUTO_COMMIT) {}

TransactionManager::ContextSource::ContextSource(int32_t stmtType,
	StatementId stmtId, ContainerId containerId, int32_t txnTimeoutInterval,
	GetMode getMode, TransactionMode txnMode, bool isUpdateStmt)
	: stmtType_(stmtType),
	  isUpdateStmt_(isUpdateStmt),
	  stmtId_(stmtId),
	  containerId_(containerId),
	  txnTimeoutInterval_(txnTimeoutInterval),
	  getMode_(getMode),
	  txnMode_(txnMode) {}

const TransactionManager::GetMode TransactionManager::AUTO = 0;
const TransactionManager::GetMode TransactionManager::CREATE = 1 << 0;
const TransactionManager::GetMode TransactionManager::GET = 1 << 1;
const TransactionManager::GetMode TransactionManager::PUT =
	(TransactionManager::CREATE | TransactionManager::GET);

const TransactionManager::TransactionMode TransactionManager::AUTO_COMMIT = 0;
const TransactionManager::TransactionMode
	TransactionManager::NO_AUTO_COMMIT_BEGIN = 1 << 0;
const TransactionManager::TransactionMode
	TransactionManager::NO_AUTO_COMMIT_CONTINUE = 1 << 1;
const TransactionManager::TransactionMode
	TransactionManager::NO_AUTO_COMMIT_BEGIN_OR_CONTINUE =
		(TransactionManager::NO_AUTO_COMMIT_BEGIN |
			TransactionManager::NO_AUTO_COMMIT_CONTINUE);

TransactionManager::TransactionManager(const ConfigTable &config)
	: pgConfig_(config),
	  replicationMode_(config.get<int32_t>(CONFIG_TABLE_TXN_REPLICATION_MODE)),
	  replicationTimeoutInterval_(
		  config.get<int32_t>(CONFIG_TABLE_TXN_REPLICATION_TIMEOUT_INTERVAL)),
	  txnTimeoutLimit_(
		  config.get<int32_t>(CONFIG_TABLE_TXN_TRANSACTION_TIMEOUT_LIMIT)),
	  txnContextMapManager_(UTIL_NEW TransactionContextMap::Manager *
							[pgConfig_.getPartitionGroupCount()]),
	  txnContextMap_(UTIL_NEW TransactionContextMap *
					 [pgConfig_.getPartitionGroupCount()]),
	  activeTxnMapManager_(UTIL_NEW ActiveTransactionMap::Manager *
						   [pgConfig_.getPartitionGroupCount()]),
	  activeTxnMap_(
		  UTIL_NEW ActiveTransactionMap * [pgConfig_.getPartitionGroupCount()]),
	  replContextMapManager_(UTIL_NEW ReplicationContextMap::Manager *
							 [pgConfig_.getPartitionGroupCount()]),
	  replContextMap_(UTIL_NEW ReplicationContextMap *
					  [pgConfig_.getPartitionGroupCount()]),
	  partition_(UTIL_NEW Partition * [pgConfig_.getPartitionCount()]),
	  ptLock_(UTIL_NEW int32_t[pgConfig_.getPartitionCount()]),
	  ptLockMutex_(UTIL_NEW util::Mutex[NUM_LOCK_MUTEX])
{
	try {
		for (PartitionId pId = 0; pId < pgConfig_.getPartitionCount(); pId++) {
			partition_[pId] = NULL;
			ptLock_[pId] = 0;
		}
		for (PartitionGroupId pgId = 0;
			 pgId < pgConfig_.getPartitionGroupCount(); pgId++) {
			txnContextMapManager_[pgId] = NULL;
			txnContextMap_[pgId] = NULL;

			activeTxnMapManager_[pgId] = NULL;
			activeTxnMap_[pgId] = NULL;

			replContextMapManager_[pgId] = NULL;
			replContextMap_[pgId] = NULL;
		}
		for (PartitionGroupId pgId = 0;
			 pgId < pgConfig_.getPartitionGroupCount(); pgId++) {
			txnContextMapManager_[pgId] =
				UTIL_NEW TransactionContextMap::Manager(util::AllocatorInfo(
					ALLOCATOR_GROUP_TXN_WORK, "sessionMap"));
			txnContextMapManager_[pgId]->setFreeElementLimit(
				DEFAULT_FREE_ELEMENT_LIMIT);
			txnContextMap_[pgId] = txnContextMapManager_[pgId]->create(
				HASH_SIZE,
				(TXN_STABLE_TRANSACTION_TIMEOUT_INTERVAL + TIMER_MERGIN_SEC) *
					1000,
				TIMER_INTERVAL_MILLISEC);

			activeTxnMapManager_[pgId] =
				UTIL_NEW ActiveTransactionMap::Manager(util::AllocatorInfo(
					ALLOCATOR_GROUP_TXN_WORK, "transactionMap"));
			activeTxnMapManager_[pgId]->setFreeElementLimit(
				DEFAULT_FREE_ELEMENT_LIMIT);
			activeTxnMap_[pgId] =
				activeTxnMapManager_[pgId]->create(HASH_SIZE, 1, 1);

			replContextMapManager_[pgId] =
				UTIL_NEW ReplicationContextMap::Manager(util::AllocatorInfo(
					ALLOCATOR_GROUP_TXN_WORK, "replicationMap"));
			replContextMapManager_[pgId]->setFreeElementLimit(
				DEFAULT_FREE_ELEMENT_LIMIT);
			replContextMap_[pgId] = replContextMapManager_[pgId]->create(
				HASH_SIZE,
				(TXN_STABLE_TRANSACTION_TIMEOUT_INTERVAL + TIMER_MERGIN_SEC) *
					1000,
				TIMER_INTERVAL_MILLISEC);
		}
	}
	catch (std::exception &e) {
		for (PartitionGroupId pgId = 0;
			 pgId < pgConfig_.getPartitionGroupCount(); pgId++) {
			txnContextMapManager_[pgId]->remove(txnContextMap_[pgId]);
			delete txnContextMapManager_[pgId];

			activeTxnMapManager_[pgId]->remove(activeTxnMap_[pgId]);
			delete activeTxnMapManager_[pgId];

			replContextMapManager_[pgId]->remove(replContextMap_[pgId]);
			delete replContextMapManager_[pgId];
		}
		delete[] txnContextMapManager_;
		delete[] txnContextMap_;

		delete[] activeTxnMapManager_;
		delete[] activeTxnMap_;

		delete[] replContextMapManager_;
		delete[] replContextMap_;


		delete[] partition_;

		delete[] ptLock_;
		delete[] ptLockMutex_;

		GS_RETHROW_USER_OR_SYSTEM(
			e, GS_EXCEPTION_MERGE_MESSAGE(
				   e, "Failed to initialize transaction manager"));
	}
}

TransactionManager::~TransactionManager() {
	for (PartitionId pId = 0; pId < pgConfig_.getPartitionCount(); pId++) {
		delete partition_[pId];
	}
	for (PartitionGroupId pgId = 0; pgId < pgConfig_.getPartitionGroupCount();
		 pgId++) {
		txnContextMapManager_[pgId]->remove(txnContextMap_[pgId]);
		delete txnContextMapManager_[pgId];

		activeTxnMapManager_[pgId]->remove(activeTxnMap_[pgId]);
		delete activeTxnMapManager_[pgId];

		replContextMapManager_[pgId]->remove(replContextMap_[pgId]);
		delete replContextMapManager_[pgId];
	}
	delete[] txnContextMapManager_;
	delete[] txnContextMap_;

	delete[] activeTxnMapManager_;
	delete[] activeTxnMap_;

	delete[] replContextMapManager_;
	delete[] replContextMap_;


	delete[] partition_;

	delete[] ptLock_;
	delete[] ptLockMutex_;
}

/*!
	@brief Creates Partition object
*/
void TransactionManager::createPartition(PartitionId pId) {
	if (partition_[pId] != NULL) {
		return;
	}

	try {
		const PartitionGroupId pgId = pgConfig_.getPartitionGroupId(pId);

		partition_[pId] = UTIL_NEW Partition(pId, txnTimeoutLimit_,
			txnContextMap_[pgId], activeTxnMap_[pgId], replContextMap_[pgId]);
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to create partition "
			"(pId="
				<< pId << ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}

/*!
	@brief Removes Partition object
*/
void TransactionManager::removePartition(PartitionId pId) {
	if (partition_[pId] == NULL) {
		return;
	}

	try {
		delete partition_[pId];
		partition_[pId] = NULL;
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to remove partition "
			"(pId="
				<< pId << ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}

/*!
	@brief Checks if Partition object exists
*/
bool TransactionManager::hasPartition(PartitionId pId) const {
	return (partition_[pId] != NULL);
}

/*!
	@brief Gets configuration of Partition group
*/
const PartitionGroupConfig &TransactionManager::getPartitionGroupConfig()
	const {
	return pgConfig_;
}

/*!
	@brief Gets current replication mode
*/
int32_t TransactionManager::getReplicationMode() const {
	return replicationMode_;
}

/*!
	@brief Gets replication timeout interval
*/
int32_t TransactionManager::getReplicationTimeoutInterval() const {
	return replicationTimeoutInterval_;
}

/*!
	@brief Creates transaction context
*/
TransactionContext &TransactionManager::put(util::StackAllocator &alloc,
	PartitionId pId, const ClientId &clientId, const ContextSource &src,
	const util::DateTime &now, EventMonotonicTime emNow, bool isRedo,
	TransactionId txnId) {
	createPartition(pId);
	TransactionContext &txn = partition_[pId]->put(alloc, clientId,
		src.containerId_, src.stmtId_, src.txnTimeoutInterval_, now, emNow,
		src.getMode_, src.txnMode_, src.isUpdateStmt_, isRedo, txnId);
	txn.manager_ = this;
	return txn;
}

/*!
	@brief Gets transaction context
*/
TransactionContext &TransactionManager::get(
	util::StackAllocator &alloc, PartitionId pId, const ClientId &clientId) {
	createPartition(pId);
	return partition_[pId]->get(alloc, clientId);
}

/*!
	@brief Removes transaction context
*/
void TransactionManager::remove(PartitionId pId, const ClientId &clientId) {
	createPartition(pId);
	{
		const PartitionGroupId pgId = pgConfig_.getPartitionGroupId(pId);
		TransactionContext *txn = txnContextMap_[pgId]->get(clientId);
		if (txn != NULL) {
			remove(*txn);
		}
	}
	partition_[pId]->remove(clientId);
}

/*!
	@brief Updates last executed statement's ID on a specified context
*/
void TransactionManager::update(TransactionContext &txn, StatementId stmtId) {
	createPartition(txn.getPartitionId());
	partition_[txn.getPartitionId()]->update(txn, stmtId);
}

/*!
	@brief Begins transaction on a specified context
*/
void TransactionManager::begin(
	TransactionContext &txn, EventMonotonicTime emNow)

{
	if (!txn.isActive()) {
		createPartition(txn.getPartitionId());
		const TransactionId txnId =
			partition_[txn.getPartitionId()]->assignNewTransactionId();
		partition_[txn.getPartitionId()]->begin(txn, txnId, emNow);
	}
}

/*!
	@brief Commits transaction on a specified context
*/
void TransactionManager::commit(
	TransactionContext &txn, BaseContainer &container) {
	if (txn.isActive()) {
		createPartition(txn.getPartitionId());
		container.commit(txn);
		partition_[txn.getPartitionId()]->commit(txn);
	}
	else {
		GS_THROW_USER_ERROR(GS_ERROR_TM_TRANSACTION_COMMIT_NOT_ALLOWED,
			"(pId=" << txn.getPartitionId()
					<< ", clientId=" << txn.getClientId()
					<< ", containerId=" << txn.getContainerId() << ")");
	}
}

/*!
	@brief Aborts transaction on a specified context
*/
void TransactionManager::abort(
	TransactionContext &txn, BaseContainer &container) {
	if (txn.isActive()) {
		createPartition(txn.getPartitionId());
		container.abort(txn);
		partition_[txn.getPartitionId()]->abort(txn);
	}
	else {
		GS_THROW_USER_ERROR(GS_ERROR_TM_TRANSACTION_ABORT_NOT_ALLOWED,
			"(pId=" << txn.getPartitionId()
					<< ", clientId=" << txn.getClientId()
					<< ", containerId=" << txn.getContainerId() << ")");
	}
}

/*!
	@brief Removes transaction forcibly on a specified context
*/
void TransactionManager::remove(TransactionContext &txn) {
	if (txn.isActive()) {
		createPartition(txn.getPartitionId());
		partition_[txn.getPartitionId()]->endTransaction(txn);
	}
}

/*!
	@brief Checks if statement already executed on a specified transaction
*/
void TransactionManager::checkStatementAlreadyExecuted(
	const TransactionContext &txn, StatementId stmtId, bool isUpdateStmt) {
	createPartition(txn.getPartitionId());
	partition_[txn.getPartitionId()]->checkStatementAlreadyExecuted(
		txn, stmtId, isUpdateStmt);
}

/*!
	@brief Checks if statement is continuous in a specified transaction
*/
void TransactionManager::checkStatementContinuousInTransaction(
	const TransactionContext &txn, StatementId stmtId, GetMode getMode,
	TransactionMode txnMode) {
	createPartition(txn.getPartitionId());
	partition_[txn.getPartitionId()]->checkStatementContinuousInTransaction(
		txn, stmtId, getMode, txnMode);
}

/*!
	@brief Checks if transaction is active
*/
bool TransactionManager::isActiveTransaction(
	PartitionId pId, TransactionId txnId) {
	createPartition(pId);
	return partition_[pId]->isActiveTransaction(txnId);
}

/*!
	@brief Gets list of all context ID (ClientID) in a specified partition
*/
void TransactionManager::getTransactionContextId(
	PartitionId pId, util::XArray<ClientId> &clientIds) {
	createPartition(pId);
	partition_[pId]->getTransactionContextId(clientIds);
}

/*!
	@brief Gets list of transaction timed out context ID (clientID) in a
   specified partition
*/
void TransactionManager::getTransactionTimeoutContextId(
	util::StackAllocator &alloc, PartitionGroupId pgId,
	EventMonotonicTime emNow, const util::XArray<bool> &checkPartitionFlagList,
	util::XArray<PartitionId> &pIds, util::XArray<ClientId> &clientIds) {
	const PartitionId beginPId = pgConfig_.getGroupBeginPartitionId(pgId);

	try {
		util::XArray<ClientId *> updateContextIds(alloc);
		util::XArray<ClientId *> updateContextIds2(alloc);

		ClientId *key;
		for (TransactionContext *txn =
				 txnContextMap_[pgId]->refresh(emNow, key);
			 txn != NULL; txn = txnContextMap_[pgId]->refresh(emNow, key)) {
			assert(txn->getPartitionId() >= beginPId);

			const PartitionId relativePId = txn->getPartitionId() - beginPId;

			if (checkPartitionFlagList[relativePId] && txn->isActive() &&
				txn->getTransactionExpireTime() <= emNow) {
				pIds.push_back(txn->getPartitionId());
				clientIds.push_back(*key);
				partition_[txn->getPartitionId()]->txnTimeoutCount_++;

				updateContextIds2.push_back(key);
			}
			else {
				updateContextIds.push_back(key);
			}
		}

		for (size_t i = 0; i < updateContextIds.size(); i++) {
			TransactionContext *txn =
				txnContextMap_[pgId]->get(*updateContextIds[i]);
			if (txn->isActive()) {
				txnContextMap_[pgId]->update(
					txn->getClientId(), txn->getTransactionExpireTime());
			}
			else {
				txnContextMap_[pgId]->update(
					txn->getClientId(), txn->getExpireTime());
			}
		}
		for (size_t i = 0; i < updateContextIds2.size(); i++) {
			TransactionContext *txn =
				txnContextMap_[pgId]->get(*updateContextIds2[i]);
			txnContextMap_[pgId]->update(
				txn->getClientId(), txn->getExpireTime());
		}

		assert(pIds.size() == clientIds.size());
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to check transaction timeout "
			"(pgId="
				<< pgId << ", emNow=" << emNow
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}

/*!
	@brief Gets list of expired context ID (clientID) in a specified partition
*/
void TransactionManager::getRequestTimeoutContextId(util::StackAllocator &alloc,
	PartitionGroupId pgId, EventMonotonicTime emNow,
	const util::XArray<bool> &checkPartitionFlagList,
	util::XArray<PartitionId> &pIds, util::XArray<ClientId> &clientIds) {
	const PartitionId beginPId = pgConfig_.getGroupBeginPartitionId(pgId);

	try {
		util::XArray<ClientId *> updateContextIds(alloc);

		ClientId *key;
		for (TransactionContext *txn =
				 txnContextMap_[pgId]->refresh(emNow, key);
			 txn != NULL; txn = txnContextMap_[pgId]->refresh(emNow, key)) {
			assert(txn->getPartitionId() >= beginPId);

			const PartitionId relativePId = txn->getPartitionId() - beginPId;

			if (checkPartitionFlagList[relativePId] &&
				txn->getExpireTime() <= emNow) {
				pIds.push_back(txn->getPartitionId());
				clientIds.push_back(*key);
				partition_[txn->getPartitionId()]->reqTimeoutCount_++;
			}
			else {
				updateContextIds.push_back(key);
			}
		}

		for (size_t i = 0; i < updateContextIds.size(); i++) {
			TransactionContext *txn =
				txnContextMap_[pgId]->get(*updateContextIds[i]);
			if (txn->isActive()) {
				txnContextMap_[pgId]->update(
					txn->getClientId(), txn->getTransactionExpireTime());
			}
			else {
				txnContextMap_[pgId]->update(
					txn->getClientId(), txn->getExpireTime());
			}
		}

		assert(pIds.size() == clientIds.size());
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to check request timeout "
			"(pgId="
				<< pgId << ", emNow=" << emNow
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}

/*!
	@brief Gets context information having active transaction in a specified
   partition
*/
void TransactionManager::backupTransactionActiveContext(PartitionId pId,
	TransactionId &maxTxnId, util::XArray<ClientId> &clientIds,
	util::XArray<TransactionId> &activeTxnIds,
	util::XArray<ContainerId> &refContainerIds,
	util::XArray<StatementId> &lastStmtIds,
	util::XArray<int32_t> &txnTimeoutIntervalSec) {
	createPartition(pId);
	partition_[pId]->backupTransactionActiveContext(maxTxnId, clientIds,
		activeTxnIds, refContainerIds, lastStmtIds, txnTimeoutIntervalSec);
}

/*!
	@brief Restores context information having active transaction in a specified
   partition
*/
void TransactionManager::restoreTransactionActiveContext(PartitionId pId,
	TransactionId maxTxnId, uint32_t numContext, const ClientId *clientIds,
	const TransactionId *activeTxnIds, const ContainerId *refContainerIds,
	const StatementId *lastStmtIds, const int32_t *txnTimeoutIntervalSec,
	EventMonotonicTime emNow) {
	removePartition(pId);
	createPartition(pId);
	partition_[pId]->restoreTransactionActiveContext(this, maxTxnId, numContext,
		clientIds, activeTxnIds, refContainerIds, lastStmtIds,
		txnTimeoutIntervalSec, emNow);
}

/*!
	@brief Creates replication context
*/
ReplicationContext &TransactionManager::put(PartitionId pId,
	const ClientId &clientId, const ContextSource &src, NodeDescriptor ND,
	EventMonotonicTime emNow) {
	createPartition(pId);
	return partition_[pId]->put(clientId, src.containerId_, src.stmtType_,
		src.stmtId_, ND, replicationTimeoutInterval_, emNow);
}

/*!
	@brief Gets replication context
*/
ReplicationContext &TransactionManager::get(
	PartitionId pId, ReplicationId replId) {
	createPartition(pId);
	return partition_[pId]->get(replId);
}

/*!
	@brief Removes replication context
*/
void TransactionManager::remove(PartitionId pId, ReplicationId replId) {
	createPartition(pId);
	partition_[pId]->remove(replId);
}

/*!
	@brief Gets list of expired replication context ID in a specified partition
*/
void TransactionManager::getReplicationTimeoutContextId(PartitionGroupId pgId,
	EventMonotonicTime emNow, util::XArray<PartitionId> &pIds,
	util::XArray<ReplicationId> &replIds) {
	try {
		ReplicationContextKey *key;
		for (ReplicationContext *replContext =
				 replContextMap_[pgId]->refresh(emNow, key);
			 replContext != NULL;
			 replContext = replContextMap_[pgId]->refresh(emNow, key)) {
			pIds.push_back(key->pId_);
			replIds.push_back(key->replId_);
			partition_[replContext->getPartitionId()]->replTimeoutCount_++;
		}

		assert(pIds.size() == replIds.size());
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to check replication timeout "
			"(pgId="
				<< pgId << ", emNow=" << emNow
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}


/*!
	@brief Gets the number of contexts in a specified partition group
*/
uint64_t TransactionManager::getTransactionContextCount(PartitionGroupId pgId) {
	return txnContextMap_[pgId]->size();
}
/*!
	@brief Gets the number of active transactions in a specified partition group
*/
uint64_t TransactionManager::getActiveTransactionCount(PartitionGroupId pgId) {
	return activeTxnMap_[pgId]->size();
}
/*!
	@brief Gets the number of replication contexts in a specified partition
   group
*/
uint64_t TransactionManager::getReplicationContextCount(PartitionGroupId pgId) {
	return replContextMap_[pgId]->size();
}
/*!
	@brief Gets the number of times the context expiraion occurrs in a specified
   partition
*/
uint64_t TransactionManager::getRequestTimeoutCount(PartitionId pId) const {
	return (partition_[pId] == NULL)
			   ? 0
			   : partition_[pId]->getRequestTimeoutCount();
}
/*!
	@brief Gets the number of times the transaction timeout occurrs in a
   specified partition
*/
uint64_t TransactionManager::getTransactionTimeoutCount(PartitionId pId) const {
	return (partition_[pId] == NULL)
			   ? 0
			   : partition_[pId]->getTransactionTimeoutCount();
}
/*!
	@brief Gets the number of times the replication timeout occurrs in a
   specified partition
*/
uint64_t TransactionManager::getReplicationTimeoutCount(PartitionId pId) const {
	return (partition_[pId] == NULL)
			   ? 0
			   : partition_[pId]->getReplicationTimeoutCount();
}
/*!
	@brief Gets memory usage on a specified partition group
*/
size_t TransactionManager::getMemoryUsage(
	PartitionGroupId pgId, bool includeFreeMemory) {
	const size_t txnContextUsage =
		txnContextMapManager_[pgId]->getElementSize() *
		txnContextMapManager_[pgId]->getElementCount();
	const size_t activeTxnUsage = activeTxnMapManager_[pgId]->getElementSize() *
								  activeTxnMapManager_[pgId]->getElementCount();
	const size_t replContextUsage =
		replContextMapManager_[pgId]->getElementSize() *
		replContextMapManager_[pgId]->getElementCount();

	const size_t txnContextFree =
		(includeFreeMemory
				? txnContextMapManager_[pgId]->getElementSize() *
					  txnContextMapManager_[pgId]->getFreeElementCount()
				: 0);
	const size_t activeTxnFree =
		(includeFreeMemory
				? activeTxnMapManager_[pgId]->getElementSize() *
					  activeTxnMapManager_[pgId]->getFreeElementCount()
				: 0);
	const size_t replContextFree =
		(includeFreeMemory
				? replContextMapManager_[pgId]->getElementSize() *
					  replContextMapManager_[pgId]->getFreeElementCount()
				: 0);

	return (txnContextUsage + activeTxnUsage + replContextUsage +
			replContextFree + txnContextFree + activeTxnFree + replContextFree);
}

/*!
	@brief Decide how to get context (for recovery)
*/
TransactionManager::GetMode TransactionManager::getContextGetModeForRecovery(
	GetMode decodedGetMode) const {
	if (decodedGetMode == AUTO) {
		return decodedGetMode;
	}
	else {
		return PUT;
	}
}
/*!
	@brief Decide transaction mode (for recovery)
*/
TransactionManager::TransactionMode
TransactionManager::getTransactionModeForRecovery(
	bool withBegin, bool isAutoCommit) const {
	assert(!(withBegin && isAutoCommit));
	if (isAutoCommit) {
		return AUTO_COMMIT;
	}
	else {
		return NO_AUTO_COMMIT_BEGIN_OR_CONTINUE;
	}
}

TransactionManager::Partition::Partition(PartitionId pId,
	int32_t txnTimeoutLimit, TransactionContextMap *txnContextMap,
	ActiveTransactionMap *activeTxnMap, ReplicationContextMap *replContextMap)
	: pId_(pId),
	  txnTimeoutLimit_(txnTimeoutLimit),
	  maxTxnId_(INITIAL_TXNID),
	  maxReplId_(INITIAL_REPLICATIONID),
	  txnContextMap_(txnContextMap),
	  activeTxnMap_(activeTxnMap),
	  replContextMap_(replContextMap),
	  reqTimeoutCount_(0),
	  txnTimeoutCount_(0),
	  replTimeoutCount_(0) {
}

TransactionManager::Partition::~Partition() {
	{
		ReplicationContextMap::Cursor cursor = replContextMap_->getCursor();
		for (ReplicationContext *replContext = cursor.next();
			 replContext != NULL; replContext = cursor.next()) {
			if (replContext->getPartitionId() == pId_) {
				const ReplicationContextKey key(
					pId_, replContext->getReplicationId());
				replContextMap_->remove(key);
			}
		}
	}
	{
		TransactionContextMap::Cursor cursor = txnContextMap_->getCursor();
		for (TransactionContext *txn = cursor.next(); txn != NULL;
			 txn = cursor.next()) {
			if (txn->getPartitionId() == pId_) {
				if (txn->isActive()) {
					const ActiveTransactionKey atKey(pId_, txn->getId());
					activeTxnMap_->remove(atKey);
				}
				txnContextMap_->remove(txn->getClientId());
			}
		}
	}
}

/*!
	@brief Creates transaction context
*/
TransactionContext &TransactionManager::Partition::put(
	util::StackAllocator &alloc, const ClientId &clientId,
	ContainerId containerId, StatementId stmtId, int32_t txnTimeoutInterval,
	const util::DateTime &now, EventMonotonicTime emNow, GetMode getMode,
	TransactionMode txnMode, bool isUpdateStmt, bool isRedo,
	TransactionId txnId) {
	try {
		if (txnTimeoutInterval < TXN_MIN_TRANSACTION_TIMEOUT_INTERVAL) {
			txnTimeoutInterval = TXN_STABLE_TRANSACTION_TIMEOUT_INTERVAL;
		}
		else if (txnTimeoutInterval > txnTimeoutLimit_) {
			txnTimeoutInterval = txnTimeoutLimit_;
		}

		const EventMonotonicTime newReqExpireTime =
			emNow +
			static_cast<EventMonotonicTime>(std::max(
				txnTimeoutInterval, TXN_STABLE_TRANSACTION_TIMEOUT_INTERVAL)) *
				1000;

		TransactionContext *txn = NULL;

		switch (getMode) {
		case CREATE:
			txn = txnContextMap_->get(clientId);
			if (txn != NULL) {
				GS_THROW_USER_ERROR(GS_ERROR_TM_SESSION_UUID_UNMATCHED, "");
			}
			else if (txnMode == NO_AUTO_COMMIT_CONTINUE) {
				GS_THROW_USER_ERROR(GS_ERROR_TM_TRANSACTION_MODE_INVALID, "");
			}
			else {
				txn = &txnContextMap_->create(clientId, newReqExpireTime);
				txn->clear();
				txn->set(clientId, pId_, containerId, newReqExpireTime,
					txnTimeoutInterval);
			}
			break;

		case GET:
			txn = txnContextMap_->get(clientId);
			if (txn == NULL) {
				TM_THROW_TXN_CONTEXT_NOT_FOUND(
					"(pId=" << pId_ << ", clientId=" << clientId
							<< ", getMode=" << TM_OUTPUT_GETMODE(getMode)
							<< ", txnMode=" << TM_OUTPUT_TXNMODE(txnMode)
							<< ")");
			}
			txn->contextExpireTime_ = newReqExpireTime;
			break;

		case AUTO:
			if (txnMode != AUTO_COMMIT) {
				GS_THROW_USER_ERROR(GS_ERROR_TM_TRANSACTION_MODE_INVALID, "");
			}
			txn = getAutoContext();
			txn->clear();
			txn->set(clientId, pId_, containerId, newReqExpireTime,
				txnTimeoutInterval);
			break;

		case PUT:  
			txn = txnContextMap_->get(clientId);
			if (txn == NULL) {
				txn = &txnContextMap_->create(clientId, newReqExpireTime);
				txn->clear();
				txn->set(clientId, pId_, containerId, newReqExpireTime,
					txnTimeoutInterval);
			}
			else {
				txn->contextExpireTime_ = newReqExpireTime;
			}
			break;

		default:
			GS_THROW_USER_ERROR(GS_ERROR_TM_CREATETION_MODE_INVALID, "");
		}

		assert(txn != NULL);

		txn->alloc_ = &alloc;

		txn->stmtStartTime_ = now;
		txn->stmtExpireTime_ = txn->stmtStartTime_;
		txn->stmtExpireTime_.addField(
			txn->txnTimeoutInterval_, util::DateTime::FIELD_SECOND);

		txn->isRedo_ = isRedo;

		const bool stmtIdCheckRequired = (getMode == GET);

		if (!isRedo && stmtIdCheckRequired) {

			checkStatementAlreadyExecuted(*txn, stmtId, isUpdateStmt);

			checkStatementContinuousInTransaction(
				*txn, stmtId, getMode, txnMode);
		}

		switch (txnMode) {
		case AUTO_COMMIT:
			if (!txn->isActive()) {
				txn->txnId_ = TransactionContext::AUTO_COMMIT_TXNID;
				txn->txnStartTime_ = emNow;
				txn->txnExpireTime_ =
					txn->txnStartTime_ +
					static_cast<EventMonotonicTime>(txn->txnTimeoutInterval_) *
						1000;
			}
			else {
				GS_THROW_USER_ERROR(GS_ERROR_TM_TRANSACTION_ALREADY_BEGIN,
					"(lastStmtId=" << txn->lastStmtId_
								   << ", txnId=" << txn->getId() << ")");
			}
			break;

		case NO_AUTO_COMMIT_BEGIN:
			if (!txn->isActive()) {
				const TransactionId assignedTxnId =
					(txnId == UNDEF_TXNID) ? assignNewTransactionId() : txnId;
				begin(*txn, assignedTxnId, emNow);
			}
			else {
			}
			break;

		case NO_AUTO_COMMIT_CONTINUE:
			if (!txn->isActive()) {
				GS_THROW_USER_ERROR(GS_ERROR_TM_TRANSACTION_NOT_FOUND,
					"(lastStmtId=" << txn->lastStmtId_ << ")");
			}
			break;

		case NO_AUTO_COMMIT_BEGIN_OR_CONTINUE:  
			if (!txn->isActive()) {
				const TransactionId assignedTxnId =
					(txnId == UNDEF_TXNID) ? assignNewTransactionId() : txnId;
				begin(*txn, assignedTxnId, emNow);
			}
			break;

		default:
			GS_THROW_USER_ERROR(GS_ERROR_TM_TRANSACTION_MODE_INVALID, "");
		}

		return *txn;
	}
	catch (StatementAlreadyExecutedException &e) {
		GS_RETHROW_CUSTOM_ERROR(StatementAlreadyExecutedException,
			GS_ERROR_DEFAULT, e,
			"Statement already executed "
				<< "(pId=" << pId_ << ", clientId=" << clientId
				<< ", sessionMode=" << TM_OUTPUT_GETMODE(getMode)
				<< ", txnMode=" << TM_OUTPUT_TXNMODE(txnMode)
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
	catch (ContextNotFoundException &) {
		throw;
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to operate session or transaction status "
			"(pId="
				<< pId_ << ", clientId=" << clientId
				<< ", sessionMode=" << TM_OUTPUT_GETMODE(getMode)
				<< ", txnMode=" << TM_OUTPUT_TXNMODE(txnMode)
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Gets transaction context
*/
TransactionContext &TransactionManager::Partition::get(
	util::StackAllocator &alloc, const ClientId &clientId) {
	try {
		TransactionContext *txn = txnContextMap_->get(clientId);
		if (txn != NULL) {
			txn->alloc_ = &alloc;
			return *txn;
		}
		else {
			TM_THROW_TXN_CONTEXT_NOT_FOUND(
				"(pId=" << pId_ << ", clientId=" << clientId << ")");
		}
	}
	catch (ContextNotFoundException &) {
		throw;
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to put session "
			"(pId="
				<< pId_ << ", clientId=" << clientId
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Removes transaction context
*/
void TransactionManager::Partition::remove(const ClientId &clientId) {
	try {
		txnContextMap_->remove(clientId);
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to remove session "
			"(pId="
				<< pId_ << ", clientId=" << clientId
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Updates last statement ID
*/
void TransactionManager::Partition::update(
	TransactionContext &txn, StatementId stmtId) {
	txn.lastStmtId_ = stmtId;
}
TransactionId TransactionManager::Partition::assignNewTransactionId() {
	return ++maxTxnId_;
}
/*!
	@brief Begins transaction
*/
void TransactionManager::Partition::begin(
	TransactionContext &txn, TransactionId txnId, EventMonotonicTime emNow) {
	try {
		const ActiveTransactionKey key(pId_, txnId);
		ActiveTransaction &activeTxn = activeTxnMap_->createNoExpire(key);
		activeTxn.clientId_ = txn.getClientId();
		txn.txnId_ = txnId;
		txn.state_ = TransactionContext::ACTIVE;
		txn.txnStartTime_ = emNow;
		txn.txnExpireTime_ =
			txn.txnStartTime_ +
			static_cast<EventMonotonicTime>(txn.txnTimeoutInterval_) * 1000;
		txnContextMap_->update(
			txn.getClientId(), txn.getTransactionExpireTime());
		if (txnId > maxTxnId_) {
			maxTxnId_ = txnId;
		}
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to begin transaction "
			"(pId="
				<< pId_ << ", clientId=" << txn.getClientId() << ", txnId="
				<< txnId << ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Commits transaction
*/
void TransactionManager::Partition::commit(TransactionContext &txn) {
	try {
		endTransaction(txn);
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to commit transaction "
			"(pId="
				<< pId_ << ", clientId=" << txn.getClientId()
				<< ", txnId=" << txn.getId()
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Aborts transaction
*/
void TransactionManager::Partition::abort(TransactionContext &txn) {
	try {
		endTransaction(txn);
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to abort transaction "
			"(pId="
				<< pId_ << ", clientId=" << txn.getClientId()
				<< ", txnId=" << txn.getId()
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Checks if transaction is active
*/
bool TransactionManager::Partition::isActiveTransaction(TransactionId txnId) {
	const ActiveTransactionKey key(pId_, txnId);
	return (activeTxnMap_->get(key) != NULL);
}
/*!
	@brief Gets transaction context ID (ClientID) list
*/
void TransactionManager::Partition::getTransactionContextId(
	util::XArray<ClientId> &clientIds) {
	try {
		TransactionContextMap::Cursor cursor = txnContextMap_->getCursor();
		for (TransactionContext *txn = cursor.next(); txn != NULL;
			 txn = cursor.next()) {
			if (txn->getPartitionId() == pId_) {
				clientIds.push_back(txn->getClientId());
			}
		}
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to check transaction active "
			"(pId="
				<< pId_ << ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Gets transaction context information on active transaction
*/
void TransactionManager::Partition::backupTransactionActiveContext(
	TransactionId &maxTxnId, util::XArray<ClientId> &clientIds,
	util::XArray<TransactionId> &activeTxnIds,
	util::XArray<ContainerId> &refContainerIds,
	util::XArray<StatementId> &lastStmtIds,
	util::XArray<int32_t> &txnTimeoutIntervalSec) {
	try {
		ActiveTransactionMap::Cursor cursor = activeTxnMap_->getCursor();
		for (ActiveTransaction *activeTxn = cursor.next(); activeTxn != NULL;
			 activeTxn = cursor.next()) {
			const TransactionContext *txn =
				txnContextMap_->get(activeTxn->clientId_);
			if (txn == NULL) {
				TM_THROW_TXN_CONTEXT_NOT_FOUND(
					"(pId=" << pId_ << ", clientId=" << activeTxn->clientId_
							<< ")");
			}
			if (txn->getPartitionId() == pId_) {
				assert(txn->isActive());
				clientIds.push_back(activeTxn->clientId_);
				activeTxnIds.push_back(txn->txnId_);
				refContainerIds.push_back(txn->containerId_);
				lastStmtIds.push_back(txn->lastStmtId_);
				txnTimeoutIntervalSec.push_back(txn->txnTimeoutInterval_);
			}
		}
		maxTxnId = maxTxnId_;
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to backup session "
			"(pId="
				<< pId_ << ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Restores transaction context information on active transaction
*/
void TransactionManager::Partition::restoreTransactionActiveContext(
	TransactionManager *manager, TransactionId maxTxnId, uint32_t numContext,
	const ClientId *clientIds, const TransactionId *activeTxnIds,
	const ContainerId *refContainerIds, const StatementId *lastStmtIds,
	const int32_t *txnTimeoutIntervalSec, EventMonotonicTime emNow) {
	try {
		for (uint32_t i = 0; i < numContext; i++) {
			const EventMonotonicTime reqTimeout =
				emNow +
				static_cast<EventMonotonicTime>(
					std::max(txnTimeoutIntervalSec[i],
						TXN_STABLE_TRANSACTION_TIMEOUT_INTERVAL)) *
					1000;
			TransactionContext &txn =
				txnContextMap_->create(clientIds[i], reqTimeout);
			txn.manager_ = manager;
			txn.clientId_ = clientIds[i];
			txn.pId_ = pId_;
			txn.containerId_ = refContainerIds[i];
			txn.lastStmtId_ = lastStmtIds[i];
			txn.txnTimeoutInterval_ = txnTimeoutIntervalSec[i];
			txn.contextExpireTime_ = reqTimeout;

			begin(txn, activeTxnIds[i], emNow);
		}
		maxTxnId_ = maxTxnId;
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to restore session "
			"(pId="
				<< pId_ << ", numContext=" << numContext << ", emNow=" << emNow
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Creates replication context
*/
ReplicationContext &TransactionManager::Partition::put(const ClientId &clientId,
	ContainerId containerId, int32_t stmtType, StatementId stmtId,
	NodeDescriptor ND, int32_t replTimeoutInterval, EventMonotonicTime emNow) {
	try {
		const ReplicationId replId = ++maxReplId_;
		const EventMonotonicTime replTimeout =
			emNow + static_cast<EventMonotonicTime>(replTimeoutInterval) * 1000;
		const ReplicationContextKey key(pId_, replId);
		ReplicationContext &replContext =
			replContextMap_->create(key, replTimeout);
		replContext.clear();
		replContext.pId_ = pId_;
		replContext.id_ = replId;
		replContext.clientId_ = clientId;
		replContext.containerId_ = containerId;
		replContext.stmtType_ = stmtType;
		replContext.stmtId_ = stmtId;
		replContext.clientNd_ = ND;
		replContext.timeout_ = replTimeout;
		return replContext;
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to put session "
			"(pId="
				<< pId_ << ", clientId=" << clientId
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Gets replication context
*/
ReplicationContext &TransactionManager::Partition::get(ReplicationId replId) {
	try {
		const ReplicationContextKey key(pId_, replId);
		ReplicationContext *replContext = replContextMap_->get(key);
		if (replContext != NULL) {
			return *replContext;
		}
		else {
			TM_THROW_REPL_CONTEXT_NOT_FOUND(
				"(pId=" << pId_ << ", replId=" << replId << ")");
		}
	}
	catch (ContextNotFoundException &) {
		throw;
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to put replication context "
			"(pId="
				<< pId_ << ", replId=" << replId
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Removes replication context
*/
void TransactionManager::Partition::remove(ReplicationId replId) {
	try {
		const ReplicationContextKey key(pId_, replId);
		replContextMap_->remove(key);
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to remove replication context "
			"(pId="
				<< pId_ << ", replId=" << replId
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}
/*!
	@brief Checks if stetement already executed
*/
void TransactionManager::Partition::checkStatementAlreadyExecuted(
	const TransactionContext &txn, StatementId stmtId,
	bool isUpdateStmt) const {
	if (stmtId <= txn.getLastStatementId() && isUpdateStmt) {
		TM_THROW_STATEMENT_ALREADY_EXECUTED(
			"(lastStmtId=" << txn.getLastStatementId() << ", stmtId=" << stmtId
						   << ")");
	}
}
/*!
	@brief Checks if statement is continuous in a transaction
*/
void TransactionManager::Partition::checkStatementContinuousInTransaction(
	const TransactionContext &txn, StatementId stmtId, GetMode getMode,
	TransactionMode txnMode) const {
	if (getMode == GET && stmtId > txn.getLastStatementId() &&
		stmtId - txn.getLastStatementId() > 1 &&
		txnMode == NO_AUTO_COMMIT_CONTINUE) {
		GS_THROW_USER_ERROR(GS_ERROR_TM_STATEMENT_INVALID,
			"(lastStmtId=" << txn.getLastStatementId() << ", stmtId=" << stmtId
						   << ")");
	}
}
uint64_t TransactionManager::Partition::getRequestTimeoutCount() const {
	return reqTimeoutCount_;
}
uint64_t TransactionManager::Partition::getTransactionTimeoutCount() const {
	return txnTimeoutCount_;
}
uint64_t TransactionManager::Partition::getReplicationTimeoutCount() const {
	return replTimeoutCount_;
}
TransactionContext *TransactionManager::Partition::getAutoContext() {
	return &autoContext_;
}
/*!
	@brief Ends transaction
*/
void TransactionManager::Partition::endTransaction(TransactionContext &txn) {
	try {
		const ActiveTransactionKey key(pId_, txn.getId());
		activeTxnMap_->remove(key);
		txn.state_ = TransactionContext::INACTIVE;
	}
	catch (std::exception &e) {
		GS_RETHROW_USER_OR_SYSTEM(e,
			"Failed to end transaction "
			"(pId="
				<< pId_ << ", txnId=" << txn.getId()
				<< ", reason=" << GS_EXCEPTION_MESSAGE(e) << ")");
	}
}

/*!
	@brief Locks partition object
*/
bool TransactionManager::lockPartition(PartitionId pId) {
	util::LockGuard<util::Mutex> lock(ptLockMutex_[pId % NUM_LOCK_MUTEX]);
	if (ptLock_[pId] == 0) {
		ptLock_[pId]++;
		return true;
	}
	else {
		return false;
	}
}

/*!
	@brief Unlocks partition object
*/
void TransactionManager::unlockPartition(PartitionId pId) {
	util::LockGuard<util::Mutex> lock(ptLockMutex_[pId % NUM_LOCK_MUTEX]);
	if (ptLock_[pId] == 1) {
		ptLock_[pId]--;
	}
}


TransactionManager::ConfigSetUpHandler TransactionManager::configSetUpHandler_;

/*!
	@brief Handler Operator
*/
void TransactionManager::ConfigSetUpHandler::operator()(ConfigTable &config) {
	CONFIG_TABLE_RESOLVE_GROUP(config, CONFIG_TABLE_TXN, "transaction");

	CONFIG_TABLE_ADD_SERVICE_ADDRESS_PARAMS(config, TXN, 10001);

	CONFIG_TABLE_ADD_PARAM(
		config, CONFIG_TABLE_TXN_NOTIFICATION_ADDRESS, STRING)
		.inherit(CONFIG_TABLE_ROOT_NOTIFICATION_ADDRESS);
	CONFIG_TABLE_ADD_PORT_PARAM(
		config, CONFIG_TABLE_TXN_NOTIFICATION_PORT, 31999);
	CONFIG_TABLE_ADD_PARAM(
		config, CONFIG_TABLE_TXN_NOTIFICATION_INTERVAL, INT32)
		.setUnit(ConfigTable::VALUE_UNIT_DURATION_S)
		.setMin(1)
		.setDefault(5);
	CONFIG_TABLE_ADD_PARAM(config, CONFIG_TABLE_TXN_REPLICATION_MODE, INT32)
		.setExtendedType(ConfigTable::EXTENDED_TYPE_ENUM)
		.addEnum(TransactionManager::REPLICATION_ASYNC, "ASYNC")
		.addEnum(TransactionManager::REPLICATION_SEMISYNC, "SEMISYNC")
		.setDefault(TransactionManager::REPLICATION_ASYNC);
	CONFIG_TABLE_ADD_PARAM(
		config, CONFIG_TABLE_TXN_REPLICATION_TIMEOUT_INTERVAL, INT32)
		.setUnit(ConfigTable::VALUE_UNIT_DURATION_S)
		.setMin(1)
		.setDefault(TXN_STABLE_TRANSACTION_TIMEOUT_INTERVAL);

	CONFIG_TABLE_ADD_PARAM(config, CONFIG_TABLE_TXN_CONNECTION_LIMIT, INT32)
		.setMin(3)
		.setMax(65536)
		.setDefault(5000);
	CONFIG_TABLE_ADD_PARAM(
		config, CONFIG_TABLE_TXN_TRANSACTION_TIMEOUT_LIMIT, INT32)
		.setUnit(ConfigTable::VALUE_UNIT_DURATION_S)
		.setMin(1)
		.setDefault(TXN_STABLE_TRANSACTION_TIMEOUT_INTERVAL);

	CONFIG_TABLE_ADD_PARAM(config, CONFIG_TABLE_TXN_STACK_MEMORY_LIMIT, INT32)
		.deprecate();
	CONFIG_TABLE_ADD_PARAM(config, CONFIG_TABLE_TXN_TOTAL_MEMORY_LIMIT, INT32)
		.alternate(CONFIG_TABLE_TXN_STACK_MEMORY_LIMIT)
		.setUnit(ConfigTable::VALUE_UNIT_SIZE_MB)
		.setMin(1)
		.setDefault(1024);
	CONFIG_TABLE_ADD_PARAM(config, CONFIG_TABLE_TXN_QUEUE_MEMORY_LIMIT, INT32)
		.deprecate();
	CONFIG_TABLE_ADD_PARAM(
		config, CONFIG_TABLE_TXN_TOTAL_MESSAGE_MEMORY_LIMIT, INT32)
		.alternate(CONFIG_TABLE_TXN_STACK_MEMORY_LIMIT)
		.setUnit(ConfigTable::VALUE_UNIT_SIZE_MB)
		.setMin(1)
		.setDefault(1024);
	CONFIG_TABLE_ADD_PARAM(config, CONFIG_TABLE_TXN_USE_KEEPALIVE, BOOL)
		.setExtendedType(ConfigTable::EXTENDED_TYPE_LAX_BOOL)
		.setDefault(true);
	CONFIG_TABLE_ADD_PARAM(config, CONFIG_TABLE_TXN_KEEPALIVE_IDLE, INT32)
		.setUnit(ConfigTable::VALUE_UNIT_DURATION_S)
		.setMin(0)
		.setDefault(600);
	CONFIG_TABLE_ADD_PARAM(config, CONFIG_TABLE_TXN_KEEPALIVE_INTERVAL, INT32)
		.setUnit(ConfigTable::VALUE_UNIT_DURATION_S)
		.setMin(0)
		.setDefault(60);
	CONFIG_TABLE_ADD_PARAM(config, CONFIG_TABLE_TXN_KEEPALIVE_COUNT, INT32)
		.setMin(0)
		.setDefault(5);
}
