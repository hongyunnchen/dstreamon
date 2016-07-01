#include "Packet.hpp"
#include "NetTypes.hpp"
#include <netinet/in.h>
#include <arpa/inet.h>

// loads 2B in Big endian into uint_16t
#define load_BE_u16(A) (((*(A)) << 8) + *((A) + 1))

// loads 4B in Big endian into uint_16t
#define load_BE_u32(B) (((load_BE_u16(B)) << 16) + (load_BE_u16(B + 2)))

namespace bm {



/* Decode IP header HLV field into version and header length */
static size_t decode_hlv(uint8_t hlv, uint8_t& version) {
    version = (hlv & 0xf0) >> 4;
    return (hlv & 0x0f) << 2;
}

/* Decode TCP header payload offset field */
static size_t decode_payoff(uint8_t payoff) {
    return (payoff & 0xf0) >> 2;
}

/* Parses MAC header, populates m_pkttype, m_ipbuffer if IP */
void Packet::parse_mac() const {
    // shortcut valid ethertype
    if (m_machdr_parsed) return;
    m_machdr_parsed = true;

    const uint8_t *macbuf = m_buffer;

    if (m_mactype == kEthernet) {
        // Shortcut further parsing on runt packet
        if (m_caplen < sizeof(ethhdr)) {
            m_pkttype = kPktTypeRuntL2;
            m_iphdr_parsed = true;
            m_ports_parsed = true;
            m_l4hdr_parsed = true;
            return;
        }

        // get packet type 
        const ethhdr *eh = reinterpret_cast<const ethhdr*>(m_buffer);
        m_pkttype = ntohs(eh->ethertype);
        macbuf += sizeof(ethhdr);

        while (m_pkttype == kPktType1q) {
            if ((m_caplen - (macbuf - m_buffer)) < sizeof(ethhdr)) {
                m_pkttype = kPktTypeRuntL2;
                m_iphdr_parsed = true;
                m_ports_parsed = true;
                m_l4hdr_parsed = true;
                return;
            }

            // 802.1q header; parse and try again
            const eth1qhdr *e1qh = reinterpret_cast<const eth1qhdr*>(macbuf);
            m_pkttype = ntohs(e1qh->ethertype);
            macbuf += sizeof(eth1qhdr);
        }        
    } else if (m_mactype == kRawIP) {
        m_pkttype = kPktTypeIP4;
    }

    if (m_pkttype == kPktTypeIP4) {
        // Store pointer to IP header
        m_iphdr_ptr = macbuf;
    } else {
        // Shortcut further parsing if not IP
        m_iphdr_parsed = true;
        m_ports_parsed = true;
        m_l4hdr_parsed = true;
    }
}

/* parses IP header, populates m_ip*, m_sip, m_dip, m_key.proto */
void Packet::parse_iphdr() const {
    // shortcut valid iphdr
    if (m_iphdr_parsed) return;
    m_iphdr_parsed = true;

    // make sure we've parsed the mac header
    parse_mac();

    // verify we have enough valid IP header
    if (m_iphdr_ptr) {
        size_t iphoff = m_iphdr_ptr - m_buffer;
        if ((m_caplen - iphoff) >= sizeof(ip4hdr)) {
            // Yep. Parse header.
            const ip4hdr *iph = reinterpret_cast<const ip4hdr*>(m_iphdr_ptr);
            m_key.src_ip4 = ntohl(iph->sip4);
            m_key.dst_ip4 = ntohl(iph->dip4);
            m_key.proto = iph->proto;
            m_iptos = iph->tos;
            m_ipttl = iph->ttl;
            
            // get header length
            uint8_t version;
            size_t iphlen = decode_hlv(iph->hlv, version);

            // find layer 4 header
            if ((m_caplen - iphoff) >= iphlen) {
                // we may even have a valid layer 4 header, store it
                m_l4hdr_ptr = m_iphdr_ptr + iphlen;
            }
        } else {
            // Shortcut further parsing on runt IP
            m_pkttype = kPktTypeRuntL3;
            m_ports_parsed = true;
            m_l4hdr_parsed = true;
        }            
    }
}

/* if protocol is UDP or TCP, populates m_key.src_port and m_key.dst_port; 
   also finds payload for UDP */
void Packet::parse_ports() const {
    // shortcut valid ports
    if (m_ports_parsed) return;
    m_ports_parsed = true;
    
    // make sure we've parsed the IP header
    parse_iphdr();

    // verify we have valid ports
    if (m_l4hdr_ptr && ((m_key.proto == kTCP) || (m_key.proto == kUDP))) {
        size_t l4hoff = m_l4hdr_ptr - m_buffer;
        if ((m_caplen - l4hoff) >= sizeof(porthdr)) {
            const porthdr *ph = reinterpret_cast<const porthdr*>(m_l4hdr_ptr);
            m_key.src_port = ntohs(ph->sp);
            m_key.dst_port = ntohs(ph->dp);
            
            //  go ahead and skip the rest of the UDP header
            if ((m_key.proto == kUDP) && ((m_caplen - l4hoff) >= sizeof(udphdr))) {
                m_l4payload_ptr = m_l4hdr_ptr + sizeof(udphdr);
            }
        } else {
            // Shortcut further parsing on runt ports
            m_pkttype = kPktTypeRuntL4;
            m_l4hdr_parsed = true;
        }
    }
}

