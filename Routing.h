//////////////////////////////////////////////////////////////////
//
// Routing.h
//
// Routing Mechanism for GNU Gatekeeper
//
// Copyright (c) Citron Network Inc. 2003
// Copyright (c) 2004-2012, Jan Willamowius
//
// This work is published under the GNU Public License version 2 (GPLv2)
// see file COPYING for details.
// We also explicitly grant the right to link this code
// with the OpenH323/H323Plus and OpenSSL library.
//
//////////////////////////////////////////////////////////////////

#ifndef ROUTING_H
#define ROUTING_H "@(#) $Id: Routing.h,v 1.67 2012/03/23 16:59:16 willamowius Exp $"

#include <map>
#include <list>
#include "slist.h"
#include "singleton.h"
#include "RasTbl.h"
#include "stl_supp.h"
#ifdef hasLUA
#include <ptclib/lua.h>
#endif

// forward references to avoid includes
class H225_AdmissionRequest;
class H225_LocationRequest;
class H225_Setup_UUIE;
class H225_Facility_UUIE;
class H225_TransportAddress;
class H225_ArrayOf_AliasAddress;
class Q931;
class SignalingMsg;
template <class> class H225SignalingMsg;
typedef H225SignalingMsg<H225_Setup_UUIE> SetupMsg;
typedef H225SignalingMsg<H225_Facility_UUIE> FacilityMsg;

class RasMsg;
class GkClient;
class GkSQLConnection;

namespace Routing {

class VirtualQueue;

/// An entry for a single call destination route
class Route {
public:
	// a policy can set flags to indicate extra status of a processed request
	enum Flags {
		e_toParent = 1,
		e_toNeighbor = 2,
		e_Reject = 4
	};

	Route();
	Route(
		const PString & policyName,
		const endptr & destEndpoint,
		unsigned priority = 1
		);
	Route(
		const PString & policyName,
		const H225_TransportAddress & destAddr,
		unsigned priority = 1
		);
	Route(
		const PString & policyName,
		const PIPSocket::Address & destIpAddr,
		WORD destPort,
		unsigned priority = 1
		);

	bool operator< (const Route & rhs) { return m_priority < rhs.m_priority; }

	unsigned GetPriority() const { return m_priority; }
	void SetPriority(unsigned p) { m_priority = p; }
	PString AsString() const;
	bool IsFailoverActive(unsigned cause) const;

	H225_TransportAddress m_destAddr; /// destination address for signaling
	endptr m_destEndpoint; /// destination endpoint record (if available)
	PString m_policy; /// name of the policy that found the route
	PString m_routeId; /// optional policy-specific route identifier
	int m_proxyMode; /// per-route proxy mode flag
	unsigned m_flags; /// additional route specific flags
	PString m_destNumber; /// rewritten number (corresponds to Toolkit::RewriteE164)
	PString m_destOutNumber; /// number actually sent to the called party  (corresponds to Toolkit::GWRewriteE164)

protected:
	unsigned char m_rerouteCauses[16]; /// bit flags to trigger rerouting on particular Q931 causes
	unsigned m_priority;	/// priority of this route (less is more important)
};

class RoutingRequest {
public:
	enum Flags {
		e_aliasesChanged = 1,
		e_fromInternal = 2,	// currently not used
		e_fromParent = 4,
		e_fromNeighbor = 8,	// currently not used
		e_Reject = 16
	};
	
	RoutingRequest();
	RoutingRequest(const std::list<Route> & failedRoutes);
	virtual ~RoutingRequest();

	bool AddRoute(const Route & route);
	bool GetFirstRoute(Route & route);
	void RemoveAllRoutes();
	bool HasRoutes() const { return !m_routes.empty(); }
	std::list<Route> & GetRoutes() { return m_routes; }

