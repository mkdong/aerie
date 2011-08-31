#ifndef _SERVER_API_H_AJK192
#define _SERVER_API_H_AJK192

enum {
	RPC_SERVER_IS_ALIVE = 22,
	RPC_CHUNK_CREATE,
	RPC_CHUNK_DELETE,
	RPC_CHUNK_ACCESS,
	RPC_CHUNK_RELEASE,
	RPC_NAME_LOOKUP,
	RPC_NAME_LINK,
	RPC_NAME_UNLINK,
	RPC_REGISTRY_LOOKUP,
	RPC_REGISTRY_ADD,
	RPC_REGISTRY_REMOVE,
	RPC_NAMESPACE_MOUNT,
};

#endif
