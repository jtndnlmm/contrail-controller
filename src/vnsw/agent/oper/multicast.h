/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef multicast_agent_oper_hpp
#define multicast_agent_oper_hpp

#include <oper/nexthop.h>
#include <oper/inet4_ucroute.h>
#include <oper/inet4_mcroute.h>
#include <netinet/in.h>
#include <net/ethernet.h>
#include <cmn/agent_cmn.h>
#include <oper/vrf.h>
#include <oper/interface.h>
#include <oper/agent_types.h>
#include <sandesh/sandesh_trace.h>

using namespace boost::uuids;

extern SandeshTraceBufferPtr MulticastTraceBuf;

#define MCTRACE(obj, ...)                                                        \
do {                                                                             \
    Multicast##obj::TraceMsg(MulticastTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while (false);                                                                 \

struct OlistTunnelEntry {
    OlistTunnelEntry() : label_(0), daddr_(0), tunnel_bmap_(0) { }
    OlistTunnelEntry(uint32_t label, const Ip4Address &addr,
                     TunnelType::TypeBmap bmap) : 
        label_(label), daddr_(addr), tunnel_bmap_(bmap) { }
    virtual ~OlistTunnelEntry() { }

    uint32_t label_;
    Ip4Address daddr_;
    TunnelType::TypeBmap tunnel_bmap_;
};

typedef std::vector<OlistTunnelEntry> TunnelOlist;

class MulticastGroupObject {
public:
    MulticastGroupObject(const std::string &vrf_name, 
                         const Ip4Address &grp_addr,
                         const std::string &vn_name) :
        vrf_name_(vrf_name), grp_address_(grp_addr), vn_name_(vn_name) {
        boost::system::error_code ec;
        src_address_ =  IpAddress::from_string("0.0.0.0", ec).to_v4();
        src_mpls_label_ = 0;
        local_olist_.clear();
        deleted_ = false;
    };     
    MulticastGroupObject(const std::string &vrf_name, 
                         const Ip4Address &grp_addr,
                         const Ip4Address &src_addr) : 
        vrf_name_(vrf_name), grp_address_(grp_addr), src_address_(src_addr) {
        src_mpls_label_ = 0;
        local_olist_.clear();
        deleted_ = false;
    };     
    virtual ~MulticastGroupObject() { };

    void SetSourceMPLSLabel(uint32_t label); 
    uint32_t GetSourceMPLSLabel() const { return src_mpls_label_; };

    //Add local member is local VM in server.
    bool AddLocalMember(const uuid &intf_uuid) { 
        if (std::find(local_olist_.begin(), local_olist_.end(), intf_uuid) !=
            local_olist_.end()) {
            return false;
        }
        local_olist_.push_back(intf_uuid); 
        return true;
    };

    //Delete local member from VM list in server 
    bool DeleteLocalMember(const uuid &intf_uuid) { 
        std::list<uuid>::iterator it = std::find(local_olist_.begin(), 
                                                 local_olist_.end(), intf_uuid);
        if (it != local_olist_.end()) {
            local_olist_.erase(it); 
            return true;
        }
        return false;
    };
    uint32_t GetLocalListSize() { return local_olist_.size(); };
    TunnelOlist &GetTunnelOlist() { return tunnel_olist_; };
    //Add remote server and label in fabric olist
    void AddMemberInTunnelOlist(uint32_t label, const Ip4Address &dip,
                                TunnelType::TypeBmap bmap) {
        tunnel_olist_.push_back(OlistTunnelEntry(label, dip, bmap));
    };

    //Labels for server + server list + ingress source label
    bool ModifyFabricMembers(const TunnelOlist &fabric_olist);
    void FlushAllFabricOlist();

    /* Ctrl node went down, flush source label and tunnel sent by it */
    void HandlePeerDown();
    //Gets
    const std::string &GetVrfName() { return vrf_name_; };
    const Ip4Address &GetGroupAddress() { return grp_address_; };
    const Ip4Address &GetSourceAddress() { return src_address_; };
    const std::list<uuid> &GetLocalOlist() { return local_olist_; };
    const std::string &GetVnName() { return vn_name_; };
    bool IsDeleted() { return deleted_; };
    void Deleted(bool val) { deleted_ = val; };

private:
    std::string vrf_name_;
    Ip4Address grp_address_;
    std::string vn_name_;
    Ip4Address src_address_;
    uint32_t src_mpls_label_;
    std::list<uuid> local_olist_; /* UUID of local i/f */
    TunnelOlist tunnel_olist_;
    bool deleted_;
    friend class MulticastHandler;
    DISALLOW_COPY_AND_ASSIGN(MulticastGroupObject);
};

/* Static class for handling multicast objects common functionalities */
class MulticastHandler {
public:
    /* Called by XMPP to add ctrl node sent olist and label */
    static void ModifyFabricMembers(const std::string &vrf_name, 
                                    const Ip4Address &group,
                                    const Ip4Address &source, 
                                    uint32_t source_label,
                                    const TunnelOlist &olist);
    /* Ctrl node went down, flush all source label and tunnel sent by it */
    static void HandlePeerDown();
    //Registered for VN notification
    static void ModifyVNIpam(DBTablePartBase *partition, DBEntryBase *e);
    //Registered for VM notification
    static void ModifyVmInterface(DBTablePartBase *partition, DBEntryBase *e); 
    //Register VM and VN notification
    static void Register();

    //Singleton object reference
    static MulticastHandler *GetInstance() { 
        if (obj_ == NULL) {
            obj_ = new MulticastHandler();
        }
        return obj_; 
    };
    //For test routines to clear all routes and mpls label
    static void Shutdown();
    //Multicast obj list addition deletion
    MulticastGroupObject *FindGroupObject(const std::string &vrf_name,
                                          const Ip4Address &dip);
private:
    //operations on list of all objectas per group/source/vrf
    void AddToMulticastObjList(MulticastGroupObject *obj) {
        multicast_obj_list_.insert(obj);
    };
    void DeleteMulticastObject(const std::string &vrf_name,
                               const Ip4Address &grp_addr);
    std::set<MulticastGroupObject *> &GetMulticastObjList() {
        return this->multicast_obj_list_;
    };

    //Notification to propagate subnh in compnh list change
    void TriggerCompositeNHChange(MulticastGroupObject *);
    //Delete teh route and mpls label for the object
    void DeleteRouteandMPLS(MulticastGroupObject *);

    //VM intf add-delete
    void DeleteVmInterface(const Interface *intf);
    void AddVmInterface(const std::string &vrf_name, const Ip4Address &addr, 
                        const uuid &itf_uuid, const string &vn_name);

    //Unresolved VM list, waiting on ipam for subnet broadcast
    void VisitUnresolvedVMList(const VnEntry *vn);
    std::list<const VmPortInterface *> &GetUnresolvedSubnetVMList(const uuid &vn_uuid) {
        return this->unresolved_subnet_vm_list_[vn_uuid];
    };
    void AddToUnresolvedSubnetVMList(const uuid &vn_uuid,
                                     const VmPortInterface *vm_itf)
    {
        vm_vn_mapping_[vm_itf->GetUuid()] = vn_uuid;
        unresolved_subnet_vm_list_[vn_uuid].push_back(vm_itf);
    };
    void DeleteVNFromUnResolvedList(const uuid &vn_uuid) {
        std::map<uuid, std::list<const VmPortInterface *> >::iterator it = 
            unresolved_subnet_vm_list_.find(vn_uuid);
        if (it != unresolved_subnet_vm_list_.end()) {
            unresolved_subnet_vm_list_.erase(it);
        }
    };
    void DeleteVMFromUnResolvedList(const VmPortInterface *vm_itf) {
        uuid vn_uuid = vm_vn_mapping_[vm_itf->GetUuid()];
        unresolved_subnet_vm_list_[vn_uuid].remove(vm_itf);
        if (unresolved_subnet_vm_list_.size() == 0) {
            DeleteVNFromUnResolvedList(vn_uuid);
        }
        vm_vn_mapping_.erase(vm_itf->GetUuid());
    };

    //IPAM handlers for subnet broadcast
    void HandleIPAMChange(const VnEntry *vn, 
                          const std::vector<VnIpam> &ipam);
    void DeleteVnIPAM(const VnEntry *vn);
    std::vector<VnIpam> &GetIpamMapList(const uuid &vn_uuid) {
        return this->vn_ipam_mapping_[vn_uuid];
    }; 
    std::map<uuid, std::vector<VnIpam> > &GetIpamMap() 
    { return vn_ipam_mapping_; };

    //broadcast rt add /delete
    void AddBroadcastRoute(const std::string &vrf_name, const Ip4Address &addr);
    void DeleteBroadcastRoute(const std::string &vrf_name, 
                              const Ip4Address &addr);

    //Subnet rt add /delete
    void AddSubnetRoute(const std::string &vrf_name, const Ip4Address &addr,
                        const std::string &vn_name);
    void DeleteSubnetRoute(const std::string &vrf_name, 
                           const Ip4Address &addr);

    //VRF VN mapping
    const string &GetAssociatedVrfForVn(const uuid &vn_uuid) {
        return vn_vrf_mapping_[vn_uuid];
    };
    void SetVrfNameForVn(const uuid &vn_uuid, const std::string &vrf_name) {
        vn_vrf_mapping_[vn_uuid] = vrf_name;
    };
    void RemoveVrfVnAssociation(const uuid &vn_uuid) {
        std::map<uuid, string>::iterator it = vn_vrf_mapping_.find(vn_uuid);
        if (it != vn_vrf_mapping_.end()) {
            vn_vrf_mapping_.erase(it);
        }
    }

    //VM itf to multicast ob
    void AddVmToMulticastObjMap(const uuid &vm_itf_uuid, 
                                MulticastGroupObject *obj) {
        this->vm_to_mcobj_list_[vm_itf_uuid].push_back(obj);
    };
    void DeleteVmToMulticastObjMap(const uuid &vm_itf_uuid) { 
        if (this->vm_to_mcobj_list_[vm_itf_uuid].size() == 0) {
            std::map<uuid, std::list<MulticastGroupObject *> >::iterator it =
                this->vm_to_mcobj_list_.find(vm_itf_uuid);
            if (it != this->vm_to_mcobj_list_.end()) {
                this->vm_to_mcobj_list_.erase(it);
            }
        }
    };

    std::list<MulticastGroupObject *> &GetVmToMulticastObjMap(const uuid &uuid)
    {
        return this->vm_to_mcobj_list_[uuid];
    };

    MulticastHandler() { };
    virtual ~MulticastHandler() { };

    static MulticastHandler *obj_;
    //TODO modify to use Backreference tool 
    //VN uuid as key and list of VM interface pointers as data 
    std::map<uuid, std::list<const VmPortInterface *> > unresolved_subnet_vm_list_;
    //Reference mapping of VM to participating multicast object list
    std::map<uuid, std::list<MulticastGroupObject *> > vm_to_mcobj_list_;
    //List of all multicast objects(VRF/G/S) 
    std::set<MulticastGroupObject *> multicast_obj_list_;
    //VN uuid as key and IPAM as data
    std::map<uuid, std::vector<VnIpam> > vn_ipam_mapping_; 
    //VN uuid to VRF name mapping
    std::map<uuid, string> vn_vrf_mapping_;
    //VM uuid <-> VN uuid
    std::map<uuid, uuid> vm_vn_mapping_;
};

#endif /* multicast_agent_oper_hpp */