	void SetRejectReason(unsigned reason) { m_reason = reason; }
	void SetFlag(unsigned f) { m_flags |= f; }
	unsigned GetRejectReason() const { return m_reason; }
	unsigned GetFlags() const { return m_flags; }
	void SetSourceIP(const PString & ip) { m_sourceIP = ip; }
	PString GetSourceIP() const { return m_sourceIP; }
	void SetCallerID(const PString & id) { m_callerID = id; }
	PString GetCallerID() const { return m_callerID; }

private:
	RoutingRequest(const RoutingRequest&);
	RoutingRequest& operator=(const RoutingRequest&);

private:
	int m_reason; /// reject reason, if no routes are found
	unsigned m_flags; /// request specific flags
	std::list<Route> m_routes;
	std::list<Route> m_failedRoutes;
	PString m_sourceIP;
	PString m_callerID;
};

template<class R, class W>
class Request : public RoutingRequest {
public:
	typedef R ReqObj;
	typedef W Wrapper;

	Request(ReqObj & r, Wrapper *w) : m_request(r), m_wrapper(w), m_clientAuthId(0) {}
	Request(ReqObj & r, Wrapper *w, const PString & id, PUInt64 authid)
		: m_request(r), m_wrapper(w), m_callingStationId(id), m_clientAuthId(authid) {}
	Request(ReqObj & r, Wrapper *w, const std::list<Route> &failedRoutes)
		: RoutingRequest(failedRoutes), m_request(r), m_wrapper(w), m_clientAuthId(0) {}

	bool Process();

	ReqObj & GetRequest() { return m_request; }
	Wrapper *GetWrapper() { return m_wrapper; }
	H225_ArrayOf_AliasAddress *GetAliases();
	void SetAliases(H225_ArrayOf_AliasAddress & aliases);
	const H225_TransportAddress * GetDestIP() const;
	const ReqObj & GetRequest() const { return m_request; }
	const Wrapper *GetWrapper() const { return m_wrapper; }
	PString GetCallingStationId() const { return m_callingStationId; }
	PUInt64 GetClientAuthId() const { return m_clientAuthId; }
	const H225_ArrayOf_AliasAddress *GetAliases() const
		{ return const_cast<Request<R, W> *>(this)->GetAliases(); }

private:
	ReqObj & m_request;
	Wrapper *m_wrapper;
	PString m_callingStationId;
	/// ID provided by client during authentication
	PUInt64 m_clientAuthId;
};

typedef Request<H225_AdmissionRequest, RasMsg> AdmissionRequest;
typedef Request<H225_LocationRequest, RasMsg> LocationRequest;
typedef Request<H225_Setup_UUIE, SetupMsg> SetupRequest;
typedef Request<H225_Facility_UUIE, FacilityMsg> FacilityRequest;


class Policy : public SList<Policy> {
public:
	Policy() : m_name("Undefined") { }
	virtual ~Policy() { }

	template <class R> bool HandleRas(Request<R, RasMsg> & request)
	{
		if( IsActive() ) {
			const char * tagname = request.GetWrapper()
				? request.GetWrapper()->GetTagName() : "unknown";
			const unsigned seqnum = request.GetRequest().m_requestSeqNum.GetValue();
			PTRACE(5,"ROUTING\tChecking policy " << m_name
				<< " for the request " << tagname << ' ' << seqnum);
			if( OnRequest(request) ) {
				PTRACE(5, "ROUTING\tPolicy " << m_name
					<< " applied to the request " << tagname << ' ' << seqnum);
				return true;
			}
		}
		return m_next && m_next->HandleRas(request);
	}

	bool Handle(SetupRequest & request);
	bool Handle(FacilityRequest & request);

protected:
	// new virtual function
	// if return false, the policy is disable
	virtual bool IsActive() const { return true; }

	// methods to handle the request
	// return true:  fate of the request is determined (confirm or reject)
	// return false: undetermined, try next
	virtual bool OnRequest(AdmissionRequest &) { return false; }
	virtual bool OnRequest(LocationRequest &)  { return false; }
	virtual bool OnRequest(SetupRequest &)	   { return false; }
	virtual bool OnRequest(FacilityRequest &)  { return false; }

protected:
	/// human readable name for the policy - it should be set inside constructors
	/// of derived policies, default value is "undefined"
	const char* m_name;
};


// the simplest policy, the destination has been explicitly specified
class ExplicitPolicy : public Policy {
public:
	ExplicitPolicy();

