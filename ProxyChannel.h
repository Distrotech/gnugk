//////////////////////////////////////////////////////////////////
//
// ProxyChannel.h
//
// Copyright (c) Citron Network Inc. 2001-2003
// Copyright (c) 2002-2011, Jan Willamowius
//
// This work is published under the GNU Public License version 2 (GPLv2)
// see file COPYING for details.
// We also explicitly grant the right to link this code
// with the OpenH323/H323Plus and OpenSSL library.
//
//////////////////////////////////////////////////////////////////

#ifndef PROXYCHANNEL_H
#define PROXYCHANNEL_H "@(#) $Id: ProxyChannel.h,v 1.93 2011/11/02 16:06:50 willamowius Exp $"

#include <vector>
#include <list>
#include <map>
#include "yasocket.h"
#include "RasTbl.h"
#include "gktimer.h"


class Q931;
class PASN_OctetString;
class H225_CallTerminationCause;
class H225_H323_UserInformation;
class H225_H323_UU_PDU_h323_message_body;
class H225_Setup_UUIE;
class H225_CallProceeding_UUIE;
class H225_Connect_UUIE;
class H225_Alerting_UUIE;
class H225_Information_UUIE;
class H225_ReleaseComplete_UUIE;
class H225_Facility_UUIE;
class H225_Progress_UUIE;
class H225_Status_UUIE;
class H225_StatusInquiry_UUIE;
class H225_SetupAcknowledge_UUIE;
class H225_Notify_UUIE;
class H225_TransportAddress;

class H245Handler;
class H245Socket;
class UDPProxySocket;
class ProxyHandler;
class HandlerList;
class SignalingMsg;
template <class> class H225SignalingMsg;
typedef H225SignalingMsg<H225_Setup_UUIE> SetupMsg;
typedef H225SignalingMsg<H225_Facility_UUIE> FacilityMsg;
struct SetupAuthData;

extern const char *RoutedSec;
extern const char *ProxySection;

void PrintQ931(int, const char *, const char *, const Q931 *, const H225_H323_UserInformation *);

bool GetUUIE(const Q931 & q931, H225_H323_UserInformation & uuie);

void SetUUIE(Q931 & q931, const H225_H323_UserInformation & uuie);

class ProxySocket : public USocket {
public:
	enum Result {
		NoData,
		Connecting,
		Forwarding,
		Closing,
		Error,
		DelayedConnecting	// H.460.18
	};

	ProxySocket(
		IPSocket *self,
		const char *type,
		WORD buffSize = 1536
	);
	virtual ~ProxySocket() = 0; // abstract class

	// new virtual function
	virtual Result ReceiveData();
	virtual bool ForwardData();
	virtual bool EndSession();
	virtual void OnError() {}

	bool IsConnected() const { return connected; }
	void SetConnected(bool c) { connected = c; }
	bool IsDeletable() const { return deletable; }
	void SetDeletable() { deletable = true; }
	ProxyHandler *GetHandler() const { return handler; }
	void SetHandler(ProxyHandler *h) { handler = h; }

private:
	ProxySocket();
	ProxySocket(const ProxySocket&);
	ProxySocket& operator=(const ProxySocket&);

protected:
	BYTE *wbuffer;
	WORD wbufsize, buflen;

private:
	bool connected, deletable;
	ProxyHandler *handler;
};

class TCPProxySocket : public ServerSocket, public ProxySocket {
public:
	TCPProxySocket(const char *, TCPProxySocket * = NULL, WORD = 0);
	virtual ~TCPProxySocket();

#ifndef LARGE_FDSET
	PCLASSINFO( TCPProxySocket, ServerSocket )

	// override from class PTCPSocket
	virtual PBoolean Accept(PSocket &);
	virtual PBoolean Connect(const Address &, WORD, const Address &);
	virtual PBoolean Connect(const Address &);
#endif
	// override from class ProxySocket
	virtual bool ForwardData();
	virtual bool TransmitData(const PBYTEArray &);