   /* if protocol is TCP, m_tcp* */

void Packet::parse_tcphdr() const {
    // shortcut valid ports
    if (m_l4hdr_parsed) return;
    m_l4hdr_parsed = true;
    
    // insure IP parsed
    parse_iphdr();

    // look for a valid TCP header
    if (m_l4hdr_ptr && (m_key.proto == kTCP)) {
        size_t tcphoff = m_l4hdr_ptr - m_buffer;
        if ((m_caplen - tcphoff) >= sizeof(tcphdr)) {
            // Yep. Parse header.
            const tcphdr *tcph = reinterpret_cast<const tcphdr*>(m_l4hdr_ptr);
            m_key.src_port = ntohs(tcph->sp);
            m_key.dst_port = ntohs(tcph->dp);
            m_tcpflags = tcph->flags;
            m_tcpseq = ntohl(tcph->seqnum);
            m_tcpack = ntohl(tcph->acknum);
            m_tcpurg = ntohs(tcph->urgent);
            m_tcpwin = ntohs(tcph->window);
            
            // get header length
            size_t tcphlen = decode_payoff(tcph->payoff);

            // FIXME parse options

            // find payload header
            if ((m_caplen - tcphoff) >= tcphlen) {
                // we may even have a valid layer 4 header, store it
                m_l4payload_ptr = m_l4hdr_ptr + tcphlen;
            }
        } else {
            // Mark runt packet
            m_pkttype = kPktTypeRuntL4;
        }
    }
}

// Andrea's old parse_buffer code goes here.

// bool parse_buffer(const bm::const_buffer<char> & buf, FlowKey& tup)
// {
//     int len_left = buf.len() - sizeof(ethhdr);
//     if (len_left < 0) return false;
// 
//     const ethhdr* eth = (const ethhdr*)buf.addr();
//     const ip4hdr* ip;
//     if(eth->h_proto == ETH_P_IP) {
//         ip=(const iphdr*)(eth+1);
//     } else if (eth->h_proto == ETH_P_8021Q) {
//         len_left -= 4;
//         if (len_left < 0) return false;
//         ip = (const iphdr*)((const char*)(eth+1)+4);//skip vlan tag
//     } else {
//         return false;
//     }
//     
//     len_left -= sizeof(iphdr);
//     if(len_left < 0) return false;
// 
//     tup.d_ip = ntohl(ip->daddr);
//     tup.s_ip = ntohl(ip->saddr);
//     tup.proto = htons(ip->protocol);
//     if (tup.proto == 6) {
//         len_left -= sizeof(tcphdr);
//         if(len_left < 0) return false;
//         const tcphdr* tcp = (const tcphdr*) (ip+1);
//         tup.s_port = ntohs(tcp->source);
//         tup.d_port = ntohs(tcp->dest);
//     } else if (tup.proto == 17) {
//         len_left -= sizeof(udphdr);
//         if (len_left < 0) return false;
//         const udphdr* udp=(const udphdr*) (ip+1);
//         tup.s_port = ntohs(udp->source);
//         tup.d_port = ntohs(udp->dest);
//     } else {
//         // FIXME handle ICMP?
//         tup.s_port = 0;
//         tup.d_port = 0;
//     }
//     
//     return true;
// }
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

}