	static void MapDestination(H225_AdmissionRequest & arq);
	static void MapDestination(H225_Setup_UUIE & setupBody);
	static void MapDestination(H225_Facility_UUIE & facility);
	static void OnReload();

protected:
	virtual bool OnRequest(AdmissionRequest &);
	// the policy doesn't apply to LocationRequest
	virtual bool OnRequest(SetupRequest &);
	virtual bool OnRequest(FacilityRequest &);

	static std::map<PString, PString> m_destMap;
};


class AliasesPolicy : public Policy {
public:
	AliasesPolicy() { m_name = "Aliases"; }

protected:
	// override from class Policy
	virtual bool OnRequest(AdmissionRequest &);
	virtual bool OnRequest(LocationRequest &);
	virtual bool OnRequest(SetupRequest &);
	virtual bool OnRequest(FacilityRequest &);

	// new virtual function
	virtual bool FindByAliases(RoutingRequest&, H225_ArrayOf_AliasAddress &) = 0;
	virtual bool FindByAliases(LocationRequest&, H225_ArrayOf_AliasAddress&) = 0;
};

// this class passes incoming requests through the chain of routing policies
class Analyzer : public Singleton<Analyzer> {
public:
	Analyzer();
	~Analyzer();

	void OnReload();

	bool Parse(AdmissionRequest &);
	bool Parse(LocationRequest &);
	bool Parse(SetupRequest &);
	bool Parse(FacilityRequest &);

private:
	typedef std::map<PString, Policy *, pstr_prefix_lesser> Rules;

	Policy *Create(const PString & policy);
	Policy *ChoosePolicy(const H225_ArrayOf_AliasAddress *, Rules &);

	Rules m_rules[4];
	PReadWriteMutex m_reloadMutex;
};


// the classical policy, find the dstination from the RegistrationTable
class InternalPolicy : public AliasesPolicy {
public:
	InternalPolicy();

protected:
	virtual bool OnRequest(AdmissionRequest &);
	virtual bool OnRequest(SetupRequest &);

	virtual bool FindByAliases(RoutingRequest &, H225_ArrayOf_AliasAddress &);
	virtual bool FindByAliases(LocationRequest &, H225_ArrayOf_AliasAddress &);
	virtual bool FindByAliases(SetupRequest &, H225_ArrayOf_AliasAddress &);
	virtual bool FindByAliases(AdmissionRequest &, H225_ArrayOf_AliasAddress &);
	
private:
	bool roundRobin;
};


// a policy to route call to parent
class ParentPolicy : public Policy {
public:
	ParentPolicy();

private:
	// override from class Policy
	virtual bool IsActive() const;

	virtual bool OnRequest(AdmissionRequest &);
	virtual bool OnRequest(LocationRequest &);
	virtual bool OnRequest(SetupRequest &);
	virtual bool OnRequest(FacilityRequest &);

	GkClient *m_gkClient;
};


// a policy to look up the destination from DNS
class DNSPolicy : public AliasesPolicy {
public:
	DNSPolicy();

protected:
	virtual bool FindByAliases(RoutingRequest &, H225_ArrayOf_AliasAddress &);
	virtual bool FindByAliases(LocationRequest &, H225_ArrayOf_AliasAddress &);

	virtual bool DNSLookup(const PString & hostname, PIPSocket::Address & addr) const;

	static bool m_resolveNonLocalLRQs;
};

// a policy to route call via external program
class VirtualQueuePolicy : public Policy {
public:
	VirtualQueuePolicy();

protected:
	// override from class Policy
	virtual bool IsActive() const;

