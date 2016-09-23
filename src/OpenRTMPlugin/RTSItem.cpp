/*!
 * @brief  This is a definition of RTSystemEditorPlugin.

 * @author Hisashi Ikari 
 * @file
 */
#include "RTSItem.h"
#include "RTSNameServerView.h"
#include "RTSCommonUtil.h"
#include "RTSDiagramView.h"
#include <cnoid/ItemManager>
#include <cnoid/Archive>
#include <cnoid/CorbaUtil>
#include <boost/algorithm/string.hpp>

#include "gettext.h"

using namespace cnoid;
using namespace std;
using namespace std::placeholders;


namespace cnoid 
{ 

class RTSystemItemImpl
{
public:
    RTSystemItem* self;
    NamingContextHelper ncHelper;
    Connection locationChangedConnection;
    map<string, RTSCompPtr> rtsComps;
    map<string, RTSConnectionPtr> rtsConnections;
    map<string, RTSConnectionPtr> deletedRtsConnections;

    RTSystemItemImpl(RTSystemItem* self);
    RTSystemItemImpl(RTSystemItem* self, const RTSystemItemImpl& org);
    ~RTSystemItemImpl();

    void initialize();
    void onLocationChanged(std::string host, int port);
    RTSComp* addRTSComp(const string& name, const QPointF& pos);
    void deleteRTSComp(const string& name);
    RTSComp* nameToRTSComp(const string& name);
    bool compIsAlive(RTSComp* rtsComp);
    RTSConnection* addRTSConnection(const string& id, const string& name,
            RTSPort* sourcePort, RTSPort* targetPort, const string& dataflow, const string& subscription);
    RTSConnection* addRTSConnectionName(const string& id, const string& name,
            const string& sourceComp, const string& sourcePort,
            const string& targetComp, const string& targetPort,
            const string& dataflow, const string& subscription);
    void deleteRTSConnection(const string& id);
    void connectionCheck();
    void RTSCompToConnectionList(const RTSComp* rtsComp,
            list<RTSConnection*>& rtsConnectionList, int mode);
    void diagramViewUpdate();
};

}

RTSPort::RTSPort(const string& name_, PortService_var port_, RTSComp* parent)
    : rtsComp(parent)
{
    isInPort = true;
    name = name_;
    port = port_;
}


bool RTSPort::connected()
{
    if(!NamingContextHelper::isObjectAlive(port))
        return false;

    ConnectorProfileList_var connectorProfiles = port->get_connector_profiles();
    return connectorProfiles->length()!=0;
}


bool RTSPort::connectedWith(RTSPort* target)
{
    if(CORBA::is_nil(port) || port->_non_existent() || CORBA::is_nil(target->port) || target->port->_non_existent())
        return false;

    ConnectorProfileList_var connectorProfiles = port->get_connector_profiles();
    for(CORBA::ULong i=0; i < connectorProfiles->length(); ++i){
        ConnectorProfile& connectorProfile = connectorProfiles[i];
        PortServiceList& connectedPorts = connectorProfile.ports;

        for(CORBA::ULong j=0; j < connectedPorts.length(); ++j){
            PortService_ptr connectedPortRef = connectedPorts[j];
            if(connectedPortRef->_is_equivalent(target->port)){
                return true;
            }
        }
    }
    return false;
}


bool RTSPort::checkConnectablePort(RTSPort* target)
{
    if(rtsComp == target->rtsComp)
        return false;
    if((isInPort && target->isInPort) ||
       (isInPort && target->isServicePort) ||
       (!isInPort && !isServicePort && !target->isInPort) ||
       (isServicePort && !target->isServicePort))
        return false;

    return true;
}


RTSPort* RTSComp::nameToRTSPort(const string& name)
{
    map<string, RTSPortPtr>::iterator it = inPorts.find(name);
    if(it!=inPorts.end())
        return it->second.get();
    it = outPorts.find(name);
    if(it!=outPorts.end())
        return it->second.get();
    return 0;
}

RTSConnection::RTSConnection(const string& id, const string& name,
        const string& sourceRtcName,const string& sourcePortName,
        const string& targetRtcName, const string& targetPortName)
    : id(id), name(name), sourceRtcName(sourceRtcName), sourcePortName(sourcePortName),
      targetRtcName(targetRtcName), targetPortName(targetPortName)
{
    pushPolicy = "all";
    pushRate = 1000.0;
    sinterface = "corba_cdr";
}


