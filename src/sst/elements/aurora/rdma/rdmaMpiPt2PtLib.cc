// Copyright 2009-2018 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2018, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#include <sst_config.h>
#include "rdmaMpiPt2PtLib.h"

using namespace SST::Aurora::RDMA;
using namespace Hermes;

#define CALL_INFO_LAMBDA     __LINE__, __FILE__


RdmaMpiPt2PtLib::RdmaMpiPt2PtLib( ComponentId_t id, Params& params) : MpiPt2Pt(id,params)
{
	if ( params.find<bool>("print_all_params",false) ) {
		printf("Aurora::RDMA::RdmaMpiPt2PtLib()\n");
		params.print_all_params(std::cout);
	}

	m_dbg.init("@t:Aurora::RDMA::RdmaMpiPt2PtLib::@p():@l ", params.find<uint32_t>("verboseLevel",0),
			params.find<uint32_t>("verboseMask",0), Output::STDOUT );

	m_rqId = 0xF00D;
    m_dbg.debug(CALL_INFO,1,1,"\n");

	Params rdmaParams =  params.find_prefix_params("rdmaLib.");

	m_rdma = dynamic_cast< Hermes::RDMA::Interface*>( loadAnonymousSubComponent<Hermes::Interface>( "aurora.rdmaLib", "", 0, ComponentInfo::SHARE_NONE, rdmaParams ) );

	assert(m_rdma);
}

void RdmaMpiPt2PtLib::_init( int* numRanks, int* myRank, Hermes::Callback* callback ) {
	
	m_dbg.debug(CALL_INFO,1,1,"numRanks=%d myRank=%d\n",*numRanks,*myRank);
	Callback* cb = new Callback( std::bind( &RdmaMpiPt2PtLib::mallocSendBuffers, this, callback, std::placeholders::_1 ) );
	rdma().createRQ( m_rqId, cb ); 
}

void RdmaMpiPt2PtLib::_isend( const Hermes::Mpi::MemAddr& buf, int count, Hermes::Mpi::DataType dataType, int dest, int tag,
		Hermes::Mpi::Comm comm, Hermes::Mpi::Request* request, Hermes::Callback* callback ) 
{
	m_dbg.debug(CALL_INFO,1,1,"buf=0x%" PRIx64 " count=%d dataSize=%d dest=%d tag=%d comm=%d\n",
			buf.getSimVAddr(),count,Mpi::sizeofDataType( dataType ), dest, tag, comm );

	Hermes::Callback* x = new Hermes::Callback([=]( int retval ) {
		m_dbg.debug(CALL_INFO_LAMBDA,"isend",1,1,"returning\n");
		m_selfLink->send(0,new SelfEvent(callback,retval) );
	});	

	SendEntry* entry = new SendEntry( buf, count, dataType, dest, tag, comm, request );
	entry->type = Hermes::Mpi::RequestData::Send;
	*request = entry;

	m_dbg.debug(CALL_INFO,1,1,"request=%p entry=%p\n",request,*request);

	m_postedSends.push( entry );

	size_t bytes = entry->count * Mpi::sizeofDataType( entry->dataType ); 
	if ( bytes > m_shortMsgLength ) {

		Callback* cb = new Callback( [=](int) {
			makeProgress(x);
		});	
				
		rdma().registerMem( entry->buf, bytes, &entry->extra.memId, cb );
	} else {

		makeProgress( x );
	}
}

void RdmaMpiPt2PtLib::_irecv( const Hermes::Mpi::MemAddr& buf, int count, Hermes::Mpi::DataType dataType, int src, int tag,
		Hermes::Mpi::Comm comm, Hermes::Mpi::Request* request, Hermes::Callback* callback ) 
{
	m_dbg.debug(CALL_INFO,1,1,"buf=0x%" PRIx64 " count=%d dataSize=%d src=%d tag=%d comm=%d\n",
			buf.getSimVAddr(),count,Mpi::sizeofDataType( dataType ), src, tag, comm );

	Hermes::Callback* x = new Hermes::Callback([=]( int retval ) {
		m_dbg.debug(CALL_INFO_LAMBDA,"irecv",1,1,"returning\n");
		m_selfLink->send(0,new SelfEvent(callback,retval) );
	});	

	RecvEntry* entry = new RecvEntry( buf, count, dataType, src, tag, comm, request );
	entry->type = Hermes::Mpi::RequestData::Recv;
	*request = entry;

	m_dbg.debug(CALL_INFO,1,1,"request=%p entry=%p\n",request,*request);

	Hermes::RDMA::Status* status = checkUnexpected( entry );

	if ( status ) {
		m_dbg.debug(CALL_INFO,1,1,"found unexpected\n");
		processMatch( *status, entry, x );
	} else { 

		m_dbg.debug(CALL_INFO,1,1,"post recv\n");
		m_postedRecvs.push_back( entry );

		size_t bytes = entry->count * Mpi::sizeofDataType( entry->dataType ); 
		if ( bytes > m_shortMsgLength ) {

			Callback* cb = new Callback( [=](int) {
				makeProgress(x);
			});
				
			rdma().registerMem( entry->buf, bytes, &entry->extra.memId, cb );
		} else {
			makeProgress( x );
		}
	}
}

