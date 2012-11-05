//////////////////////////////////////////////////////////////////
//
// RasPDU.h
//
// Define RAS PDU for GNU Gatekeeper
// Avoid including large h225.h in RasSrv.h
//
// Copyright (c) Citron Network Inc. 2001-2003
// Copyright (c) 2006-2011, Jan Willamowius
//
// This work is published under the GNU Public License version 2 (GPLv2)
// see file COPYING for details.
// We also explicitly grant the right to link this code
// with the OpenH323/H323Plus and OpenSSL library.
//
//////////////////////////////////////////////////////////////////

#ifndef RASPDU_H
#define RASPDU_H "@(#) $Id: RasPDU.h,v 1.29 2012/05/16 16:00:36 willamowius Exp $"

#include <list>
#include "yasocket.h"
#include "factory.h"
#include "rasinfo.h"


class Toolkit;
class GkStatus;
class RegistrationTable;
class CallTable;
class RasListener;
class MulticastListener;
class CallSignalListener;
class StatusListener;
class MultiplexRTPListener;
class RasServer;
class CallSignalSocket;

const unsigned MaxRasTag = H225_RasMessage::e_serviceControlResponse;

class GatekeeperMessage {
public:
	GatekeeperMessage() : m_peerPort(0), m_socket(NULL)
#ifdef HAS_H46017
		, m_h46017Socket(NULL)
#endif
		{ }
	unsigned GetTag() const { return m_recvRAS.GetTag(); }
	const char *GetTagName() const;
	bool Read(RasListener *);
#ifdef HAS_H46017
	bool Read(const PBYTEArray & buffer);
#endif
	bool Reply() const;

	PPER_Stream m_rasPDU;
	H225_RasMessage m_recvRAS;
	H225_RasMessage m_replyRAS;
	PIPSocket::Address m_peerAddr;
	WORD m_peerPort;
	PIPSocket::Address m_localAddr;
	RasListener * m_socket;
#ifdef HAS_H46017
	CallSignalSocket * m_h46017Socket;
#endif
};

class RasListener : public UDPSocket {
public:
	RasListener(const Address &, WORD);
	virtual ~RasListener();

	GatekeeperMessage *ReadRas();
	bool SendRas(const H225_RasMessage &, const Address &, WORD);

	WORD GetSignalPort() const { return m_signalPort; }
	void SetSignalPort(WORD pt) { m_signalPort = pt; }

	Address GetLocalAddr(const Address &) const;
	Address GetPhysicalAddr(const Address & addr) const;
	H225_TransportAddress GetRasAddress(const Address &) const;
	H225_TransportAddress GetCallSignalAddress(const Address &) const;

	// new virtual function
	// filter out unwanted message to the listener by returning false
	virtual bool Filter(GatekeeperMessage *) const;

protected:
	Address m_ip;
	PMutex m_wmutex;
	WORD m_signalPort;
	bool m_virtualInterface;
};

class RasMsg : public Task {
public:
	virtual ~RasMsg() { delete m_msg; }

	// new virtual function
	virtual bool Process() = 0;
   
	virtual int GetSeqNum() const = 0;
	virtual H225_NonStandardParameter *GetNonStandardParam() = 0;

	// override from class Task
	virtual void Exec();

	bool IsFrom(const PIPSocket::Address &, WORD) const;
	unsigned GetTag() const { return m_msg->GetTag(); }
	const char *GetTagName() const { return m_msg->GetTagName(); }

	void GetRasAddress(H225_TransportAddress &) const;
	void GetCallSignalAddress(H225_TransportAddress &) const;

	bool EqualTo(const RasMsg *) const;
	bool operator==(const RasMsg & other) const { return EqualTo(&other); }

	bool Reply() const { return m_msg->Reply(); }

	GatekeeperMessage *operator->() { return m_msg; }
	const GatekeeperMessage *operator->() const { return m_msg; }

	static void Initialize();
protected:
	RasMsg(GatekeeperMessage *m) : m_msg(m) {}
	RasMsg(const RasMsg &);

	static bool PrintStatus(const PString &);
	
	GatekeeperMessage *m_msg;

	// just pointers to global singleton objects
	// cache for faster access
	static Toolkit *Kit;
	static RegistrationTable *EndpointTbl;
	static CallTable *CallTbl;
	static RasServer *RasSrv;
};

template<class RAS>
class RasPDU : public RasMsg {
public:
	typedef RAS RasClass;
	
	RasPDU(GatekeeperMessage *m) : RasMsg(m), request(m->m_recvRAS) {}
	virtual ~RasPDU() {}

	// override from class RasMsg
	virtual bool Process() { return false; }
	virtual int GetSeqNum() const { return request.m_requestSeqNum; }
	virtual H225_NonStandardParameter *GetNonStandardParam();

	operator RAS & () { return request; }
	operator const RAS & () const { return request; }

	H225_RasMessage & BuildConfirm();
	H225_RasMessage & BuildReject(unsigned);