bool RTSConnection::connect()
{
    if(CORBA::is_nil(sourcePort->port) || sourcePort->port->_non_existent() || CORBA::is_nil(targetPort->port) || targetPort->port->_non_existent())
        return false;

    ConnectorProfile cprof;
    cprof.connector_id = CORBA::string_dup(id.c_str());
    cprof.name = CORBA::string_dup(name.c_str());
    cprof.ports.length(2);
    cprof.ports[0] = PortService::_duplicate(sourcePort->port);
    cprof.ports[1] = PortService::_duplicate(targetPort->port);

    CORBA_SeqUtil::push_back(cprof.properties,
            NVUtil::newNV("dataport.dataflow_type",
                    dataflow.c_str()));
    CORBA_SeqUtil::push_back(cprof.properties,
            NVUtil::newNV("dataport.interface_type",
                    sinterface.c_str()));
    CORBA_SeqUtil::push_back(cprof.properties,
            NVUtil::newNV("dataport.subscription_type",
                    subscription.c_str()));

    RTC::ReturnCode_t result = sourcePort->port->connect(cprof);
    if(result == RTC::RTC_OK){
        PortProfile_var portprofile = sourcePort->port->get_port_profile();
        ConnectorProfileList connections = portprofile->connector_profiles;
        for(CORBA::ULong i = 0; i < connections.length(); i++){
            ConnectorProfile& connector = connections[i];
            PortServiceList& connectedPorts = connector.ports;
            for(CORBA::ULong j=0; j < connectedPorts.length(); ++j){
                PortService_ptr connectedPortRef = connectedPorts[j];
                PortProfile_var connectedPortProfile = connectedPortRef->get_port_profile();
                string portName = string(connectedPortProfile->name);
                if(portName == targetPortName){
                    id = string(connector.connector_id);
                    return true;
                }
            }
        }
        return false;
    }else{
        return false;
    }
}


bool RTSConnection::disConnect()
{
    if(CORBA::is_nil(sourcePort->port) || sourcePort->port->_non_existent())
        return false;

    ReturnCode_t result = sourcePort->port->disconnect(id.c_str());
    if(result == RTC_OK)
        return true;
    else
        return false;
}


RTSComp::RTSComp(const string& name, RTC::RTObject_ptr rtc, RTSystemItemImpl* impl, const QPointF& pos) :
        impl(impl), pos(pos), name(name)
{
    rtc_ = 0;

    if(!NamingContextHelper::isObjectAlive(rtc))
        return;

    rtc_ = rtc;
    ComponentProfile_var cprofile = rtc_->get_component_profile();

    ownedExeContList = rtc_->get_owned_contexts();
    participatingExeContList = rtc_->get_participating_contexts();

    PortServiceList_var portlist = rtc_->get_ports();
    for (CORBA::ULong i = 0; i < portlist->length(); i++) {
        PortProfile_var portprofile = portlist[i]->get_port_profile();
        coil::Properties pproperties = NVUtil::toProperties(portprofile->properties);
        string portType = pproperties["port.port_type"];
        RTSPortPtr rtsPort = new RTSPort(string(portprofile->name), portlist[i], this);
        if (boost::iequals(portType, "CorbaPort")){
            rtsPort->isServicePort = true;
            rtsPort->isInPort = false;
            outPorts.insert(pair<string, RTSPortPtr>(rtsPort->name, rtsPort));
        }else{
            rtsPort->isServicePort = false;
            if(boost::iequals(portType, "DataInPort"))
                inPorts.insert(pair<string, RTSPortPtr>(rtsPort->name, rtsPort));
            else{
                rtsPort->isInPort = false;
                outPorts.insert(pair<string, RTSPortPtr>(rtsPort->name, rtsPort));
            }
        }
    }

    connectionCheck();
}


