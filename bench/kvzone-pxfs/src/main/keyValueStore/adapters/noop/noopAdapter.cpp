/*
 * Copyright (c) 2010 NEC Laboratories, Inc. All rights reserved.
 */ 
#include <tcutil.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "noopAdapter.h"
#include </scratch/nvm/stamnos/libfs/src/pxfs/client/c_api.h>

using namespace keyValueStore::adapters::noopAdapter;

static boost::mutex noop_mutex;

NoopAdapter::NoopAdapter(SyncKeyValueStoreConfigs const& config)
    : SyncKeyValueStore(config)
{
	libfs_init2("10000");
	libfs_mount("/tmp/stamnos_pool", "/pxfs", "mfs", 0);
	//libfs_chdir("/pxfs");
}

NoopAdapter::~NoopAdapter()
{
	libfs_shutdown();
}

void NoopAdapter::commit()
{
}

void NoopAdapter::insert(InsertRequest& r)
{
    char keyBuf[16];
    r.getKey().serialize(keyBuf);

    uint32 valSize = r.getValue().getSerializedSize();
    char valBuf[valSize];
    r.getValue().serialize(valBuf);

    //cout << keyBuf << " " << valBuf << endl;

    int fd;
    boost::mutex::scoped_lock debug_lock(noop_mutex);
    fd = libfs_open(keyBuf, O_CREAT | O_RDWR);
    if(fd <= 0)
	goto ret_in;
    libfs_write(fd, valBuf, valSize);
    libfs_close(fd);

ret_in:
    r.getSuccessCallback()(&(r.getKey()));
}

void NoopAdapter::lookup(LookupRequest& r)
{
    char keyBuf[16];
    r.getKey().serialize(keyBuf);

    char valBuf[4096];
    int fd;

    boost::mutex::scoped_lock debug_lock(noop_mutex);
    fd = libfs_open(keyBuf, O_CREAT | O_RDWR);
    if(fd <= 0)
	goto ret_lo;
    libfs_read(fd, valBuf, 4096);
    libfs_close(fd);

ret_lo:
    r.getSuccessCallback()(&(r.getKey()), FOUND);
}

void NoopAdapter::remove(RemoveRequest& r)
{
    char keyBuf[16];
    r.getKey().serialize(keyBuf);

    boost::mutex::scoped_lock debug_lock(noop_mutex);
    libfs_unlink(keyBuf);
    r.getSuccessCallback()(&(r.getKey()));
}

void NoopAdapter::processRequests(char* , SafeKVStoreQueue & requestQueue, boost::mutex::scoped_lock &queueLock)
{
    DLOG(1, " NoopAdapter processRequests ");
    AbstractKVStoreRequest* req = requestQueue.pop();
    queueLock.unlock();
    if (req == NULL) 
    {
        return;
    }

    switch (req->getType())
    {
        case INSERT:
            {
                DLOG(1, " NoopAdapter InsertRequest ");
                InsertRequest& r = dynamic_cast<InsertRequest&>(*req);
		this->insert(r);
                //r.getSuccessCallback()(&(r.getKey()));
            }
            break;
        case REMOVE:
            {
                DLOG(1, " NoopAdapter RemoveRequest ");
                RemoveRequest& r = dynamic_cast<RemoveRequest&>(*req);
                //r.getSuccessCallback()(&(r.getKey()));
		this->remove(r);
            }
            break;
        case LOOKUP:
            {
                DLOG(1, " NoopAdapter LookupRequest ");
                LookupRequest& r = dynamic_cast<LookupRequest&>(*req);
                //r.getSuccessCallback()(&(r.getKey()), NOT_FOUND);
		this->lookup(r);
            }
            break;
        case RESET:
            {
                DLOG(1, " NoopAdapter ResetRequest ");
                ResetRequest& r = dynamic_cast<ResetRequest&>(*req);
                r.getSuccessCallback()();
            }
        default:
            FAILURE("unknown request type");
            break;
    }
    delete req;
}
