#ifndef SNIFF_PROC_CLASS_H
#define SNIFF_PROC_CLASS_H


#include <unistd.h>
#include "sniff.h"
#include "calltable.h"


#define MAX_TCPSTREAMS 1024

class TcpReassemblySip {
public:
	struct tcp_stream_packet {
		packet_s_process *packetS;
		time_t ts;
		u_int32_t seq;
		u_int32_t next_seq;
		u_int32_t ack_seq;
		tcp_stream_packet *next;
		int lastpsh;
	};
	struct tcp_stream {
		tcp_stream() {
			packets = NULL;
			complete_data = NULL;
			last_ts = 0;
			last_seq = 0;
			last_ack_seq = 0;
		}
		tcp_stream_packet* packets;
		SimpleBuffer* complete_data;
		time_t last_ts;
		u_int32_t last_seq;
		u_int32_t last_ack_seq;
	};
	struct tcp_stream_id {
		tcp_stream_id(u_int32_t saddr = 0, u_int16_t source = 0, 
			      u_int32_t daddr = 0, u_int16_t dest = 0) {
			this->saddr = saddr;
			this->source = source;
			this->daddr = daddr; 
			this->dest = dest;
		}
		u_int32_t saddr;
		u_int16_t source;
		u_int32_t daddr;
		u_int16_t dest;
		bool operator < (const tcp_stream_id& other) const {
			return((this->saddr < other.saddr) ? 1 : (this->saddr > other.saddr) ? 0 :
			       (this->source < other.source) ? 1 : (this->source > other.source) ? 0 :
			       (this->daddr < other.daddr) ? 1 : (this->daddr > other.daddr) ? 0 :
			       (this->dest < other.dest));
		}
	};
public:
	void processPacket(packet_s_process **packetS_ref, class PreProcessPacket *processPacket);
	void clean(time_t ts = 0);
private:
	bool addPacket(tcp_stream *stream, packet_s_process **packetS_ref, PreProcessPacket *processPacket);
	void complete(tcp_stream *stream, tcp_stream_id id, PreProcessPacket *processPacket);
	tcp_stream_packet *getLastStreamPacket(tcp_stream *stream) {
		if(!stream->packets) {
			return(NULL);
		}
		tcp_stream_packet *packet = stream->packets;
		while(packet->next) {
			packet = packet->next;
		}
		return(packet);
	}
	bool isCompleteStream(tcp_stream *stream) {
		if(!stream->packets) {
			return(false);
		}
		int data_len;
		u_char *data;
		if(stream->complete_data) {
			data_len = stream->complete_data->size();
			data = stream->complete_data->data();
		} else {
			data_len = stream->packets->packetS->datalen;
			data = (u_char*)stream->packets->packetS->data;
		}
		while(data_len > 0) {
			u_char *endHeaderSepPos = (u_char*)memmem(data, data_len, "\r\n\r\n", 4);
			if(endHeaderSepPos) {
				*endHeaderSepPos = 0;
				char *contentLengthPos = strcasestr((char*)data, "Content-Length: ");
				*endHeaderSepPos = '\r';
				unsigned int contentLength = 0;
				if(contentLengthPos) {
					contentLength = atol(contentLengthPos + 16);
				}
				int sipDataLen = (endHeaderSepPos - data) + 4 + contentLength;
				extern int check_sip20(char *data, unsigned long len, ParsePacket::ppContentsX *parseContents);
				if(sipDataLen == data_len) {
					return(true);
				} else if(sipDataLen < data_len) {
					if(!check_sip20((char*)(data + sipDataLen), data_len - sipDataLen, NULL)) {
						return(true);
					} else {
						data += sipDataLen;
						data_len -= sipDataLen;
					}
				} else {
					break;
				}
			} else {
				break;
			}
		}
		return(false);
	}
	void cleanStream(tcp_stream *stream, bool callFromClean = false);
private:
	map<tcp_stream_id, tcp_stream> tcp_streams;
};


