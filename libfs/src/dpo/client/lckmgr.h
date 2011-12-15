/// \file lckmgr.h 
///
/// \brief Client lock manager interface.

#ifndef __STAMNOS_DPO_CLIENT_LOCK_MANAGER_H
#define __STAMNOS_DPO_CLIENT_LOCK_MANAGER_H

#include <string>
#include <google/sparsehash/sparseconfig.h>
#include <google/dense_hash_map>
#include <google/dense_hash_set>
#include <set>
#include <boost/functional/hash.hpp>
#include <boost/lexical_cast.hpp>
#include "rpc/rpc.h"
#include "dpo/common/gtque.h"
#include "dpo/common/lock_protocol.h"
#include "common/bitmap.h"

namespace dpo {
namespace cc {
namespace client {


// SUGGESTED LOCK CACHING IMPLEMENTATION PLAN:
//
// All the requests on the server run till completion and threads wait 
// on condition variables on the client to wait for a lock. This allows 
// the server to:
//  - be replicated using the replicated state machine approach (Schneider 1990)
//  - serve requests using a single core in an event-based fashion
//
// On the client a lock can be in several states:
//  - free: client owns the lock (exclusively) and no thread has it
//  - locked: client owns the lock (exclusively) and a thread has it
//  - acquiring: the client is acquiring lock
//  - releasing: the client is releasing ownership
//
// in the state acquiring and xlocked there may be several threads
// waiting for the lock, but the first thread in the list interacts
// with the server and wakes up the threads when its done (released
// the lock).  a thread in the list is identified by its thread id
// (tid).
//
// a thread is in charge of getting a lock: if the server cannot grant
// it the lock, the thread will receive a retry reply.  at some point
// later, the server sends the thread a retry RPC, encouraging the client
// thread to ask for the lock again.
//
// once a thread has acquired a lock, its client obtains ownership of
// the lock. the client can grant the lock to other threads on the client 
// without interacting with the server. 
//
// the server must send the client a revoke request to get the lock back. this
// request tells the client to send the lock back to the
// server when the lock is released or right now if no thread on the
// client is holding the lock.  when receiving a revoke request, the
// client adds it to a list and wakes up a releaser thread, which returns
// the lock the server as soon it is free.
//
// the releasing is done in a separate a thread to avoid
// deadlocks and to ensure that revoke and retry RPCs from the server
// run to completion (i.e., the revoke RPC cannot do the release when
// the lock is free.
//
// a challenge in the implementation is that retry and revoke requests
// can be out of order with the acquire and release requests. That
// is, a client may receive a revoke request before it has received
// the positive acknowledgement on its acquire request.  Similarly, a
// client may receive a retry before it has received a response on its
// initial acquire request.  A flag field is used to record if a retry
// has been received.
//


// ISSUES to be filed in the issue tracker
//
// include extra data structure outstanding_locks_to keep track of outstanding lock 
// requests to speeden up lookup of outstanind locks. the downside of including 
// such a data structure is that it is placed in the critical path of lock acquisition
// (but since lock acquisition involves an RPC, we expect the overhead of the data
// structure to be relatively small)
//
// 
// currently we serialize access to the lock state through the lock manager global
// lock. we expect this to be okay as public locks are heavyweight (involve RPC)
// consider using fine grain mutex (one per lock) if we see contention.

typedef uint16_t LockType;

class LockId {
	enum {
		LOCK_NUMBER_LEN_LOG2 = 48
	};
public:
	LockId(uint64_t u64 = 0)
		: u64_(u64)
	{ }

	LockId(const LockId& lid)
		: u64_(lid.u64_)
	{ }

	LockId(LockType type_id, uint64_t num) {
		Init(type_id, num);
	}

	LockType type() const {
		return u16_[3];
	}

	uint64_t number() const {
		return u64_ & ((1LLU << LOCK_NUMBER_LEN_LOG2) - 1);
	}

