#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    if (trans.find(next_hop_ip) != trans.end()){
        EthernetFrame _etherframe;
        _etherframe.payload() = dgram.serialize();
        _etherframe.header().dst = trans[next_hop_ip];
        _etherframe.header().src = _ethernet_address;
        _etherframe.header().type = EthernetHeader::TYPE_IPv4;
        _frames_out.push (_etherframe);
        return ;
    }
    else if (_time_of_arp.find (next_hop_ip) == _time_of_arp.end() || 
            _past_time - _time_of_arp[next_hop_ip] > 5000){
        //! set the asking arp message
        ARPMessage _arpmessage;
        _arpmessage.sender_ethernet_address = _ethernet_address;
        _arpmessage.sender_ip_address = _ip_address.ipv4_numeric();
        _arpmessage.target_ip_address = next_hop.ipv4_numeric();
        _arpmessage.opcode = ARPMessage::OPCODE_REQUEST;

        //! set the asking arp type ehterframe
        EthernetFrame _etherframe_arp;
        _etherframe_arp.header().src = _ethernet_address;
        _etherframe_arp.header().dst = ETHERNET_BROADCAST;
        _etherframe_arp.header().type = EthernetHeader::TYPE_ARP;
        _etherframe_arp.payload() = std::move(_arpmessage.serialize());
        
        _frames_out.push (_etherframe_arp);
        _waiting_array.emplace (dgram, next_hop);

        _time_of_arp[next_hop_ip] = _past_time;
    }
    else 
        _waiting_array.emplace (dgram, next_hop);
    // DUMMY_CODE(dgram, next_hop, next_hop_ip);
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // DUMMY_CODE(frame);
    ARPMessage arp_request, _arp_reply;
    if (frame.header().type == EthernetHeader::TYPE_ARP){
        if (frame.header().dst == ETHERNET_BROADCAST &&
            arp_request.parse(frame.payload()) == ParseResult::NoError && 
            arp_request.target_ip_address == _ip_address.ipv4_numeric()){
            _arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            _arp_reply.sender_ethernet_address = _ethernet_address;
            _arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            _arp_reply.target_ethernet_address = frame.header().src;
            _arp_reply.target_ip_address = arp_request.sender_ip_address;
            
            trans[arp_request.sender_ip_address] = arp_request.sender_ethernet_address;
            _time_of_trans[arp_request.sender_ip_address] = _past_time;

            EthernetFrame _etherframe_arp;
            _etherframe_arp.header().src = _ethernet_address;
            _etherframe_arp.header().dst = arp_request.sender_ethernet_address;
            _etherframe_arp.header().type = EthernetHeader::TYPE_ARP;
            _etherframe_arp.payload() = std::move(_arp_reply.serialize());

            _frames_out.push (_etherframe_arp);
        }
        else if (frame.header().dst == _ethernet_address &&
                _arp_reply.parse (frame.payload()) == ParseResult::NoError){
                    if (_arp_reply.opcode == ARPMessage::OPCODE_REPLY && 
                        _arp_reply.target_ip_address == _ip_address.ipv4_numeric()){
                        trans[_arp_reply.sender_ip_address] = _arp_reply.sender_ethernet_address;
                        _time_of_trans[_arp_reply.sender_ip_address] = _past_time;
                        //---------------------------------------------
                        std::queue <std::pair<InternetDatagram, Address> > _tempo{};
                        while (!_waiting_array.empty()){
                            auto to_send = _waiting_array.front();
                            if (trans.find (to_send.second.ipv4_numeric()) != trans.end())
                                send_datagram (to_send.first, to_send.second);
                            else
                                _tempo.push (to_send);
                            _waiting_array.pop ();
                        }
                        _waiting_array = _tempo;
                    }
                    else return nullopt;
                }
        else return nullopt;
    }
    else if (frame.header().type == EthernetHeader::TYPE_IPv4){
        IPv4Datagram _ipv4_data;
        if (frame.header().dst == _ethernet_address && 
            _ipv4_data.parse (frame.payload()) == ParseResult::NoError){
            return _ipv4_data;
        }
        else return nullopt;
    }
    return nullopt;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    _past_time += ms_since_last_tick;
    for (auto it = _time_of_trans.begin(); it != _time_of_trans.end(); ){
        auto _to_erase = *it;
        if (_past_time - _to_erase.second > 30000){
            trans.erase (trans.find (_to_erase.first));
            it = _time_of_trans.erase (it);
        }
        else ++ it;
    }
    // DUMMY_CODE(ms_since_last_tick); 
}
