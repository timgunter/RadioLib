#pragma once

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifdef INADDR_NONE
#undef INADDR_NONE
#endif// INADDR_NONE

#include <array>
#include <memory>
#include <string>
#include <sstream>

//#include "../../TypeDef.h"

#include "UDPLoRaSim.h"

#ifdef str
#define OLD_STR str
#undef str
#endif // str

#ifdef xstr
#define OLD_XSTR xstr
#undef xstr
#endif // xstr

IP::IP(ip const &ip_) : m_ip(ip_) {}

IP::operator std::string() const
{
    std::ostringstream sstr;
    for(auto const &i : m_ip)
        sstr << (sstr.str().empty() ? "" : ".") << i;
    return sstr.str();
}

IP::operator in_addr_t() const { return inet_addr(static_cast<std::string>(*this).c_str()); }

in_addr_t IP::operator()(void) const { return static_cast<in_addr_t>(*this); }

IPMulti::IPMulti(ip const &ip_) : IP(ip_) {}
IPIFace::IPIFace(ip const &ip_) : IP(ip_) {}

Port::Port(uint16_t const port) : m_port(htons(port)) {}
uint16_t Port::operator()(void) const { return m_port; }

TxGroup::TxGroup(Port const &port, IPMulti const &multicast) { init(port, multicast); }

void TxGroup::init(Port const &port, IPMulti const &multicast)
{
    m_txGroup = {};
    m_txGroup.sin_family      = AF_INET;
    m_txGroup.sin_port        = port();
    m_txGroup.sin_addr.s_addr = multicast();
}

TxGroup::operator  sockaddr*(     ) const { return static_cast<sockaddr *>(&m_txGroup); }
sockaddr *TxGroup::operator()(void) const { return static_cast<sockaddr *>(*this     ); }
size_t    TxGroup::size(      void) const { return sizeof(m_txGroup); }

UDPLoRaSim::UDPLoRaSim(
    Port      const &port
    , IPMulti const &multicast
    , IPIFace const &interface
    , size_t  const  maxPacket
    , float   const  freqStep
)
: PhysicalLayer(freqStep, maxPacket)
, m_port(       port               )
, m_multicast(  multicast          )
, m_interface(  interface          )
{}

UDPLoRaSim::~UDPLoRaSim(void) {}

bool UDPLoRaSim::isOpen(void) const { return m_rxSocket && m_txSocket; }

SockPtr UDPLoRaSim::openRx(SockPtr sock, Port const &port, IPMulti const &multicast, IPIFace const &interface)
{
    if(!sock) sock.reset(socket(AF_INET_SOCK_DGRAM, 0), SockDeleter());

    if(!sock) return SockPtr();

    int reuse = 1;
    if(0 > setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&reuse), sizeof(reuse)))
        return SockPtr();

    { // Scope for sockaddr_in
        sockaddr_in saddr     = {};
        saddr.sin_family      = AF_INET;
        saddr.sin_port        = port();
        saddr.sin_addr.s_addr = INADDR_ANY;

        if(0 > bind(sock.get(), reinterpret_cast<sockaddr *>(&saddr), sizeof(saddr)))
            return SockPtr();
    }

    { // Scope for ip_mreq
        ip_mreq group = {};
        group.imr_multiaddr.s_addr = multicast();
        group.imr_interface.s_addr = interface();
        if(0 > setsockopt(sock.get(), IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char *>(&group), sizeof(group)))
            return SockPtr();
    }

    return sock;
} // openRx()

SockPtr UDPLoRaSim::openTx(SockPtr sock, Port const &port, IPIFace const &interface)
{
    if(!sock) sock.reset(socket(AF_INET_SOCK_DGRAM, 0), SockDeleter());

    in_addr iface = {};
    iface.s_addr  = interface();
    if(0 > setsockopt(sock.get(), IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<char *>(&iface), sizeof(iface)))
        return SockPtr();

    return sock;
} // openTx()

bool UDPLoRaSim::open(void)
{
    if(isOpen()) return true;

    m_rxSocket = openRx(m_rxSocket, m_port, m_multicast, m_interface);
    m_txSocket = openTx(m_rxSocket, m_port,              m_interface); // Use same socket for both
    //m_txSocket = openTx(m_txSocket, m_port,              m_interface); // Not sure if this will work, may need separate sockets

    m_txGroup.init(m_port, m_multicast);

    if(!m_txSocket || !m_rxSocket) { close(); return false; }

    return true;
} // open()
    
void UDPLoRaSim::close(void)
{
    m_rxSocket.reset();
    m_txSocket.reset();
    m_txGroup.init();
    m_rxCurr = -1;
    m_rxPrev = -1;
} // close()

// blocking
int16_t UDPLoRaSim::transmit(uint8_t* data, size_t len, uint8_t addr = 0) override
{
    return startTransmit(data, len, addr);
    //int16_t const state = startTransmit(data, len, addr);

    //while(true)
    //{
    //    yield();
    //}

    //return standby();
} // transmit

// blocking
int16_t UDPLoRaSim::receive(uint8_t* data, size_t len) override
{
    return readData(data, len);

    //int16_t const state = startReceive(len, mode);
    //uint8_t const mode  = 0;
    //int16_t const state = startReceive(len, mode);

    //// Wait for packet or timeout
    //while(true)
    //{
    //    yield();
    //}

    //return readData(data, len);
}

// non-blocking
int16_t UDPLoRaSim::startTransmit(uint8_t* data, size_t len, uint8_t addr = 0) override
{
    open(); // If already open, will return immediately

    if(0 > sendto(m_txSocket.get(), data, len, 0, m_txGroup(), m_txGroup.size()))
        return ERR_UNKNOWN; // TODO: better error reporting

    return ERR_NONE;
}

int16_t UDPLoRaSim::readData(uint8_t* data, size_t len) override
{
    m_rxCurr = read(m_rxSocket.get(), data, len);
    if(0 > m_rxCurr); return ERR_UNKNOWN; // TODO: better error reporting

    return ERR_NONE;
}

size_t UDPLoRaSim::getPacketLength(bool update = true) override
{
    if(update) { m_rxPrev = m_rxCurr; }

    if(0 > m_rxCurr) return 0; // TODO: Is this correct?

    return static_cast<size_t>(m_rxCurr);
}

/// I don't think I need to implement anything for these?
int16_t UDPLoRaSim::standby(                               ) override { return ERR_NONE; }
int16_t UDPLoRaSim::transmitDirect(       uint32_t FRF = 0 ) override { return ERR_NONE; }
int16_t UDPLoRaSim::receiveDirect(                         ) override { return ERR_NONE; }
int16_t UDPLoRaSim::setFrequencyDeviation(float    freqDev ) override { return ERR_NONE; }
int16_t UDPLoRaSim::setDataShaping(       float    sh      ) override { return ERR_NONE; }
int16_t UDPLoRaSim::setEncoding(          uint8_t  encoding) override { return ERR_NONE; }

//#ifdef OLD_STR
//#define str OLD_STR
//#endif // OLD_STR
//
//#ifdef OLD_XSTR
//#define str OLD_XSTR
//#endif // OLD_XSTR
//
//#if defined(OLD_STR) || defined(OLD_XSTR)
//#include "configuration.h"
//#endif // if OLD_STR or OLD_XSTR