class PreProcessPacket {
public:
	enum eTypePreProcessThread {
		ppt_detach,
		ppt_sip,
		ppt_extend,
		ppt_pp_call,
		ppt_pp_register,
		ppt_pp_rtp
	};
	struct batch_packet_s {
		batch_packet_s(unsigned max_count) {
			count = 0;
			used = 0;
			batch = new packet_s*[max_count];
			for(unsigned i = 0; i < max_count; i++) {
				batch[i] = new packet_s;
			}
			this->max_count = max_count;
		}
		~batch_packet_s() {
			for(unsigned i = 0; i < max_count; i++) {
				delete batch[i];
			}
			delete [] batch;
		}
		packet_s **batch;
		volatile unsigned count;
		volatile int used;
		unsigned max_count;
	};
	struct batch_packet_s_process {
		batch_packet_s_process(unsigned max_count) {
			count = 0;
			used = 0;
			batch = new FILE_LINE packet_s_process*[max_count];
			memset(batch, 0, sizeof(packet_s_process*) * max_count);
			this->max_count = max_count;
		}
		~batch_packet_s_process() {
			for(unsigned i = 0; i < max_count; i++) {
				if(batch[i]) {
					batch[i]->blockstore_clear();
					delete batch[i];
					batch[i] = NULL;
				}
			}
			delete [] batch;
		}
		packet_s_process **batch;
		volatile unsigned count;
		volatile int used;
		unsigned max_count;
	};
public:
	PreProcessPacket(eTypePreProcessThread typePreProcessThread);
	~PreProcessPacket();
	inline void push_packet(bool is_ssl, u_int64_t packet_number,
				unsigned int saddr, int source, unsigned int daddr, int dest, 
				char *data, int datalen, int dataoffset,
				pcap_t *handle, pcap_pkthdr *header, const u_char *packet, bool packetDelete,
				int istcp, struct iphdr2 *header_ip,
				pcap_block_store *block_store, int block_store_index, int dlt, int sensor_id,
				bool blockstore_lock = true) {
		packet_s packetS;
		packetS.packet_number = packet_number;
		packetS.saddr = saddr;
		packetS.source = source;
		packetS.daddr = daddr; 
		packetS.dest = dest;
		packetS.data = data; 
		packetS.datalen = datalen; 
		packetS.dataoffset = dataoffset;
		packetS.handle = handle; 
		packetS.header = *header; 
		packetS.packet = packet; 
		packetS.istcp = istcp; 
		packetS.header_ip = header_ip; 
		packetS.block_store = block_store; 
		packetS.block_store_index =  block_store_index; 
		packetS.dlt = dlt; 
		packetS.sensor_id = sensor_id;
		packetS.is_ssl = is_ssl;
		if(blockstore_lock) {
			packetS.blockstore_lock();
		}
		this->push_packet(&packetS);
	}
	inline void push_packet(packet_s *packetS) {
		if(typePreProcessThread == ppt_detach && opt_enable_ssl) {
			this->lock_push();
		}
		if(!qring_push_index) {
			unsigned usleepCounter = 0;
			while(this->qring_detach[this->writeit]->used != 0) {
				usleep(20 *
				       (usleepCounter > 10 ? 50 :
					usleepCounter > 5 ? 10 :
					usleepCounter > 2 ? 5 : 1));
				++usleepCounter;
			}
			qring_push_index = this->writeit + 1;
			qring_push_index_count = 0;
			qring_detach_active_push_item = qring_detach[qring_push_index - 1];
		}
		*qring_detach_active_push_item->batch[qring_push_index_count] = *packetS;
		++qring_push_index_count;
		if(qring_push_index_count == qring_detach_active_push_item->max_count) {
			qring_detach_active_push_item->count = qring_push_index_count;
			qring_detach_active_push_item->used = 1;
			if((this->writeit + 1) == this->qring_length) {
				this->writeit = 0;
			} else {
				this->writeit++;
			}
			qring_push_index = 0;
			qring_push_index_count = 0;
		}
		if(typePreProcessThread == ppt_detach && opt_enable_ssl) {
			this->unlock_push();
		}
	}
	inline void push_packet(packet_s_process *packetS) {
		if(typePreProcessThread == ppt_detach && opt_enable_ssl) {
			this->lock_push();
		}
		if(!qring_push_index) {
			unsigned usleepCounter = 0;
			while(this->qring[this->writeit]->used != 0) {
				usleep(20 *
				       (usleepCounter > 10 ? 50 :
					usleepCounter > 5 ? 10 :
					usleepCounter > 2 ? 5 : 1));
				++usleepCounter;
			}
			qring_push_index = this->writeit + 1;
			qring_push_index_count = 0;
			qring_active_push_item = qring[qring_push_index - 1];
		}
		qring_active_push_item->batch[qring_push_index_count] = packetS;
		++qring_push_index_count;
		if(qring_push_index_count == qring_active_push_item->max_count) {
			qring_active_push_item->count = qring_push_index_count;
			qring_active_push_item->used = 1;
			if((this->writeit + 1) == this->qring_length) {
				this->writeit = 0;
			} else {
				this->writeit++;
			}
			qring_push_index = 0;
			qring_push_index_count = 0;
		}
		if(typePreProcessThread == ppt_detach && opt_enable_ssl) {
			this->unlock_push();
		}
	}
	inline void push_batch() {
		if(typePreProcessThread == ppt_detach && opt_enable_ssl) {
			this->lock_push();
		}
		if(qring_push_index && qring_push_index_count) {
			if(typePreProcessThread == ppt_detach) {
				qring_detach_active_push_item->count = qring_push_index_count;
				qring_detach_active_push_item->used = 1;
			} else {
				qring_active_push_item->count = qring_push_index_count;
				qring_active_push_item->used = 1;
			}
			if((this->writeit + 1) == this->qring_length) {
				this->writeit = 0;
			} else {
				this->writeit++;
			}
			qring_push_index = 0;
			qring_push_index_count = 0;
		}
		if(typePreProcessThread == ppt_detach && opt_enable_ssl) {
			this->unlock_push();
		}
	}
	void preparePstatData();
	double getCpuUsagePerc(bool preparePstatData);
	void terminate();
	static bool isEnableDetach() {
		extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
		return(preProcessPacket[0] != NULL);
	}
	static bool isEnableSip() {
		extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
		return(preProcessPacket[1] != NULL);
	}
	static bool isEnableExtend() {
		extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
		return(preProcessPacket[2] != NULL);
	}
	static void autoStartNextLevelPreProcessPacket();
	double getQringFillingPerc() {
		unsigned int _readit = readit;
		unsigned int _writeit = writeit;
		return(_writeit >= _readit ?
			(double)(_writeit - _readit) / qring_length * 100 :
			(double)(qring_length - _readit + _writeit) / qring_length * 100);
	}
	inline packet_s_process *packetS_sip_create() {
		packet_s_process *packetS = new FILE_LINE packet_s_process;
		return(packetS);
	}
	inline packet_s_process_0 *packetS_rtp_create() {
		packet_s_process_0 *packetS = new FILE_LINE packet_s_process_0;
		return(packetS);
	}
	inline packet_s_process *packetS_sip_pop_from_stack(u_int16_t queue_index) {
		packet_s_process *packetS;
		if(this->stackSip->pop((void**)&packetS, queue_index)) {
			/*
			if(*(u_char*)packetS) {
				cout << "XXX1" << endl;
				abort();
			}
			*/
			packetS->init();
		} else {
			packetS = new FILE_LINE packet_s_process;
		}
		packetS->stack = this->stackSip;
		return(packetS);
	}
	inline packet_s_process_0 *packetS_rtp_pop_from_stack(u_int16_t queue_index) {
		packet_s_process_0 *packetS;
		if(this->stackRtp->pop((void**)&packetS, queue_index)) {
			/*
			if(*(u_char*)packetS) {
				cout << "XXX2" << endl;
				abort();
			}
			*/
			packetS->init();
		} else {
			packetS = new FILE_LINE packet_s_process_0;
		}
		packetS->stack = this->stackRtp;
		return(packetS);
	}
	inline void packetS_destroy(packet_s_process **packetS) {
		(*packetS)->blockstore_unlock();
		delete *packetS;
		*packetS = NULL;
	}
	inline void packetS_destroy(packet_s_process_0 **packetS) {
		(*packetS)->blockstore_unlock();
		delete *packetS;
		*packetS = NULL;
	}
	inline void packetS_push_to_stack(packet_s_process **packetS, u_int16_t queue_index) {
		(*packetS)->blockstore_unlock();
		if(!(*packetS)->stack ||
		   !(*packetS)->stack->push((void*)*packetS, queue_index)) {
			delete *packetS;
		}
		*packetS = NULL;
	}
	inline void packetS_push_to_stack(packet_s_process_0 **packetS, u_int16_t queue_index) {
		(*packetS)->blockstore_unlock();
		if(!(*packetS)->stack ||
		   !(*packetS)->stack->push((void*)*packetS, queue_index)) {
			delete *packetS;
		}
		*packetS = NULL;
	}
	inline eTypePreProcessThread getTypePreProcessThread() {
		return(typePreProcessThread);
	}
private:
	void sipProcess_SIP(packet_s_process **packetS_ref);
	void sipProcess_EXTEND(packet_s_process **packetS_ref);
	void sipProcess_reassembly(packet_s_process **packetS_ref);
	void sipProcess_parseSipData(packet_s_process **packetS_ref);
	void sipProcess_sip(packet_s_process **packetS_ref);
	void sipProcess_rtp(packet_s_process **packetS_ref);
	inline bool sipProcess_getCallID(packet_s_process **packetS_ref);
	inline bool sipProcess_getCallID_publish(packet_s_process **packetS_ref);
	inline void sipProcess_getSipMethod(packet_s_process **packetS_ref);
	inline void sipProcess_getLastSipResponse(packet_s_process **packetS_ref);
	inline void sipProcess_findCall(packet_s_process **packetS_ref);
	inline void sipProcess_createCall(packet_s_process **packetS_ref);
	void *outThreadFunction();
	void lock_push() {
		while(__sync_lock_test_and_set(&this->_sync_push, 1)) {
			usleep(10);
		}
	}
	void unlock_push() {
		__sync_lock_release(&this->_sync_push);
	}
private:
	eTypePreProcessThread typePreProcessThread;
	unsigned int qring_batch_item_length;
	unsigned int qring_length;
	batch_packet_s **qring_detach;
	batch_packet_s *qring_detach_active_push_item;
	batch_packet_s_process **qring;
	batch_packet_s_process *qring_active_push_item;
	unsigned qring_push_index;
	unsigned qring_push_index_count;
	volatile unsigned int readit;
	volatile unsigned int writeit;
	pthread_t out_thread_handle;
	pstat_data threadPstatData[2];
	int outThreadId;
	volatile int _sync_push;
	bool term_preProcess;
	cHeapItemsPointerStack *stackSip;
	cHeapItemsPointerStack *stackRtp;
friend inline void *_PreProcessPacket_outThreadFunction(void *arg);
friend class TcpReassemblySip;
};