void RTSComp::connectionCheckSub(RTSPort* rtsPort)
{
    if(CORBA::is_nil(rtsPort->port) || rtsPort->port->_non_existent())
        return;

    PortProfile_var portprofile = rtsPort->port->get_port_profile();
    ConnectorProfileList connectorProfiles = portprofile->connector_profiles;
    for(CORBA::ULong i = 0; i < connectorProfiles.length(); i++){
        ConnectorProfile& connectorProfile = connectorProfiles[i];
        string id = string(connectorProfile.connector_id);
        map<string, RTSConnectionPtr>::iterator itr = impl->rtsConnections.find(id);
        if(itr!=impl->rtsConnections.end()){
            impl->deletedRtsConnections.erase(id);
            continue;
        }
        PortServiceList& connectedPorts = connectorProfile.ports;
        for(CORBA::ULong j=0; j < connectedPorts.length(); ++j){
            PortService_ptr connectedPortRef = connectedPorts[j];
            PortProfile_var connectedPortProfile = connectedPortRef->get_port_profile();
            string portName = string(connectedPortProfile->name);
            vector<string> target;
            RTCCommonUtil::splitPortName(portName, target);
            if(target[0] != name){
                RTSComp* targetRTC = impl->nameToRTSComp(target[0]);
                if(targetRTC){
                    RTSConnectionPtr rtsConnection = new RTSConnection(
                            id, string(connectorProfile.name),
                            name, string(portprofile->name),
                            target[0], portName);
                    coil::Properties properties = NVUtil::toProperties(connectorProfile.properties);
                    rtsConnection->dataflow = properties["dataport.dataflow_type"];
                    rtsConnection->subscription = properties["dataport.subscription_type"];
                    rtsConnection->srcRTC = this;
                    rtsConnection->sourcePort = nameToRTSPort(rtsConnection->sourcePortName);
                    rtsConnection->targetRTC = targetRTC;
                    rtsConnection->targetPort = targetRTC->nameToRTSPort(rtsConnection->targetPortName);
                    impl->rtsConnections.insert(pair<string, RTSConnectionPtr>(id, rtsConnection));
                }
            }
        }
    }
}


void RTSComp::connectionCheck()
{
    for(map<string, RTSPortPtr>::iterator it = inPorts.begin(); it != inPorts.end(); it++)
        connectionCheckSub(it->second.get());
    for(map<string, RTSPortPtr>::iterator it = outPorts.begin(); it != outPorts.end(); it++)
        connectionCheckSub(it->second.get());
}