	void RemoveRemoteSocket();
	void SetRemoteSocket(TCPProxySocket * ret) { remote=ret; }
	//TCPProxySocket * GetRemoteSocket() const { return remote; }

private:
	TCPProxySocket();
	TCPProxySocket(const TCPProxySocket&);
	TCPProxySocket& operator=(const TCPProxySocket&);

protected:
	struct TPKTV3 {
		TPKTV3() {}
		TPKTV3(WORD);

		BYTE header, padding;
		WORD length;
	};

	bool ReadTPKT();

	TCPProxySocket *remote;
	PBYTEArray buffer;

private:
	bool InternalWrite(const PBYTEArray &);
	bool SetMinBufSize(WORD);

	BYTE *bufptr;
	TPKTV3 tpkt;
	unsigned tpktlen;
};

#if H323_H450
class X880_Invoke;
class H4501_InterpretationApdu;
#endif

class CallSignalSocket : public TCPProxySocket {
public:
	CallSignalSocket();
	CallSignalSocket(CallSignalSocket *, WORD _port);
	virtual ~CallSignalSocket();

#ifdef LARGE_FDSET
	// override from class TCPProxySocket
	virtual bool Connect(const Address &);
#else
	PCLASSINFO ( CallSignalSocket, TCPProxySocket )
	// override from class TCPProxySocket
	virtual PBoolean Connect(const Address &);
#endif

	// override from class ProxySocket
	virtual Result ReceiveData();
	virtual bool EndSession();
	virtual void OnError();

	void SendReleaseComplete(const H225_CallTerminationCause * = 0);
	void SendReleaseComplete(H225_ReleaseCompleteReason::Choices);

	bool HandleH245Mesg(PPER_Stream &, bool & suppress, H245Socket * h245sock = NULL);
	void OnH245ChannelClosed() { m_h245socket = NULL; }
	Address GetLocalAddr() { return localAddr; }
	Address GetPeerAddr() { return peerAddr; }
	Address GetMasqAddr() { return masqAddr; }
	PINDEX GetCallNumber() const { return m_call ? m_call->GetCallNumber() : 0; }
	H225_CallIdentifier GetCallIdentifier() const { return m_call ? m_call->GetCallIdentifier() : 0; }
	void BuildFacilityPDU(Q931 &, int, const PObject * = NULL);
	void BuildProgressPDU(Q931 &, PBoolean fromDestination);
	void BuildProceedingPDU(Q931 & ProceedingPDU, const H225_CallIdentifier & callId, unsigned crv);
	void BuildSetupPDU(Q931 &, const H225_CallIdentifier & callid, unsigned crv, const PString & destination, bool h245tunneling);
	void RemoveCall();
	bool RerouteCall(CallLeg which, const PString & destination, bool h450transfer);
	void RerouteCaller(PString destination);
	void RerouteCalled(PString destination);
	void PerformConnecting();

	// override from class ServerSocket
	virtual void Dispatch();
	void DispatchNextRoute();
	Result RetrySetup();
	void TryNextRoute();
	void RemoveH245Handler();
	void SaveTCS(const H245_TerminalCapabilitySet & tcs) { m_savedTCS = tcs; }
	H245_TerminalCapabilitySet GetSavedTCS() const { return m_savedTCS; }
#ifdef HAS_H235_MEDIA
    bool HandleH235TCS(H245_TerminalCapabilitySet & tcs);
    bool HandleH235OLC(H245_OpenLogicalChannel & olc);
#endif
	bool CompareH245Socket(H245Socket * sock) const { return sock == m_h245socket; }	// compare pointers !
	
protected:
	CallSignalSocket(CallSignalSocket *);
	
