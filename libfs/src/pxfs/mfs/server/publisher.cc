#include "pxfs/mfs/server/publisher.h"
#include "osd/main/server/stsystem.h"
#include "osd/main/server/session.h"
#include "osd/main/server/shbuf.h"
#include "osd/main/server/verifier.h"
#include "osd/containers/byte/verifier.h"
#include "pxfs/server/session.h"
#include "pxfs/mfs/server/dir_inode.h"
#include "pxfs/mfs/server/file_inode.h"
#include "common/errno.h"

namespace server {

int
Publisher::Register(::osd::server::StorageSystem* stsystem)
{
	stsystem->publisher()->RegisterOperation(::Publisher::Message::LogicalOperation::kMakeFile, Publisher::MakeFile);
	stsystem->publisher()->RegisterOperation(::Publisher::Message::LogicalOperation::kMakeDir, Publisher::MakeDir);
	stsystem->publisher()->RegisterOperation(::Publisher::Message::LogicalOperation::kLink, Publisher::Link);
	stsystem->publisher()->RegisterOperation(::Publisher::Message::LogicalOperation::kUnlink, Publisher::Unlink);
	stsystem->publisher()->RegisterOperation(::Publisher::Message::LogicalOperation::kWrite, Publisher::Write);
	return E_SUCCESS;
}


template<typename T>
T* LoadLogicalOperation(::osd::server::OsdSession* session, char* buf)
{
	int                                               n;
	T*                                                lgc_op;
	::osd::server::OsdSharedBuffer*                   shbuf = session->shbuf_;
	osd::Publisher::Message::LogicalOperationHeader* header = osd::Publisher::Message::LogicalOperationHeader::Load(buf);
	n = sizeof(*lgc_op) - sizeof(*header);
	if (shbuf->Read(&buf[sizeof(*header)], n) < n) {
		return NULL;
	}
	lgc_op = T::Load(buf);
	return lgc_op;
}


int
Publisher::MakeFile(::osd::server::OsdSession* osdsession, char* buf, 
                    ::osd::Publisher::Message::BaseMessage* next)
{
	int                   ret;
	osd::common::ObjectId oid;
	DirInode              dinode;
	FileInode             finode;
	Session*              session = static_cast<Session*>(osdsession);
	
	::Publisher::Message::LogicalOperation::MakeFile* lgc_op = LoadLogicalOperation< ::Publisher::Message::LogicalOperation::MakeFile>(session, buf);
	
	// verify preconditions
	oid = osd::common::ObjectId(lgc_op->parino_);
	if ((ret = lock_verifier_->VerifyLock(session, oid)) < 0) {
		return ret;
	}

	// do the operation 
	if (Inode::type(lgc_op->parino_) != kDirInode) {
		return -1;
	}
	DirInode* pp = DirInode::Load(session, lgc_op->parino_, &dinode);
	FileInode* cp = FileInode::Make(session, lgc_op->childino_, &finode);
	pp->Link(session, lgc_op->name_, cp);
	
	return E_SUCCESS;
}


int
Publisher::MakeDir(::osd::server::OsdSession* osdsession, char* buf, 
                   ::osd::Publisher::Message::BaseMessage* next)
{
	int                   ret;
	osd::common::ObjectId oid;
	DirInode              dinode1;
	DirInode              dinode2;
	Session*              session = static_cast<Session*>(osdsession);

	::Publisher::Message::LogicalOperation::MakeDir* lgc_op = LoadLogicalOperation< ::Publisher::Message::LogicalOperation::MakeDir>(session, buf);
	
	// verify preconditions
	oid = osd::common::ObjectId(lgc_op->parino_);
	if ((ret = lock_verifier_->VerifyLock(session, oid)) < 0) {
		return ret;
	}

	// do the operation 
	if (Inode::type(lgc_op->parino_) != kDirInode) {
		return -E_VRFY;
	}
	DirInode* pp = DirInode::Load(session, lgc_op->parino_, &dinode1);
	DirInode* cp = DirInode::Make(session, lgc_op->childino_, &dinode2);
	if ((ret = pp->Link(session, lgc_op->name_, cp)) < 0) { return ret; }
	if ((ret = cp->Link(session, ".", cp)) < 0) { return ret; }
	if ((ret = cp->Link(session, "..", pp)) < 0) { return ret; }

	return E_SUCCESS;
}




int
Publisher::Link(::osd::server::OsdSession* osdsession, char* buf, 
                ::osd::Publisher::Message::BaseMessage* next)
{
	int                   ret;
	osd::common::ObjectId oid;
	DirInode              dinode;
	Session*              session = static_cast<Session*>(osdsession);

	::Publisher::Message::LogicalOperation::Link* lgc_op = LoadLogicalOperation< ::Publisher::Message::LogicalOperation::Link>(session, buf);
	
	// verify preconditions
	oid = osd::common::ObjectId(lgc_op->parino_);
	if ((ret = lock_verifier_->VerifyLock(session, oid)) < 0) {
		return ret;
	}

	// do the operation 
	if (Inode::type(lgc_op->parino_) != kDirInode) {
		return -1;
	}
	DirInode* dp = DirInode::Load(session, lgc_op->parino_, &dinode);
	return dp->Link(session, lgc_op->name_, lgc_op->childino_);
}


int
Publisher::Unlink(::osd::server::OsdSession* osdsession, char* buf, 
                  ::osd::Publisher::Message::BaseMessage* next)
{
	int                   ret;
	osd::common::ObjectId oid;
	DirInode              dinode;
	InodeNumber           child_ino;
	Session*              session = static_cast<Session*>(osdsession);
	
	::Publisher::Message::LogicalOperation::Unlink* lgc_op = LoadLogicalOperation< ::Publisher::Message::LogicalOperation::Unlink>(session, buf);
	
	// verify preconditions
	oid = osd::common::ObjectId(lgc_op->parino_);
	if ((ret = lock_verifier_->VerifyLock(session, oid)) < 0) {
		return ret;
	}

	// do the operation 
	DirInode* dp = DirInode::Load(session, lgc_op->parino_, &dinode);
	return dp->Unlink(session, lgc_op->name_);
}



// buffer buf already holds the logical operation header
int
Publisher::Write(::osd::server::OsdSession* session, char* buf, 
                 ::osd::Publisher::Message::BaseMessage* next)
{
	int                   ret;
	osd::common::ObjectId oid;

	::Publisher::Message::LogicalOperation::Write* lgc_op = LoadLogicalOperation< ::Publisher::Message::LogicalOperation::Write>(session, buf);
	
	// verify preconditions
	oid = osd::common::ObjectId(lgc_op->ino_);
	if ((ret = lock_verifier_->VerifyLock(session, oid)) < 0) {
		return ret;
	}
	// verify each container operation and then apply it
	return write_verifier_->Parse(session, next);
}


int
Publisher::Init()
{
	lock_verifier_ = new LockVerifier();
	write_verifier_ = new WriteVerifier();
	return E_SUCCESS;
}


WriteVerifier* Publisher::write_verifier_ = NULL;
LockVerifier*  Publisher::lock_verifier_ = NULL;

} // namespace server