bool RTSComp::isActive()
{
    if(CORBA::is_nil(rtc_) || rtc_->_non_existent())
        return false;

    for(CORBA::ULong i=0; i<ownedExeContList->length(); i++){
        if(ownedExeContList[i]->get_component_state(rtc_) == RTC::ACTIVE_STATE)
            return true;
    }
    for(CORBA::ULong i=0; i<participatingExeContList->length(); i++){
        if(participatingExeContList[i]->get_component_state(rtc_) == RTC::ACTIVE_STATE)
            return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////
void RTSystemItem::initialize(ExtensionManager* ext)
{
    ext->itemManager().registerClass<RTSystemItem>(N_("RTSystemItem"));
    ext->itemManager().addCreationPanel<RTSystemItem>();
}


RTSystemItem::RTSystemItem()
{
    impl = new RTSystemItemImpl(this);
    autoConnection = true;
}


RTSystemItemImpl::RTSystemItemImpl(RTSystemItem* self)
    : self(self)
{
    initialize();
}


RTSystemItem::RTSystemItem(const RTSystemItem& org)
    : Item(org)
{
    impl = new RTSystemItemImpl(this, *org.impl);
    autoConnection = org.autoConnection;
}


RTSystemItemImpl::RTSystemItemImpl(RTSystemItem* self, const RTSystemItemImpl& org)
    : self(self)
{
    initialize();
}


RTSystemItem::~RTSystemItem()
{
    delete impl;
}


RTSystemItemImpl::~RTSystemItemImpl()
{
    locationChangedConnection.disconnect();
}


Item* RTSystemItem::doDuplicate() const
{
    return new RTSystemItem(*this);
}


void RTSystemItemImpl::initialize()
{
    RTSNameServerView* nsView = RTSNameServerView::instance();
    if(nsView){
        if(!locationChangedConnection.connected()){
            locationChangedConnection = nsView->sigLocationChanged().connect(
                    std::bind(&RTSystemItemImpl::onLocationChanged, this, _1, _2));
            ncHelper.setLocation(nsView->getHost(), nsView->getPort());
        }
    }
}


void RTSystemItemImpl::onLocationChanged(string host, int port)
{
    ncHelper.setLocation(host, port);
}


RTSComp* RTSystemItem::nameToRTSComp(const string& name)
{
    impl->nameToRTSComp(name);
}


RTSComp* RTSystemItemImpl::nameToRTSComp(const string& name)
{
    map<string, RTSCompPtr>::iterator it = rtsComps.find(name);
    if(it==rtsComps.end())
        return 0;
    else
        return it->second.get();
}


RTSComp* RTSystemItem::addRTSComp(const string& name, const QPointF& pos)
{
    return impl->addRTSComp(name, pos);
}


RTSComp* RTSystemItemImpl::addRTSComp(const string& name, const QPointF& pos)
{
    if(!nameToRTSComp(name)){
        RTC::RTObject_ptr rtc = ncHelper.findObject<RTC::RTObject>(name, "rtc");
        RTSCompPtr rtsComp = new RTSComp(name, rtc, this, pos);
        rtsComps.insert(pair<string, RTSCompPtr>(name, rtsComp));
        return rtsComp.get();
    }
    return 0;
}


void RTSystemItem::deleteRTSComp(const string& name)
{
    impl->deleteRTSComp(name);
}


void RTSystemItemImpl::deleteRTSComp(const string& name)
{
    rtsComps.erase(name);
}

bool RTSystemItem::compIsAlive(RTSComp* rtsComp)
{
    return impl->compIsAlive(rtsComp);
}

bool RTSystemItemImpl::compIsAlive(RTSComp* rtsComp)
{
    return ncHelper.isObjectAlive(rtsComp->rtc_);
/*
    if(!rtsComp->rtc_){
        RTC::RTObject_ptr rtc = ncHelper.findObject<RTC::RTObject>(rtsComp->name, "rtc");
        if(ncHelper.isObjectAlive(rtc)){
            rtsComp->setRtc(rtc);
            return true;
        }else
            return false;
    }else{
        if(ncHelper.isObjectAlive(rtsComp->rtc_))
            return true;
        else{
            rtsComp->setRtc(0);
            return false;
        }
    }
    */
}

RTSConnection* RTSystemItem::addRTSConnection(const string& id, const string& name,
            RTSPort* sourcePort, RTSPort* targetPort, const string& dataflow, const string& subscription)
{
    return impl->addRTSConnection(id, name, sourcePort, targetPort, dataflow, subscription);
}


RTSConnection* RTSystemItemImpl::addRTSConnectionName(const string& id, const string& name,
        const string& sourceCompName, const string& sourcePortName,
        const string& targetCompName, const string& targetPortName,
        const string& dataflow, const string& subscription)
{
    RTSPort* sourcePort;
    RTSPort* targetPort;
    RTSComp* sourceRtc = nameToRTSComp(sourceCompName);
    if(sourceRtc){
        sourcePort = sourceRtc->nameToRTSPort(sourcePortName);
    }
    RTSComp* targetRtc = nameToRTSComp(targetCompName);
    if(targetRtc){
        targetPort = targetRtc->nameToRTSPort(targetPortName);
    }
    if(sourcePort && targetPort){
        addRTSConnection(id, name, sourcePort, targetPort, dataflow, subscription);
    }
}


RTSConnection* RTSystemItemImpl::addRTSConnection(const string& id, const string& name,
            RTSPort* sourcePort, RTSPort* targetPort, const string& dataflow, const string& subscription)
{
    if(CORBA::is_nil(sourcePort->port) || sourcePort->port->_non_existent() || CORBA::is_nil(targetPort->port) || targetPort->port->_non_existent())
        return 0;

    RTSConnectionPtr rtsConnection = new RTSConnection(
            id, name,
            sourcePort->rtsComp->name, sourcePort->name,
            targetPort->rtsComp->name, targetPort->name);
    rtsConnection->srcRTC = sourcePort->rtsComp;
    rtsConnection->sourcePort = sourcePort;
    rtsConnection->targetRTC = targetPort->rtsComp;
    rtsConnection->targetPort = targetPort;
    rtsConnection->dataflow = dataflow;
    rtsConnection->subscription = subscription;
    if(rtsConnection->connect()){
        rtsConnections.insert(pair<string, RTSConnectionPtr>(rtsConnection->id, rtsConnection));
        return rtsConnection;
    }else
        return 0;
}


void RTSystemItem::connectionCheck()
{
    impl->connectionCheck();
}


void RTSystemItemImpl::connectionCheck()
{
    deletedRtsConnections = rtsConnections;
    for(map<string, RTSCompPtr>::iterator it = rtsComps.begin();
            it != rtsComps.end(); it++){
        it->second->connectionCheck();
    }
}


void RTSystemItem::RTSCompToConnectionList(const RTSComp* rtsComp,
        list<RTSConnection*>& rtsConnectionList, int mode)
{
    impl->RTSCompToConnectionList(rtsComp, rtsConnectionList, mode);
}


void RTSystemItemImpl::RTSCompToConnectionList(const RTSComp* rtsComp,
        list<RTSConnection*>& rtsConnectionList, int mode)
{
    for(map<string, RTSConnectionPtr>::iterator it = rtsConnections.begin();
            it != rtsConnections.end(); it++){
        switch (mode){
        case 0:
        default :
            if(it->second->sourceRtcName == rtsComp->name || it->second->targetRtcName == rtsComp->name)
                rtsConnectionList.push_back(it->second);
            break;
        case 1:
            if(it->second->sourceRtcName == rtsComp->name)
                rtsConnectionList.push_back(it->second);
            break;
        case 2:
            if(it->second->targetRtcName == rtsComp->name)
                rtsConnectionList.push_back(it->second);
            break;
        }
    }
}


map<string, RTSCompPtr>& RTSystemItem::rtsComps()
{
    return impl->rtsComps;
}


map<string, RTSConnectionPtr>& RTSystemItem::rtsConnections()
{
    return impl->rtsConnections;
}


map<string, RTSConnectionPtr>& RTSystemItem::deletedRtsConnections()
{
    return impl->deletedRtsConnections;
}


void RTSystemItem::deleteRtsConnection(const string& id)
{
    impl->deleteRTSConnection(id);
}


void RTSystemItemImpl::deleteRTSConnection(const string& id)
{
    rtsConnections.erase(id);
}


void RTSystemItem::doPutProperties(PutPropertyFunction& putProperty)
{
    putProperty(_("Auto Connection"), autoConnection, changeProperty(autoConnection));
}


bool RTSystemItem::store(Archive& archive)
{
    archive.write("AutoConnection", autoConnection);

    map<string, RTSCompPtr>& comps = impl->rtsComps;
    Listing& s = *archive.createFlowStyleListing("RTSComps");

    for(map<string, RTSCompPtr>::iterator it = comps.begin();
            it != comps.end(); it++){
        RTSComp* comp = it->second.get();
        ostringstream ss;
        ss << "\"" << comp->name << " " << comp->pos.x() << " " << comp->pos.y() << "\"";
        s.append(ss.str());
    }

    map<string, RTSConnectionPtr>& connections = impl->rtsConnections;
    Listing& s0 = *archive.createFlowStyleListing("RTSConnections");

    for(map<string, RTSConnectionPtr>::iterator it = connections.begin();
            it != connections.end(); it++){
        RTSConnection* connect = it->second.get();
        ostringstream ss;
        ss << "\"" << connect->name << " " << connect->sourceRtcName << " " << connect->sourcePortName <<
                " " << connect->targetRtcName << " " << connect->targetPortName <<
                " " << connect->dataflow << " " << connect->subscription << "\"";
        s0.append(ss.str());
    }

    return true;

}


bool RTSystemItem::restore(const Archive& archive)
{
    archive.read("AutoConnection", autoConnection);

    const Listing& s = *archive.findListing("RTSComps");
    if(s.isValid()){
        for(int i=0; i<s.size(); i++){
            string value = s[i].toString();
            vector<string> results;
            boost::split(results, value, boost::is_any_of(" "));
            string& name = results[0];
            istringstream ssx(results[1]);
            istringstream ssy(results[2]);
            double x,y;
            ssx >> x; ssy >> y;
            archive.addPostProcess(
                    std::bind(&RTSystemItem::addRTSComp, this, name, QPointF(x,y)));
        }
    }

    if(autoConnection){
        const Listing& s0 = *archive.findListing("RTSConnections");
        if(s0.isValid()){
            for(int i=0; i<s0.size(); i++){
                string value = s0[i].toString();
                vector<string> results;
                boost::split(results, value, boost::is_any_of(" "));
                archive.addPostProcess(
                        std::bind(&RTSystemItemImpl::addRTSConnectionName, impl, "", results[0],
                                results[1], results[2], results[3], results[4], results[5], results[6]));
            }
        }
    }

    archive.addPostProcess(std::bind(&RTSystemItemImpl::diagramViewUpdate, impl));

    return true;
}


void RTSystemItemImpl::diagramViewUpdate()
{
    RTSNameServerView* nsView = RTSNameServerView::instance();
    if(nsView){
        nsView->updateView();
    }
    RTSDiagramView* diagramView = RTSDiagramView::instance();
    if(diagramView){
        diagramView->updateView();
    }
}