	virtual bool OnRequest(AdmissionRequest &);
	virtual bool OnRequest(LocationRequest &);
	virtual bool OnRequest(SetupRequest &);

private:
	VirtualQueue *m_vqueue;
};


class NumberAnalysisPolicy : public Policy {
public:
	struct PrefixEntry {
		string m_prefix;
		int m_minLength;
		int m_maxLength;
	};
	
	NumberAnalysisPolicy();

protected:
	virtual bool OnRequest(AdmissionRequest &);
	virtual bool OnRequest(SetupRequest &);

private:
	NumberAnalysisPolicy(const NumberAnalysisPolicy &);
	NumberAnalysisPolicy& operator=(const NumberAnalysisPolicy &);
	
private:
	typedef vector<PrefixEntry> Prefixes;

	/// list of number prefixes, with min/max number length as values
	Prefixes m_prefixes;
};

// a policy to look up the destination from ENUM Name Server
class ENUMPolicy : public AliasesPolicy {
public:
	ENUMPolicy();

protected:
	virtual bool FindByAliases(RoutingRequest &, H225_ArrayOf_AliasAddress &);
	virtual bool FindByAliases(LocationRequest &, H225_ArrayOf_AliasAddress &);

	bool m_resolveLRQs;
};

class DestinationRoutes {
public:
	DestinationRoutes();
	~DestinationRoutes() { }
	
	bool EndPolicyChain() const { return m_endChain; }
	bool RejectCall() const { return m_reject; }
	void SetRejectCall(bool reject) { m_reject = reject; m_endChain = true; }
	unsigned int GetRejectReason() const { return m_rejectReason; }
	void SetRejectReason(unsigned int reason) { m_rejectReason = reason; }
	bool ChangeAliases() const { return m_aliasesChanged; }
	H225_ArrayOf_AliasAddress GetNewAliases() const { return m_newAliases; }
	void SetNewAliases(const H225_ArrayOf_AliasAddress & aliases) { m_newAliases = aliases; m_aliasesChanged = true; }

	void AddRoute(const Route & route, bool endChain = true);

	std::list<Route> m_routes;

protected:
	bool m_endChain;
	bool m_reject;
	unsigned int m_rejectReason;
	bool m_aliasesChanged;
	H225_ArrayOf_AliasAddress m_newAliases;
};

// superclass for dynamic policies like sql and lua scripring
class DynamicPolicy : public Policy {
public:
	DynamicPolicy();
	virtual ~DynamicPolicy() { }

protected:
	virtual bool IsActive() const { return m_active; }

	virtual bool OnRequest(AdmissionRequest &);
	virtual bool OnRequest(LocationRequest &);
	virtual bool OnRequest(SetupRequest &);

	virtual void RunPolicy(
		/*in */
		const PString & source,
		const PString & calledAlias,
		const PString & calledIP,
		const PString & caller,
		const PString & callingStationId,
		const PString & callid,
		const PString & messageType,
		const PString & clientauthid,
		/* out: */
		DestinationRoutes & destination) = 0;

protected:
	// active ?
	bool m_active;
};

// a policy to route calls via an SQL database
class SqlPolicy : public DynamicPolicy {
public:
	SqlPolicy();
	virtual ~SqlPolicy();

protected:
	virtual void RunPolicy(
		/*in */
		const PString & source,
		const PString & calledAlias,
		const PString & calledIP,
		const PString & caller,
		const PString & callingStationId,
		const PString & callid,
		const PString & messageType,
		const PString & clientauthid,
		/* out: */
		DestinationRoutes & destination);

protected:
	// connection to the SQL database
	GkSQLConnection* m_sqlConn;
	// parametrized query string for the routing query
	PString m_query;
	// query timeout
	long m_timeout;
};

#ifdef hasLUA
// a policy to route calls with LUA
class LuaPolicy : public DynamicPolicy {
public:
	LuaPolicy();
	virtual ~LuaPolicy();

protected:
	virtual void RunPolicy(
		/*in */
		const PString & source,
		const PString & calledAlias,
		const PString & calledIP,
		const PString & caller,
		const PString & callingStationId,
		const PString & callid,
		const PString & messageType,
		const PString & clientauthid,
		/* out: */
		DestinationRoutes & destination);

protected:
	// LUA interpreter
	PLua m_lua;
	// script to run
	PString m_script;
};
#endif

// a policy to route all calls to one default endpoint
class CatchAllPolicy : public Policy {
public:
	CatchAllPolicy();
	virtual ~CatchAllPolicy() { }

protected:
	virtual bool OnRequest(AdmissionRequest & request) { return CatchAllRoute(request); }
	virtual bool OnRequest(LocationRequest & request) { return CatchAllRoute(request); }
	virtual bool OnRequest(SetupRequest & request) { return CatchAllRoute(request); }

