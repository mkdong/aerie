#ifndef _MFS_CLIENT_SUPERBLOCK_H_KAJ911
#define _MFS_CLIENT_SUPERBLOCK_H_KAJ911

#include "client/backend.h"
#include <pthread.h>
#include "mfs/client/dir_inode.h"
#include "mfs/psb.h"

namespace mfs {

class SuperBlock: public client::SuperBlock {
public:

	SuperBlock(client::Session* session, PSuperBlock<client::Session>* psb)
		: psb_(psb),
		  root_(this, NULL)
	{ 
		root_.Init(session, psb->root_);
		root_.SetSuperBlock(this);
		imap_ = new client::InodeMap();
		pthread_mutex_init(&mutex_, NULL);
		printf("Superblock: this=%p\n", this);
		printf("Superblock: root_=%p\n", &root_);
	}

	SuperBlock(client::Session* session)
		: root_(this, NULL)
	{ 
		imap_ = new client::InodeMap();
		pthread_mutex_init(&mutex_, NULL);
	}


	client::Inode* GetRootInode() {
		return &root_;
	}

	client::Inode* CreateImmutableInode(int t);
	client::Inode* WrapInode();
	int AllocInode(client::Session* session, int type, client::Inode** ipp);
	int GetInode(client::InodeNumber ino, client::Inode** ipp);
	int PutInode(client::Inode* ip);

	void* GetPSuperBlock() { return (void*) psb_; }

private:
	DirInodeMutable               root_;
	PSuperBlock<client::Session>* psb_;
	pthread_mutex_t               mutex_;
	client::InodeMap*             imap_;
};


} // namespace mfs

#endif /* _MFS_CLIENT_SUPERBLOCK_H_KAJ911 */