inline packet_s_process *PACKET_S_PROCESS_SIP_CREATE() {
	extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
	return(preProcessPacket[0]->packetS_sip_create());
}

inline packet_s_process_0 *PACKET_S_PROCESS_RTP_CREATE() {
	extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
	return(preProcessPacket[0]->packetS_rtp_create());
}

inline packet_s_process *PACKET_S_PROCESS_SIP_POP_FROM_STACK(u_int16_t queue_index) {
	extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
	return(preProcessPacket[0]->packetS_sip_pop_from_stack(queue_index));
}

inline packet_s_process_0 *PACKET_S_PROCESS_RTP_POP_FROM_STACK(u_int16_t queue_index) {
	extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
	return(preProcessPacket[0]->packetS_rtp_pop_from_stack(queue_index));
}

inline void PACKET_S_PROCESS_DESTROY(packet_s_process_0 **packet) {
	extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
	preProcessPacket[0]->packetS_destroy(packet);
}

inline void PACKET_S_PROCESS_DESTROY(packet_s_process **packet) {
	extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
	preProcessPacket[0]->packetS_destroy(packet);
}

inline void PACKET_S_PROCESS_PUSH_TO_STACK(packet_s_process_0 **packet, u_int16_t queue_index) {
	extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
	preProcessPacket[0]->packetS_push_to_stack(packet, queue_index);
}