	bool CatchAllRoute(RoutingRequest & request) const;
	
	PString m_catchAllAlias;
	PString m_catchAllIP;
};



template<class R, class W>
inline bool Request<R, W>::Process()
{
	return Analyzer::Instance()->Parse(*this);
}

/** A class that supports ACD (Automatic Call Distribution). A call
    made to specified alias(-es) (called virtual queue) is signalled
	via the GK status line to an external application (an ACD application)
	that decides where the call should be routed (e.g. what agent should
	answe the call). Basically, it rewrites virtual queue alias
	into the alias of the specified agent.

	The route request is uniquely identified by (EndpointIdentifier,CRV)
	values pair.
*/
class VirtualQueue
{
public:
	VirtualQueue();
	~VirtualQueue();

	/// reload settings from the config file
	void OnReload();

	/** @return
		True if there is at least one virtual queue configured.
	*/
	bool IsActive() const { return m_active; }

	/** Send RouteRequest to the GK status line	and wait
		for a routing decision to be made by some external application
		(ACD application).

		@return
		True if the external application routed the call (either by specifying
		an alias or by rejecting the call), false if timed out waiting
		for the routing decision.
		If the request was rejected, destinationInfo is set to an epmty array
		(0 elements).
	*/
	bool SendRouteRequest(
		/// source IP of the request (endpoint for ARQ, gatekeeper for LRQ)
		const PString& source,
		/// calling endpoint
		const PString& epid,
		/// CRV (Call Reference Value) of the call associated with this request
		unsigned crv,
		/// destination (virtual queue) aliases as specified
		/// by the calling endpoint (modified by this function on successful return)
		H225_ArrayOf_AliasAddress* destinationInfo,
		/// destinationCallSignalAddr (optionally set by this function on successful return)
		PString* callSigAdr,
		/// bind IP for BindAndRouteToGateway
		PString* bindIP,
		/// caller ID
		PString* callerID,
		/// should the call be rejected modified by this function on return)
		bool & reject,
		/// an actual virtual queue name (should be present in destinationInfo too)
		const PString& vqueue,
		/// a sequence of aliases for the calling endpoint
		/// (in the "alias:type[=alias:type]..." format)
		const PString& sourceInfo,
		/// the callID as string
		const PString& callID,
		/// the called IP for unregistered calls
		const PString& calledip = "unknown",
		/// vendor string of caller
		const PString& vendorString = "unknown"
		);

	/** Make a routing decision for a pending route request (inserted
		by SendRequest).

		@return
		True if the matching pending request has been found, false otherwise.
	*/
	bool RouteToAlias(
		/// aliases for the routing target (an agent that the call will be routed to)
		/// that will replace the original destination info
		const H225_ArrayOf_AliasAddress& agent,
		/// ip that will replace the destinationCallSignalAddress (RouteToGateway)
		/// used only if set (port != 0)
		const PString& destinationip,
		/// identifier of the endpoint associated with the route request
		const PString& callingEpId,
		/// CRV of the call associated with the route request
		unsigned crv,
		/// callID of the call associated with the route request
		const PString& callID,
		// outgoing IP or empty
		const PString& bindIP,
		// callerID or empty
		const PString& callerID,
		/// should this call be rejected
		bool reject = false
		);