	void SetRemote(CallSignalSocket *);
	bool CreateRemote(H225_Setup_UUIE &setupBody);
public:
#ifdef HAS_H46018
	bool CreateRemote(const H225_TransportAddress & addr);
	bool OnSCICall(H225_CallIdentifier callID, H225_TransportAddress sigAdr);
	bool IsCallFromTraversalServer() const { return m_callFromTraversalServer; }
	bool IsCallToTraversalServer() const { return m_callToTraversalServer; }
	void SetSessionMultiplexDestination(WORD session, bool isRTCP, const H323TransportAddress & toAddress, H46019Side side);
	void SetLCMultiplexDestination(unsigned lc, bool isRTCP, const H323TransportAddress & toAddress, H46019Side side);
	void SetLCMultiplexID(unsigned lc, bool isRTCP, PUInt32b multiplexID, H46019Side side);
	void SetLCMultiplexSocket(unsigned lc, bool isRTCP, int multiplexSocket, H46019Side side);
	const H245Handler * GetH245Handler() const { return m_h245handler; }
#endif

protected:
	CallSignalSocket * GetRemote() const { return (CallSignalSocket *)remote; }

	void ForwardCall(FacilityMsg *msg);

	/// signaling message handlers
	void OnSetup(SignalingMsg *msg);
	void OnCallProceeding(SignalingMsg *msg);
	void OnConnect(SignalingMsg *msg);
	void OnAlerting(SignalingMsg *msg);
	void OnReleaseComplete(SignalingMsg *msg);
	void OnFacility(SignalingMsg *msg);
	void OnProgress(SignalingMsg *msg);
	void OnInformation(SignalingMsg *msg);

	bool OnTunneledH245(H225_ArrayOf_PASN_OctetString &, bool & suppress);
	bool OnFastStart(H225_ArrayOf_PASN_OctetString &, bool);

#if H323_H450
	bool OnH450PDU(H225_ArrayOf_PASN_OctetString &);
	bool OnH450Invoke(X880_Invoke &, H4501_InterpretationApdu &);
	bool OnH450CallTransfer(PASN_OctetString *);
#endif

	template<class UUIE> bool HandleH245Address(UUIE & uu)
	{
		if (uu.HasOptionalField(UUIE::e_h245Address)) {
			if (m_call)
				m_call->SetH245ResponseReceived();
			if (SetH245Address(uu.m_h245Address))
				return (m_h245handler != 0);
			uu.RemoveOptionalField(UUIE::e_h245Address);
			return true;
		}
		return false;
	}
	
	template<class UUIE> bool HandleFastStart(UUIE & uu, bool fromCaller)
	{
		if (!uu.HasOptionalField(UUIE::e_fastStart))
			return false;
			
		if (!fromCaller && m_call)
			m_call->SetFastStartResponseReceived();
			
		return m_h245handler != NULL ? OnFastStart(uu.m_fastStart, fromCaller) : false;
	}

private:
	CallSignalSocket(const CallSignalSocket&);
	CallSignalSocket& operator=(const CallSignalSocket&);
	
	void InternalInit();
	void BuildReleasePDU(Q931 &, const H225_CallTerminationCause *) const;
	// if return false, the h245Address field will be removed
	bool SetH245Address(H225_TransportAddress &);
	bool InternalConnectTo();
	bool ForwardCallConnectTo();

	/** @return
	    A string that can be used to identify a calling number.
	*/
	PString GetCallingStationId(
		/// Q.931/H.225 Setup message with additional data
		const SetupMsg& setup,
		/// additional data
		SetupAuthData& authData
		) const;

	/** @return
	    A string that can be used to identify a calling number.
	*/
	PString GetCalledStationId(
		/// Q.931/H.225 Setup message with additional data
		const SetupMsg& setup,
		/// additional data
		SetupAuthData& authData
		) const;

	/// @return	a number dialed by the user
	PString GetDialedNumber(
		/// Q.931/H.225 Setup message with additional data
		const SetupMsg& setup
		) const;