void RdmaMpiPt2PtLib::processTest( TestBase* waitEntry, int ) 
{
	m_dbg.debug(CALL_INFO,1,1,"\n");
   	if ( waitEntry->isDone() ) {

		m_dbg.debug(CALL_INFO,1,1,"entry done\n");
		(*waitEntry->callback)(0);
		delete waitEntry;

	} else if ( waitEntry->blocking )  {
		m_dbg.debug(CALL_INFO,1,1,"blocking\n");
		Callback* cb = new Callback( [=](int retval ){
			m_dbg.debug(CALL_INFO_LAMBDA,"processTest",1,1,"return from blocking checkRQ %s\n",retval ? "message ready":"no message");
			assert( retval == 1 );
		    Callback* cb = new Callback( std::bind( &RdmaMpiPt2PtLib::processTest, this, waitEntry, std::placeholders::_1 ) );
			checkMsgAvail( cb, retval );
		});
		rdma().checkRQ( m_rqId, &m_rqStatus, true, cb );
	} else {
		m_dbg.debug(CALL_INFO,1,1,"entry not done\n");
		(*waitEntry->callback)(0);
		delete waitEntry;
	}		
}

void RdmaMpiPt2PtLib::makeProgress( Hermes::Callback* callback )
{
	m_dbg.debug(CALL_INFO,1,1,"\n");
	Callback* cb = new Callback( std::bind( &RdmaMpiPt2PtLib::processSendQ, this, callback, std::placeholders::_1 ) );

	processRecvQ( cb, 0 ); 
}

void RdmaMpiPt2PtLib::processRecvQ( Hermes::Callback* callback, int retval  )
{
	m_dbg.debug(CALL_INFO,1,1,"\n");
	Callback* cb = new Callback( std::bind( &RdmaMpiPt2PtLib::checkMsgAvail, this, callback, std::placeholders::_1 ) );
	rdma().checkRQ( m_rqId, &m_rqStatus, false, cb );
}

void RdmaMpiPt2PtLib::checkMsgAvail( Hermes::Callback* callback, int retval ) {
    if ( 1 == retval ) {
		m_dbg.debug(CALL_INFO,1,1,"message avail\n");

		Hermes::Callback* cb = new Hermes::Callback;
		*cb = [=](int) {
			processRecvQ( callback, 0 );
		};
        processMsg( m_rqStatus, cb );
    } else { 
		m_dbg.debug(CALL_INFO,1,1,"no message\n");
		(*callback)(0);
		delete callback;
	}
}