inline void PACKET_S_PROCESS_PUSH_TO_STACK(packet_s_process **packet, u_int16_t queue_index) {
	extern PreProcessPacket *preProcessPacket[MAX_PREPROCESS_PACKET_THREADS];
	preProcessPacket[0]->packetS_push_to_stack(packet, queue_index);
}


class ProcessRtpPacket {
public:
	enum eType {
		hash,
		distribute
	};
public:
	struct batch_packet_s_process {
		batch_packet_s_process(unsigned max_count) {
			count = 0;
			used = 0;
			batch = new FILE_LINE packet_s_process_0*[max_count];
			memset(batch, 0, sizeof(packet_s_process_0*) * max_count);
			this->max_count = max_count;
		}
		~batch_packet_s_process() {
			for(unsigned i = 0; i < max_count; i++) {
				if(batch[i]) {
					batch[i]->blockstore_clear();
					delete batch[i];
					batch[i]= NULL;
				}
			}
			delete [] batch;
		}
		packet_s_process_0 **batch;
		volatile unsigned count;
		volatile int used;
		unsigned max_count;
	};
	struct arg_next_thread {
		ProcessRtpPacket *processRtpPacket;
		int next_thread_id;
	};
public:
	ProcessRtpPacket(eType type, int indexThread);
	~ProcessRtpPacket();
	inline void push_packet(packet_s_process_0 *packetS) {
		if(!qring_push_index) {
			unsigned usleepCounter = 0;
			while(this->qring[this->writeit]->used != 0) {
				usleep(20 *
				       (usleepCounter > 10 ? 50 :
					usleepCounter > 5 ? 10 :
					usleepCounter > 2 ? 5 : 1));
				++usleepCounter;
			}
			qring_push_index = this->writeit + 1;
			qring_push_index_count = 0;
			qring_active_push_item = this->qring[qring_push_index - 1];
		}
		qring_active_push_item->batch[qring_push_index_count] = packetS;
		++qring_push_index_count;
		if(qring_push_index_count == qring_active_push_item->max_count) {
			qring_active_push_item->count = qring_push_index_count;
			qring_active_push_item->used = 1;
			if((this->writeit + 1) == this->qring_length) {
				this->writeit = 0;
			} else {
				this->writeit++;
			}
			qring_push_index = 0;
			qring_push_index_count = 0;
		}
	}
	inline void push_batch() {
		if(qring_push_index && qring_push_index_count) {
			qring_active_push_item->count = qring_push_index_count;
			qring_active_push_item->used = 1;
			if((this->writeit + 1) == this->qring_length) {
				this->writeit = 0;
			} else {
				this->writeit++;
			}
			qring_push_index = 0;
			qring_push_index_count = 0;
		}
	}
	void preparePstatData(int nextThreadId = 0);
	double getCpuUsagePerc(bool preparePstatData, int nextThreadId = 0);
	void terminate();
	static void autoStartProcessRtpPacket();
	void addRtpRhThread();
	static void addRtpRdThread();
	double getQringFillingPerc() {
		unsigned int _readit = readit;
		unsigned int _writeit = writeit;
		return(_writeit >= _readit ?
			(double)(_writeit - _readit) / qring_length * 100 :
			(double)(qring_length - _readit + _writeit) / qring_length * 100);
	}
	bool isNextThreadsGt2Processing(int process_rtp_packets_hash_next_threads) {
		//#pragma GCC diagnostic push
		//#pragma -Warray-bounds
		for(int i = 2; i < process_rtp_packets_hash_next_threads; i++) {
			if(this->hash_batch_thread_process[i]) {
				return(true);
			}
		}
		return(false);
		//#pragma GCC diagnostic pop
	}
	bool existsNextThread(int next_thread_index) {
		return(next_thread_index < MAX_PROCESS_RTP_PACKET_HASH_NEXT_THREADS &&
		       this->nextThreadId[next_thread_index]);
	}
private:
	void *outThreadFunction();
	void *nextThreadFunction(int next_thread_index_plus);
	void rtp_batch(batch_packet_s_process *batch);
	void find_hash(packet_s_process_0 *packetS, bool lock = true);
public:
	eType type;
	int indexThread;
	int outThreadId;
	int nextThreadId[MAX_PROCESS_RTP_PACKET_HASH_NEXT_THREADS];
private:
	int process_rtp_packets_hash_next_threads;
	volatile int process_rtp_packets_hash_next_threads_use_for_batch;
	unsigned int qring_batch_item_length;
	unsigned int qring_length;
	batch_packet_s_process **qring;
	batch_packet_s_process *qring_active_push_item;
	unsigned qring_push_index;
	unsigned qring_push_index_count;
	volatile unsigned int readit;
	volatile unsigned int writeit;
	pthread_t out_thread_handle;
	pthread_t next_thread_handle[MAX_PROCESS_RTP_PACKET_HASH_NEXT_THREADS];
	pstat_data threadPstatData[1 + MAX_PROCESS_RTP_PACKET_HASH_NEXT_THREADS][2];
	bool term_processRtp;
	volatile batch_packet_s_process *hash_batch_thread_process[MAX_PROCESS_RTP_PACKET_HASH_NEXT_THREADS];
	sem_t sem_sync_next_thread[MAX_PROCESS_RTP_PACKET_HASH_NEXT_THREADS][2];
friend inline void *_ProcessRtpPacket_outThreadFunction(void *arg);
friend inline void *_ProcessRtpPacket_nextThreadFunction(void *arg);
};


#endif