	void SetCallTypePlan(Q931 *q931);

protected:
	callptr m_call;
	// localAddr is NOT the local address the socket bind to,
	// but the local address that remote socket bind to
	// they may be different in multi-homed environment
	Address localAddr, peerAddr, masqAddr;
	WORD peerPort;

private:
	WORD m_crv;
	H245Handler *m_h245handler;
	H245Socket *m_h245socket;
	bool m_h245Tunneling;
	bool m_isnatsocket;
	Result m_result;
	/// stored for use by ForwardCall, NULL if ForwardOnFacility is disabled
	Q931 *m_setupPdu;
	/// true if the socket is connected to the caller, false if to the callee
	bool m_callerSocket;
	/// H.225.0 protocol version in use by the remote party
	unsigned m_h225Version;
	/// raw Setup data as received from the caller (for failover)
	PBYTEArray m_rawSetup;
	PMutex infomutex;    // Information PDU processing Mutex
	H245_TerminalCapabilitySet m_savedTCS;	// saved tcs to re-send
#ifdef HAS_H46018
	bool m_callFromTraversalServer; // is this call from a traversal server ?
	bool m_callToTraversalServer;
	bool m_senderSupportsH46019Multiplexing;
#endif
};

class CallSignalListener : public TCPListenSocket {
#ifndef LARGE_FDSET
	PCLASSINFO ( CallSignalListener, TCPListenSocket )
#endif
public:
	CallSignalListener(const Address &, WORD);
	~CallSignalListener();

	// override from class TCPListenSocket
	virtual ServerSocket *CreateAcceptor() const;

protected:
	Address m_addr;
};

#ifdef HAS_H46018

class MultiplexRTPListener : public UDPSocket {
#ifndef LARGE_FDSET
	PCLASSINFO ( MultiplexRTPListener, UDPSocket )
#endif
public:
	MultiplexRTPListener(WORD pt, WORD buffSize = 1536);
	virtual ~MultiplexRTPListener();

	virtual int GetOSSocket() const { return os_handle; }

	virtual void ReceiveData();
protected:
	BYTE * wbuffer;
	WORD wbufsize, buflen;
};

class H46019Channel
{
public:
	H46019Channel(const H225_CallIdentifier & callid, WORD session, void * openedBy);

	void Dump() const;

	bool sideAReady(bool isRTCP) const { return isRTCP ? IsSet(m_addrA_RTCP) : IsSet(m_addrA); }
	bool sideBReady(bool isRTCP) const { return isRTCP ? IsSet(m_addrB_RTCP) : IsSet(m_addrB); }
	H46019Channel SwapSides() const; // return a copy with side A and B swapped

	bool IsKeepAlive(void * data, unsigned len, bool isRTCP) { return isRTCP ? true : (len == 12); };

	void HandlePacket(PUInt32b receivedMultiplexID, const H323TransportAddress & fromAddress, void * data, unsigned len, bool isRTCP);
	static void Send(PUInt32b sendMultiplexID, const H323TransportAddress & toAddress, int ossocket, void * data, unsigned len);

//protected:
	H225_CallIdentifier m_callid;
	WORD m_session;
	void * m_openedBy;	// pointer to H245ProxyHandler used as an ID
	void * m_otherSide;	// pointer to H245ProxyHandler used as an ID
	H323TransportAddress m_addrA;
	H323TransportAddress m_addrA_RTCP;
	H323TransportAddress m_addrB;
	H323TransportAddress m_addrB_RTCP;
	PUInt32b m_multiplexID_fromA;
	PUInt32b m_multiplexID_toA;
	PUInt32b m_multiplexID_fromB;
	PUInt32b m_multiplexID_toB;
	int m_osSocketToA;
	int m_osSocketToA_RTCP;
	int m_osSocketToB;
	int m_osSocketToB_RTCP;
	bool m_EnableRTCPStats;
};

class MultiplexedRTPReader : public SocketsReader {
public:
	MultiplexedRTPReader();
	virtual ~MultiplexedRTPReader();

	virtual int GetRTPOSSocket() const { return m_multiplexRTPListener ? m_multiplexRTPListener->GetOSSocket() : INVALID_OSSOCKET; }
	virtual int GetRTCPOSSocket() const { return m_multiplexRTCPListener ? m_multiplexRTCPListener->GetOSSocket() : INVALID_OSSOCKET; }

protected:
	virtual void OnStart();
	virtual void ReadSocket(IPSocket * socket);