	bool operator==(const LockId& other) const {
		return (u64_ == other.u64_);
	}

	bool operator!=(const LockId& other) const {
		return !(*this == other);
	}

	lock_protocol::LockId marshall() const {
		return u64_;
	}

	std::string string() const {
		return boost::lexical_cast<std::string>(type()) + 
			   std::string(".") + 
			   boost::lexical_cast<std::string>(number());
	}

	const char* c_str() const {
		return string().c_str();
	}
private:
	void Init(LockType type_id, uint64_t num) {
		u64_ = type_id;
		u64_ = u64_ << LOCK_NUMBER_LEN_LOG2;
		u64_ = u64_ | num;
	}

	union {
		uint64_t u64_;  
		uint16_t u16_[4];
	};
};

struct LockIdHashFcn {
	std::size_t operator()(const LockId& lid) const {
		boost::hash<uint64_t> hasher;
		return hasher(lid.marshall());
	}
};


class ThreadRecord {
public:
	typedef pthread_t            id_t;
	typedef lock_protocol::Mode  Mode;

	ThreadRecord();
	ThreadRecord(id_t, Mode);

	id_t id() const { return tid_; };
	Mode mode() const { return mode_; };
	void set_mode(Mode mode) { mode_ = mode; };

private:
	id_t tid_;
	Mode mode_;
};


class Lock {

public:
	enum LockStatus {
		NONE, 
		FREE, 
		LOCKED, 
		ACQUIRING, 
		/* RELEASING (unused) */
	};

	enum Flag {
		FLG_NOBLK = lock_protocol::FLG_NOQUE, // don't block if can't grant lock
		FLG_CAPABILITY = lock_protocol::FLG_CAPABILITY
	};

	Lock(LockId);
	~Lock();

	// changes the status of this lock
	void set_status(LockStatus);
	LockStatus status() const { return status_; }
	LockId lid() const { return lid_; }
	lock_protocol::Mode mode(pthread_t tid) { 
		ThreadRecord* t = gtque_.Find(tid);
		return (t != NULL) ? t->mode(): lock_protocol::Mode(lock_protocol::Mode::NL);
	}

	LockId                    lid_;

	/// we use only a single cv to monitor changes of the lock's status
	/// this may be less efficient because there would be many spurious
	/// wake-ups, but it's simple anyway
	pthread_cond_t            status_cv_;

	/// condvar that is signaled when the ``used'' field is set to true
	pthread_cond_t            used_cv_;

	pthread_cond_t            got_acq_reply_cv_;
  
	/// condvar that is signaled when the server informs the client to retry
	pthread_cond_t            retry_cv_;

	GrantQueue<ThreadRecord>  gtque_; 
	