	typedef Factory<RasMsg, unsigned>::Creator1<GatekeeperMessage *> RasCreator;
	struct Creator : public RasCreator {
		Creator() : RasCreator(RasInfo<RAS>::tag) {}
		virtual RasMsg *operator()(GatekeeperMessage *m) const { return new RasPDU<RAS>(m); }
	};

protected:
	RAS & request;
};

// abstract factory for listeners
class GkInterface {
public:
	typedef PIPSocket::Address Address;

	GkInterface(const Address &);
	virtual ~GkInterface();

	// we can't call virtual functions in constructor
	// so initialize here
	virtual bool CreateListeners(RasServer *);

	bool IsBoundTo(const Address *addr) const { return m_address == *addr; }
	bool IsReachable(const Address *) const;

	RasListener *GetRasListener() const { return m_rasListener; }

	WORD GetRasPort() const { return m_rasPort; }
	WORD GetSignalPort() const { return m_signalPort; }
	Address GetAddress() const { return m_address; }

protected:
	bool ValidateSocket(IPSocket *, WORD &);

	template <class Listener> bool SetListener(WORD nport, WORD & oport, Listener *& listener, Listener *(GkInterface::*creator)())
	{
		if (!listener || !oport || oport != nport) {
			oport = nport;
			if (listener)
				listener->Close();
			listener = (this->*creator)();
			if (ValidateSocket(listener, oport))
				return true;
			else
				listener = NULL;
		}
		return false;
	}

	Address m_address;
	RasListener *m_rasListener;
	MulticastListener *m_multicastListener;
	CallSignalListener *m_callSignalListener;
	StatusListener *m_statusListener;
	WORD m_rasPort, m_multicastPort, m_signalPort, m_statusPort;
	RasServer *m_rasSrv;

private:
	virtual RasListener *CreateRasListener();
	virtual MulticastListener *CreateMulticastListener();
	virtual CallSignalListener *CreateCallSignalListener();
	virtual StatusListener *CreateStatusListener();
};

class RasHandler {
public:
	typedef PIPSocket::Address Address;

	RasHandler();
	virtual ~RasHandler() {}

	// new virtual function

	// check if the message is the expected one
	// default behavior: check if the tag is in m_tagArray
	virtual bool IsExpected(const RasMsg *) const;

	// process the RasMsg object
	// the object must be deleted after processed
	virtual void Process(RasMsg *) = 0;

	// give the derived class an opportunity to create customized PDU
	// default behavior: return the original one
	virtual RasMsg *CreatePDU(RasMsg *ras) { return ras; }

	// stop the handler
	virtual void Stop() {}

protected:
	void AddFilter(unsigned);

	RasServer *m_rasSrv;

private:
	bool m_tagArray[MaxRasTag + 1];
};

// encapsulate a gatekeeper request and reply
class RasRequester : public RasHandler {
public:
	RasRequester() { Init(); }
	// note the H225_RasMessage object must have
	// longer lifetime than this object
	RasRequester(H225_RasMessage &);
	RasRequester(H225_RasMessage &, const Address &);
	virtual ~RasRequester();

	WORD GetSeqNum() const { return m_seqNum; }
	bool WaitForResponse(int);
	RasMsg *GetReply();

	// override from class RasHandler
	virtual bool IsExpected(const RasMsg *) const;
	virtual void Process(RasMsg *);
	virtual void Stop();

	// new virtual function
	virtual bool SendRequest(const Address &, WORD, int = 2);
	virtual bool OnTimeout();

protected:
	void AddReply(RasMsg *);

	H225_RasMessage *m_request;
	WORD m_seqNum;
	Address m_txAddr, m_loAddr;
	WORD m_txPort;
	PTime m_sentTime;
	int m_timeout, m_retry;
	PSyncPoint m_sync;

private:
	void Init();

	PMutex m_qmutex;
	std::list<RasMsg *> m_queue;
	std::list<RasMsg *>::iterator m_iterator;
};

template<class RAS>
class Requester : public RasRequester {
public:
	typedef typename RasInfo<RAS>::Tag Tag;
	typedef typename RasInfo<RAS>::ConfirmTag ConfirmTag;
	typedef typename RasInfo<RAS>::RejectTag RejectTag;
	Requester(H225_RasMessage &, const Address &);
	virtual ~Requester() { this->m_rasSrv->UnregisterHandler(this); } // fix for GCC 3.4.2
};

template<class RAS>
Requester<RAS>::Requester(H225_RasMessage & obj_ras, const Address & ip) : RasRequester(obj_ras, ip)
{
	obj_ras.SetTag(Tag());
	RAS & ras = obj_ras;
	ras.m_requestSeqNum = GetSeqNum();
	AddFilter(ConfirmTag());
	AddFilter(RejectTag());
	this->m_rasSrv->RegisterHandler(this); // fix for GCC 3.4.2
}

#endif // RASPDU_H