void RdmaMpiPt2PtLib::processSendEntry( Hermes::Callback* callback, SendEntryBase* _entry ) {

	SendEntry* entry = dynamic_cast<SendEntry*>(_entry);
	size_t bytes = entry->count * Mpi::sizeofDataType( entry->dataType ); 

	entry->sendBuf = allocSendBuf(); 

	MsgHdr* hdr = (MsgHdr*) entry->sendBuf->buf->getBacking();

	hdr->srcRank = os().getMyRank(entry->comm);
	hdr->tag = entry->tag;
	hdr->count = entry->count;
	hdr->dataType = entry->dataType;
	hdr->comm = entry->comm;
	hdr->type = MsgHdr::Match;
	m_dbg.debug(CALL_INFO,1,1,"tag=%d rank=%d comm=%d count=%d type=%d\n",hdr->tag,hdr->srcRank,hdr->comm,hdr->count,hdr->type);

    Hermes::Callback* cb;

	int delay = 0;

	Hermes::ProcAddr procAddr = getProcAddr(_entry);

	if ( bytes <= m_shortMsgLength ) {
		m_dbg.debug(CALL_INFO,1,1,"short message\n");
		if ( entry->buf.getBacking() ) {
			void* payload = entry->buf.getBacking(); 
			memcpy( entry->sendBuf->buf->getBacking(sizeof(MsgHdr)), payload, bytes ); 
		}	
		m_dbg.debug(CALL_INFO,1,MPI_DBG_MASK_MSG_LVL1,"success, sent shortmsg to rank %d, bytes=%zu\n",entry->dest, bytes);
		entry->doneFlag = true;
		cb = callback;

		delay = calcMemcpyLatency( bytes );

	} else {
		m_dbg.debug(CALL_INFO,1,1,"long message key=%p\n",entry);

		hdr->readAddr = entry->buf.getSimVAddr();
		hdr->key = entry;

		cb = new Hermes::Callback;

    	*cb = std::bind( &RdmaMpiPt2PtLib::waitForLongAck, this, callback, entry, std::placeholders::_1 );
		
		bytes = 0;
	}
    Hermes::Callback* x = new Hermes::Callback([=]( int retval ) {
		m_dbg.debug(CALL_INFO_LAMBDA,"processSendEntry",1,1," memcpy delay returning\n");
		sendMsg( procAddr, *entry->sendBuf->buf, sizeof(MsgHdr) + bytes, &entry->sendBuf->handle, cb );
    });

    m_selfLink->send(delay,new SelfEvent(x) );
}

void RdmaMpiPt2PtLib::waitForLongAck( Hermes::Callback* callback, SendEntry* entry, int retval )
{
	 m_dbg.debug(CALL_INFO,1,1,"\n");

	Hermes::Callback* cb = new Hermes::Callback;
	*cb = [=](int retval ){

		m_dbg.debug(CALL_INFO_LAMBDA,"waitForLongAck",1,1,"back from checkRQ\n");
   		Hermes::Callback* cb = new Hermes::Callback;
		*cb = [=](int retval ){ 
			m_dbg.debug(CALL_INFO_LAMBDA,"waitForLongAck",1,1,"back from checkMsgAvail\n");
			if ( entry->isDone() ) {
				m_dbg.debug(CALL_INFO_LAMBDA,"waitForLongAck",1,1,"long send done\n");
				(*callback)(0);
				delete callback;				
			} else {
				m_dbg.debug(CALL_INFO_LAMBDA,"waitForLongAck",1,1,"call waitForLongAck again\n");
				waitForLongAck( callback, entry, 0 );
			}
		};

		assert( retval == 1 );
		checkMsgAvail( cb, retval );
	};
	rdma().checkRQ( m_rqId, &m_rqStatus, true, cb );
}

void RdmaMpiPt2PtLib::sendMsg( Hermes::ProcAddr procAddr, const Hermes::MemAddr& addr, size_t length, int* handle, Hermes::Callback* callback )
{
	m_dbg.debug(CALL_INFO,1,1,"destNid=%d destPid=%d addr=0x%" PRIx64 "length=%zu\n",
				procAddr.node, procAddr.pid, addr.getSimVAddr(), length  );
	rdma().send( procAddr, m_rqId, addr, length, handle, callback );
}

void RdmaMpiPt2PtLib::processMsg( Hermes::RDMA::Status& status, Hermes::Callback* callback ) {
    m_dbg.debug(CALL_INFO,1,1,"got message from node=%d pid=%d \n",status.procAddr.node, status.procAddr.pid );
	
    MsgHdr* hdr = (MsgHdr*) status.addr.getBacking();
	if( hdr->type == MsgHdr::Ack ) {
		m_dbg.debug(CALL_INFO,1,1,"Ack key=%p\n",hdr->key);
		SendEntry* entry = (SendEntry*) hdr->key;
		size_t bytes = entry->count * Mpi::sizeofDataType( entry->dataType ); 
		m_dbg.debug(CALL_INFO,1,MPI_DBG_MASK_MSG_LVL1,"success, sent long  msg to rank %d, bytes=%zu\n",entry->dest, bytes);
		entry->doneFlag = true;

		repostRecvBuffer( status.addr, callback );
		return;
	}

	auto foo = [=]( RecvEntryBase* entry ) {
		if ( entry ) {
			processMatch( status, entry, callback );
		} else {
			m_dbg.debug(CALL_INFO,1,1,"unexpected recv\n");

			m_unexpectedRecvs.push_back( new Hermes::RDMA::Status(status) );

			(*callback)(0);
			delete callback;
		}
	};

	findPostedRecv( hdr, foo );
}