	/// The sequence number of the latest *COMPLETED* acquire request made
	/// by the client to obtain this lock.
	/// By completed, we mean the remote acquire() call returns with a value.
	int                       seq_;
	bool                      used_;        ///< set to true after first use
	bool                      can_retry_;   ///< set when a retry message from the server is received
	int                       revoke_type_; ///< type of revocation requested
	lock_protocol::Mode       public_mode_; ///< mode as known by the server and seen by the world
	void*                     payload_;     ///< lock users may use it for anything they like
	bool                      cancel_;      ///< cancel outstanding request
private:
	LockStatus                status_;
};


// Classes that inherit LockUser can override callback functions to 
// be notified of lock events
class LockUser {
public:
	virtual void OnRelease(Lock*) = 0;
	virtual void OnConvert(Lock*) = 0;
	virtual int Revoke(Lock* lock, lock_protocol::Mode mode) = 0;
	virtual ~LockUser() {};
};


class LockManager {
	enum {
		LOCK_TYPE_COUNT = 2
	};
	typedef google::dense_hash_map<LockId, Lock*, LockIdHashFcn> LockMap;
	typedef google::dense_hash_map<LockId, int, LockIdHashFcn>  RevokeMap;
public:
	LockManager(rpcc* rpc_client, rpcs* rpc_server, std::string id);
	~LockManager();
	Lock* FindLock(LockId lid);
	Lock* FindOrCreateLock(LockId lid);
	lock_protocol::status Acquire(Lock* lock, lock_protocol::Mode::Set mode_set, int flags, int argc, void** argv, lock_protocol::Mode& mode_granted);
	lock_protocol::status Acquire(Lock* lock, lock_protocol::Mode::Set mode_set, int flags, lock_protocol::Mode& mode_granted);
	lock_protocol::status Acquire(LockId lid, lock_protocol::Mode::Set mode_set, int flags, int argc, void** argv, lock_protocol::Mode& mode_granted);
	lock_protocol::status Acquire(LockId lid, lock_protocol::Mode::Set mode_set, int flags, lock_protocol::Mode& mode_granted);
	lock_protocol::status Convert(Lock* lock, lock_protocol::Mode new_mode, bool synchronous = false);
	lock_protocol::status Convert(LockId lid, lock_protocol::Mode new_mode, bool synchronous = false);
	lock_protocol::status Release(Lock* lock, bool synchronous = false);
	lock_protocol::status Release(LockId lid, bool synchronous = false);
	lock_protocol::status Cancel(Lock* l);
	lock_protocol::status Cancel(LockId lid);
	lock_protocol::status stat(LockId lid);
	void Releaser();
	void ShutdownReleaser();
	void RegisterLockUser(LockType type, LockUser* lu);
	void UnregisterLockUser(LockType type);

	rlock_protocol::status revoke(lock_protocol::LockId, int seq, int revoke_type, int& unused);
	rlock_protocol::status retry(lock_protocol::LockId, int seq, int& current_seq);

	int id() { return cl2srv_->id(); }

private:
	int do_acquire(Lock* l, lock_protocol::Mode::Set mode_set, int flags, int argc, void** argv, lock_protocol::Mode& mode_granted);
	int do_convert(Lock* l, lock_protocol::Mode mode, int flags);
	int do_release(Lock* l, int flags);
	Lock* FindLockInternal(LockId lid);
	Lock* FindOrCreateLockInternal(LockId lid);
	lock_protocol::status AcquireInternal(unsigned long tid, Lock* l, lock_protocol::Mode::Set mode_set, int flags, int argc, void** argv, lock_protocol::Mode& mode_granted);
	lock_protocol::status ConvertInternal(unsigned long tid, Lock* l, lock_protocol::Mode new_mode, bool synchronous);
	lock_protocol::status ReleaseInternal(unsigned long tid, Lock* e, bool synchronous);
	lock_protocol::Mode SelectMode(Lock* l, lock_protocol::Mode::Set mode_set);
	lock_protocol::status CancelLockRequestInternal(Lock* l);

	class LockUser*            lu_[LOCK_TYPE_COUNT];
	std::string                hostname_;
	std::string                id_;
	/// the RPC object through which we receive callbacks from the server
	rpcs*                      srv2cl_;
	/// the RPC object through which we make calls to the server
	rpcc*                      cl2srv_;

	int                        last_seq_;
	volatile bool              releaser_thread_running_;

	/// locks known to this lock manager
	LockMap                    locks_;

	// global lock
	pthread_mutex_t            mutex_;
	// key: lock id; value: seq no. of the corresponding acquire
	RevokeMap                  revoke_map_;
	// controls access to the revoke_map
	pthread_mutex_t            revoke_mutex_;
	pthread_cond_t             revoke_cv;
	pthread_t                  releasethread_th_;
};


} // namespace client
} // namespace cc
} // namespace dpo


namespace client {
	typedef ::dpo::cc::client::Lock        Lock;
	typedef ::dpo::cc::client::LockManager LockManager;
} // namespace client

#endif // __STAMNOS_DPO_CLIENT_LOCK_MANAGER_H