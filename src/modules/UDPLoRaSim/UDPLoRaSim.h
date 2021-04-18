#pragma once

#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <array>
#include <sstream>

#include "../../TypeDef.h"

// Cuz 0 is a valid socket(ugh)
class SockHandle
{
public:
    static int const nullsock = -1;

    SockHandle(int            const sock     ) : m_sock(sock) {}
    SockHandle(std::nullptr_t const = nullptr) : m_sock(nullsock  ) {}

    operator bool() const { return m_sock != nullsock; }
    operator int&()       { return m_sock; }
    operator int () const { return m_sock; }

    int       *operator&()       { return &m_sock; }
    int const *operator&() const { return &m_sock; }

    friend bool operator==(SockHandle const &left, SockHandle const &right) { return   left.m_sock == right.m_sock;  }
    friend bool operator==(SockHandle const &left, std::nullptr_t const   ) { return   left.m_sock == nullsock;      }
    friend bool operator==(std::nullptr_t const,   SockHandle const &right) { return   nullsock    == right.m_sock;  }

    friend bool operator!=(SockHandle const &left, SockHandle const &right) { return !(left        == right       ); }
    friend bool operator!=(SockHandle const &left, std::nullptr_t const   ) { return !(left        == nullptr     ); }
    friend bool operator!=(std::nullptr_t const,   SockHandle const &right) { return !(nullptr     == right       ); }

private:
    int m_sock = nullsock;
}; // class SockHandle

struct SockDeleter
{
    using pointer = SockHandle;
    void operator()(pointer &p) { close(p); p = pointer(); }
}; // class SockDeleter

//using SockPtr = std::unique_ptr<int, SockDeleter>;
using SockPtr = std::shared_ptr<int, SockDeleter>; // Shared so we can use same socket for rx/tx

class IP
{
public:
    using ip = std::array<uint8_t, 4>;

    IP(ip const &ip_) : m_ip(ip_) {}

    operator std::string() const
    {
        std::ostringstream sstr;
        for(auto const &i : ip)
            sstr << (sstr.str().empty() ? "" : ".") << i;
        return sstr.str();
    }

    operator in_addr_t() const { return inet_addr(static_cast<std::string>(*this).c_str()); }

    in_addr_t operator()(void) const { return static_cast<in_addr_t>(*this); }

private:
    ip const m_ip;
}; // class IP

/// Provides one definiton of defaults for multicast address and interface
struct IPMulti : public IP { IPMulti(ip const &ip_ = {255, 0, 20, 20}) : IP(ip_) {} };
struct IPIFace : public IP { IPIFace(ip const &ip_ = {127, 0,  0,  1}) : IP(ip_) {} };

/// Provides one definition of default port
struct Port
{
    Port(uint16_t const port = 2020) : m_port(htons(port)) {}
    uint16_t operator() const { return m_port; }
    uint16_t const m_port;
}

class UDPLoRaSim : public PhysicalLayer
{
public:
    /// So that base class methods that aren't overridden here are accessible
    using PhysicalLayer::transmit;
    using PhysicalLayer::receive;
    using PhysicalLayer::startTransmit;
    using PhysicalLayer::readData;

    UDPLoRaSim(
        Port     const &port      = Port(),
        IPMulti  const &multicast = IPMulti(),
        IPIFace  const &interface = IPIFace(),
        ipsize_t const  maxPacket = (1<<16)-1,
        float    const  freqStep  = 0.f
    )
    : PhysicalLayer(freqStep, maxPacket)
    , m_port(       port               )
    , m_multicast(  multicast          )
    , m_interface(  interface          )
    {}

    virtual ~UDPLoRaSim(void) {}

    bool isOpen(void) const { return m_rxSocket && m_txSocket; }

    static SockPtr openRx(SockPtr sock, Port const &port, IPMulti const &multicast = IPMulti(), IPIFace const &interface = IPIFace())
    {
        if(!sock) sock.reset(socket(AF_INET_SOCK_DGRAM, 0));

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

    static SockPtr openTx(SockPtr sock, Port const &port, IPMulti const &multicast = IPMulti(), IPIFace const &interface = IPIFace())
    {
        if(!sock) sock.reset(socket(AF_INET_SOCK_DGRAM, 0));

        { // Scope for in_addr
            in_addr iface = {};
            iface.s_addr  = interface();
            if(0 > setsockopt(sock.get(), IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<char *>(&iface), sizeof(iface)))
                return SockPtr();
        }

        //{ // Scope for sockaddr_in
        //    sockaddr_in groupSock     = {};
        //    groupSock.sin_family      = AF_INET;
        //    groupSock.sin_port        = port();
        //    groupSock.sin_addr.s_addr = multicast();
        //}

        return sock;
    } // openTx()

    bool open(void)
    {
        if(isOpen()) return true;

        m_rxSocket = openRx(m_rxSocket, m_port, m_multicast, m_interface);
        m_txSocket = openTx(m_rxSocket, m_port, m_multicast, m_interface); // Use same socket for both
        //m_txSocket = openTx(m_txSocket, m_port, m_multicast, m_interface); // Not sure if this will work, may need separate sockets

        if(!m_txSocket || !m_rxSocket) { close(); return false; }

        return true;
    } // open()
    
    void close(void)
    {
        m_rxSocket.reset();
        m_txSocket.reset();
    } // close()

    // blocking
    int16_t transmit(uint8_t* data, size_t len, uint8_t addr = 0) override
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
    int16_t receive(uint8_t* data, size_t len) override
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
    int16_t startTransmit(uint8_t* data, size_t len, uint8_t addr = 0) override
    {
        open(); // If already open, will return immediately

        { // Scope for sockaddr_in
            sockaddr_in groupSock     = {};
            groupSock.sin_family      = AF_INET;
            groupSock.sin_port        = m_port();
            groupSock.sin_addr.s_addr = m_multicast();

            if(0 > sendto(m_txSocket.get(), data, len, 0, static_cast<sockaddr *>(&group), sizeof(group)))
                return ERR_UNKNOWN; // TODO: better error reporting
        }

        return ERR_NONE;
    }

    int16_t readData(uint8_t* data, size_t len) override
    {
        m_rxCurr = read(m_rxSocket.get(), data, len);
        if(0 > m_rxCurr); return ERR_UNKNOWN; // TODO: better error reporting

        return ERR_NONE;
    }

    size_t getPacketLength(bool update = true) override
    {
        if(update) { m_rxPrev = m_rxCurr; }

        if(0 > m_rxCurr) return 0; // TODO: Is this correct?

        return static_cast<size_t>(m_rxCurr);
    }

    /// I don't think I need to implement anything for these?
    int16_t standby(                               ) override { return ERR_NONE; }
    int16_t transmitDirect(       uint32_t FRF = 0 ) override { return ERR_NONE; }
    int16_t receiveDirect(                         ) override { return ERR_NONE; }
    int16_t setFrequencyDeviation(float    freqDev ) override { return ERR_NONE; }
    int16_t setDataShaping(       float    sh      ) override { return ERR_NONE; }
    int16_t setEncoding(          uint8_t  encoding) override { return ERR_NONE; }

private:
    Port     m_port;
    IPMulti  m_multicast;
    IPIFace  m_interface;

    SockPtr  m_rxSocket;
    SockPtr  m_txSocket;

    ssize_t  m_rxCurr = -1;
    ssize_t  m_rxPrev = -1;
}; // class UDPLoRaSim