void RdmaMpiPt2PtLib::processMatch( const Hermes::RDMA::Status status, RecvEntryBase* _entry, Hermes::Callback* callback ) {
	RecvEntry* entry = dynamic_cast< RecvEntry* >( _entry );
    MsgHdr* hdr = (MsgHdr*) status.addr.getBacking();

	entry->status.rank = hdr->srcRank;
	entry->status.tag = hdr->tag; 
	size_t bytes = entry->count * Mpi::sizeofDataType( entry->dataType ); 
	if ( bytes <= m_shortMsgLength ) {
		m_dbg.debug(CALL_INFO,1,1,"found posted short recv bytes=%zu\n",bytes);
		void* buf = entry->buf.getBacking();
		if ( buf ) {  
			void* payload = status.addr.getBacking(sizeof(MsgHdr)); 
			memcpy( buf, payload, bytes ); 
		}
		m_dbg.debug(CALL_INFO,1,MPI_DBG_MASK_MSG_LVL1,"success, received short msg from rank %d, bytes=%zu\n",entry->src,bytes);
		entry->doneFlag = true;

		Hermes::Callback* x = new Hermes::Callback([=]( int retval ) {
			m_dbg.debug(CALL_INFO_LAMBDA,"processMatch",1,1,"memcpy delay returning\n");
			repostRecvBuffer( status.addr, callback );
		});

		m_selfLink->send( calcMemcpyLatency( bytes ), new SelfEvent(x) );
	} else {

		m_dbg.debug(CALL_INFO,1,1,"found posted long recv bytes=%zu\n", bytes);

	    Hermes::Callback* cb = new Hermes::Callback;
		*cb = [=](int retval ){
			m_dbg.debug(CALL_INFO_LAMBDA,"processMsg",1,1,"back from read, send ACK\n");

			m_dbg.debug(CALL_INFO,1,MPI_DBG_MASK_MSG_LVL1,"success, received long msg from rank %d, bytes=%zu\n", entry->src, bytes );
			entry->doneFlag = true;

			Hermes::ProcAddr procAddr = status.procAddr;

			entry->sendBuf = allocSendBuf();
			MsgHdr* ackHdr = (MsgHdr*) entry->sendBuf->buf->getBacking();

			ackHdr->type = MsgHdr::Ack;
			ackHdr->key = hdr->key; 
	    	Hermes::Callback* cb = new Hermes::Callback;
			*cb = [=](int retval ){
				repostRecvBuffer( status.addr, callback );
			};

			sendMsg( procAddr, *entry->sendBuf->buf, sizeof(MsgHdr), &entry->sendBuf->handle, cb );
		};

		rdma().read( status.procAddr, entry->buf, hdr->readAddr, bytes, cb );
	}
}

Hermes::RDMA::Status* RdmaMpiPt2PtLib::checkUnexpected( RecvEntryBase* entry ) {
	std::deque< Hermes::RDMA::Status* >::iterator iter = m_unexpectedRecvs.begin();
	for ( ; iter != m_unexpectedRecvs.end(); ++iter ) {
    	MsgHdrBase* hdr = (MsgHdrBase*) (*iter)->addr.getBacking();
		if ( checkMatch( hdr, entry ) ) {
			Hermes::RDMA::Status* retval = *iter;
			m_unexpectedRecvs.erase(iter);
			return retval;
		}
	}
	return NULL;
}

void RdmaMpiPt2PtLib::postRecvBuffer( Hermes::Callback* callback, int count, int retval ) {

    m_dbg.debug(CALL_INFO,1,1,"count=%d retval=%d rdId=%d\n",count,retval, m_rqId);

    --count;
    Hermes::Callback* cb = new Hermes::Callback;
    if ( count > 0 ) {
        *cb = std::bind( &RdmaMpiPt2PtLib::postRecvBuffer, this, callback, count, std::placeholders::_1 );
    } else {
        cb = callback;
    }
    size_t length = m_shortMsgLength + sizeof(MsgHdr);
    Hermes::MemAddr addr = m_recvBuff.offset( count * length );
    rdma().postRecv( m_rqId, addr, length, NULL, cb );
}

void RdmaMpiPt2PtLib::repostRecvBuffer( Hermes::MemAddr addr, Hermes::Callback* callback ) {
    m_dbg.debug(CALL_INFO,1,1,"rqId=%d\n",m_rqId);

   	size_t length = m_shortMsgLength + sizeof(MsgHdr);
   	rdma().postRecv( m_rqId, addr, length, NULL, callback );
}