	MultiplexRTPListener * m_multiplexRTPListener;
	MultiplexRTPListener * m_multiplexRTCPListener;
};

// handles multiplexed RTP and keepAlives
class MultiplexedRTPHandler : public Singleton<MultiplexedRTPHandler> {
public:
	MultiplexedRTPHandler();
	virtual ~MultiplexedRTPHandler();

	virtual void OnReload() { /* currently not runtime changable */ }

	virtual void AddChannel(const H46019Channel & cha);
	virtual void UpdateChannel(const H46019Channel & cha);
	virtual H46019Channel GetChannel(const H225_CallIdentifier & callid, WORD session, void * openedBy) const;
	virtual void RemoveChannels(const H225_CallIdentifier & callid);
	virtual void DumpChannels(const PString & msg = "") const;

	virtual void HandlePacket(PUInt32b receivedMultiplexID, const H323TransportAddress & fromAddress, void * data, unsigned len, bool isRTCP);

	virtual int GetRTPOSSocket() const { return m_reader ? m_reader->GetRTPOSSocket() : INVALID_OSSOCKET; }
	virtual int GetRTCPOSSocket() const { return m_reader ? m_reader->GetRTCPOSSocket() : INVALID_OSSOCKET; }

	virtual PUInt32b GetMultiplexID(const H225_CallIdentifier & callid, WORD session, void * to);
	virtual PUInt32b GetNewMultiplexID();

protected:
	MultiplexedRTPReader * m_reader;
	mutable PReadWriteMutex listLock;
	list<H46019Channel> m_h46019channels;
	PUInt32b idCounter; // we should make sure this counter is _not_ reset on reload
};
#endif

class ProxyHandler : public SocketsReader {
public:
	ProxyHandler(const PString& name);
	virtual ~ProxyHandler();

	void Insert(TCPProxySocket *);
	void Insert(TCPProxySocket *, TCPProxySocket *);
	void Insert(UDPProxySocket *, UDPProxySocket *);
	void MoveTo(ProxyHandler *, TCPProxySocket *);
	bool IsEmpty() const { return m_socksize == 0; }
	void LoadConfig();
	bool Detach(TCPProxySocket *);
	void Remove(TCPProxySocket *);
	
private:
	// override from class RegularJob
	virtual void OnStart();

	// override from class SocketsReader
	virtual bool BuildSelectList(SocketSelectList &);
	virtual void ReadSocket(IPSocket *);
	virtual void CleanUp();

	void AddPairSockets(IPSocket *, IPSocket *);
	void FlushSockets();
	void Remove(iterator);
	void DetachSocket(IPSocket *socket);

	ProxyHandler();
	ProxyHandler(const ProxyHandler&);
	ProxyHandler& operator=(const ProxyHandler&);
	
private:
	std::list<PTime *> m_removedTime;
	/// time to wait before deleting a closed socket
	PTimeInterval m_socketCleanupTimeout;
};

class HandlerList {
public:
	HandlerList();
	virtual ~HandlerList();

	/** @return
	    Signaling proxy thread to handle a new signaling/H.245/T.120 socket.
	*/
	ProxyHandler* GetSigHandler();

	/** @return
	    RTP proxy thread to handle a pair of new RTP sockets.
	*/
	ProxyHandler* GetRtpHandler();

	void LoadConfig();

private:
	HandlerList(const HandlerList&);
	HandlerList& operator=(const HandlerList&);
	
private:
	/// signaling/H.245/T.120 proxy handling threads
	std::vector<ProxyHandler *> m_sigHandlers;
	/// RTP proxy handling threads
	std::vector<ProxyHandler *> m_rtpHandlers;
	/// number of signaling handlers
	unsigned m_numSigHandlers;
	/// number of RTP handlers
	unsigned m_numRtpHandlers;
	/// next available signaling handler
	unsigned m_currentSigHandler;
	/// next available RTP handler
	unsigned m_currentRtpHandler;
	/// atomic access to the handler lists
	PMutex m_handlerMutex;
};

#endif // PROXYCHANNEL_H
