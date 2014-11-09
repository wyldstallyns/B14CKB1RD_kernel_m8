#ifndef __FS_CEPH_MESSENGER_H
#define __FS_CEPH_MESSENGER_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/radix-tree.h>
#include <linux/uio.h>
#include <linux/workqueue.h>

#include "types.h"
#include "buffer.h"

struct ceph_msg;
struct ceph_connection;

struct ceph_connection_operations {
	struct ceph_connection *(*get)(struct ceph_connection *);
	void (*put)(struct ceph_connection *);

	
	void (*dispatch) (struct ceph_connection *con, struct ceph_msg *m);


	struct ceph_auth_handshake *(*get_authorizer) (
				struct ceph_connection *con,
			       int *proto, int force_new);
	int (*verify_authorizer_reply) (struct ceph_connection *con, int len);
	int (*invalidate_authorizer)(struct ceph_connection *con);

	
	
	void (*fault) (struct ceph_connection *con);

	void (*peer_reset) (struct ceph_connection *con);

	struct ceph_msg * (*alloc_msg) (struct ceph_connection *con,
					struct ceph_msg_header *hdr,
					int *skip);
};

#define ENTITY_NAME(n) ceph_entity_type_name((n).type), le64_to_cpu((n).num)

struct ceph_messenger {
	struct ceph_entity_inst inst;    
	struct ceph_entity_addr my_enc_addr;

	atomic_t stopping;
	bool nocrc;

	u32 global_seq;
	spinlock_t global_seq_lock;

	u32 supported_features;
	u32 required_features;
};

struct ceph_msg {
	struct ceph_msg_header hdr;	
	struct ceph_msg_footer footer;	
	struct kvec front;              
	struct ceph_buffer *middle;
	struct page **pages;            
	unsigned nr_pages;              
	unsigned page_alignment;        
	struct ceph_pagelist *pagelist; 

	struct ceph_connection *con;
 	struct list_head list_head;

	struct kref kref;
	struct bio  *bio;		
	struct bio  *bio_iter;		
	int bio_seg;			
	struct ceph_pagelist *trail;	
	bool front_is_vmalloc;
	bool more_to_follow;
	bool needs_out_seq;
	int front_max;
	unsigned long ack_stamp;        

	struct ceph_msgpool *pool;
};

struct ceph_msg_pos {
	int page, page_pos;  
	int data_pos;        
	bool did_page_crc;   
};

#define BASE_DELAY_INTERVAL	(HZ/2)
#define MAX_DELAY_INTERVAL	(5 * 60 * HZ)



struct ceph_connection {
	void *private;

	const struct ceph_connection_operations *ops;

	struct ceph_messenger *msgr;

	atomic_t sock_state;
	struct socket *sock;
	struct ceph_entity_addr peer_addr; /* peer address */
	struct ceph_entity_addr peer_addr_for_me;

	unsigned long flags;
	unsigned long state;	
	const char *error_msg;  


	struct ceph_entity_name peer_name; 

	unsigned peer_features;
	u32 connect_seq;      
	u32 peer_global_seq;  

	int auth_retry;       
	void *auth_reply_buf;   
	int auth_reply_buf_len;

	struct mutex mutex;

	
	struct list_head out_queue;
	struct list_head out_sent;   
	u64 out_seq;		     

	u64 in_seq, in_seq_acked;  

	
	char in_banner[CEPH_BANNER_MAX_LEN];
	struct ceph_msg_connect out_connect;
	struct ceph_msg_connect_reply in_reply;
	struct ceph_entity_addr actual_peer_addr;

	
	struct ceph_msg *out_msg;        
	bool out_msg_done;
	struct ceph_msg_pos out_msg_pos;

	struct kvec out_kvec[8],         
		*out_kvec_cur;
	int out_kvec_left;   
	int out_skip;        
	int out_kvec_bytes;  
	bool out_kvec_is_msg; 
	int out_more;        
	__le64 out_temp_ack; 

	
	struct ceph_msg_header in_hdr;
	struct ceph_msg *in_msg;
	struct ceph_msg_pos in_msg_pos;
	u32 in_front_crc, in_middle_crc, in_data_crc;  

	char in_tag;         
	int in_base_pos;     
	__le64 in_temp_ack;  

	struct delayed_work work;	    
	unsigned long       delay;          
};


extern const char *ceph_pr_addr(const struct sockaddr_storage *ss);
extern int ceph_parse_ips(const char *c, const char *end,
			  struct ceph_entity_addr *addr,
			  int max_count, int *count);


extern int ceph_msgr_init(void);
extern void ceph_msgr_exit(void);
extern void ceph_msgr_flush(void);

extern void ceph_messenger_init(struct ceph_messenger *msgr,
			struct ceph_entity_addr *myaddr,
			u32 supported_features,
			u32 required_features,
			bool nocrc);

extern void ceph_con_init(struct ceph_connection *con, void *private,
			const struct ceph_connection_operations *ops,
			struct ceph_messenger *msgr);
extern void ceph_con_open(struct ceph_connection *con,
			  __u8 entity_type, __u64 entity_num,
			  struct ceph_entity_addr *addr);
extern bool ceph_con_opened(struct ceph_connection *con);
extern void ceph_con_close(struct ceph_connection *con);
extern void ceph_con_send(struct ceph_connection *con, struct ceph_msg *msg);

extern void ceph_msg_revoke(struct ceph_msg *msg);
extern void ceph_msg_revoke_incoming(struct ceph_msg *msg);

extern void ceph_con_keepalive(struct ceph_connection *con);

extern struct ceph_msg *ceph_msg_new(int type, int front_len, gfp_t flags,
				     bool can_fail);
extern void ceph_msg_kfree(struct ceph_msg *m);


static inline struct ceph_msg *ceph_msg_get(struct ceph_msg *msg)
{
	kref_get(&msg->kref);
	return msg;
}
extern void ceph_msg_last_put(struct kref *kref);
static inline void ceph_msg_put(struct ceph_msg *msg)
{
	kref_put(&msg->kref, ceph_msg_last_put);
}

extern void ceph_msg_dump(struct ceph_msg *msg);

#endif