	/** Make a routing decision for a pending route request (inserted
		by SendRequest).

		@return
		True if the matching pending request has been found, false otherwise.
	*/
	bool RouteToAlias(
		/// alias for the routing target that
		/// will replace the original destination info
		const PString& agent,
		/// will replace the original destinationCallSignallAddress
		const PString& destinationip, 		
		/// identifier of the endpoint associated with the route request
		const PString& callingEpId,
		/// CRV of the call associated with the route request
		unsigned crv,
		/// callID of the call associated with the route request
		const PString& callID,
		// outgoing IP or empty
		const PString& bindIP,
		// callerID or empty
		const PString& callerID,
		/// should this call be rejected
		bool reject = false
		);

	/** Reject a pending route request (inserted by SendRequest).

		@return
		True if the matching pending request has been found, false otherwise.
	*/
	bool RouteReject(
		/// identifier of the endpoint associated with the route request
		const PString& callingEpId,
		/// CRV of the call associated with the route request
		unsigned crv,
		/// callID of the call associated with the route request
		const PString& callID
		);

	/** @return
		True if the specified alias matches a name of an existing virtual queue.
	*/
	bool IsDestinationVirtualQueue(
		const PString& destinationAlias /// alias to be matched
		) const;

private:
	/// a holder for a pending route request
	struct RouteRequest
	{
		RouteRequest(
			const PString& callingEpId,
			unsigned crv,
			const PString& callID,
			H225_ArrayOf_AliasAddress* agent,
			PString* callsignaladdr,
			PString* bindIP,
			PString* callerID
			)
			:
			m_callingEpId((const char*)callingEpId), m_crv(crv), m_callID(callID),
			m_agent(agent), m_callsignaladdr(callsignaladdr), m_sourceIP(bindIP),
			m_callerID(callerID), m_reject(false) {}

		/// identifier for the endpoint associated with this request
		PString m_callingEpId;
		/// CRV for the call associated with this request
		unsigned m_crv;
		/// callID for the call associated with this request
		PString m_callID;
		/// aliases for the virtual queue matched (on input)
		/// aliases for the target agent - target route (on output)
		H225_ArrayOf_AliasAddress* m_agent;
		/// destinationCallSignallAddress for the target agent - target route IF NOT NULL
		PString* m_callsignaladdr;
		/// bindIP or empty
		PString* m_sourceIP;
		/// callerID or empty
		PString* m_callerID;
		/// should this call be rejected
		bool m_reject;
		/// a synchronization point for signaling that routing decision
		/// has been made by the external application
		PSyncPoint m_sync;
	};

	typedef std::list<RouteRequest *> RouteRequests;

	RouteRequest *InsertRequest(
		/// identifier for the endpoint associated with this request
		const PString& callingEpId,
		/// CRV for the call associated with this request
		unsigned crv,
		/// callID for the call associated with this request
		const PString& callID,
		/// a pointer to an array to be filled with agent aliases
		/// when the routing decision has been made
		H225_ArrayOf_AliasAddress* agent,
		/// a pointer to a string to be filled with a callSignalAddress
		/// when the routing decision has been made (optional)
		PString* callSigAdr,
		/// bind IP for BindAndRouteToGateway
		PString* bindIP,
		/// caller ID
		PString* callerID,
		/// set by the function to true if another route request for the same
		/// call is pending
		bool& duplicate
		);

	/// an array of names (aliases) for the virtual queues
	PStringArray m_virtualQueueAliases;
	/// an array of prefixes for the virtual queues
	PStringArray m_virtualQueuePrefixes;
	/// a regular expression for the virtual queues
	PString m_virtualQueueRegex;
	/// virtual queues enabled/disabled
	bool m_active;
	/// time (in milliseconds) to wait for a routing decision to be made
	long m_requestTimeout;
	/// a list of active (pending) route requests
	RouteRequests m_pendingRequests;
	/// a mutex protecting pending requests and virtual queues lists
	PMutex m_listMutex;
};


} // end of namespace Routing

#endif // ROUTING_H
