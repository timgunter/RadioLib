#pragma once

#include <netinet/in.h>

#include <array>
#include <memory>
#include <string>

#include "protocols/PhysicalLayer/PhysicalLayer.h"

// Cuz 0 is a valid socket(ugh)
class SockHandle
{
public:
    static int const nullsock = -1;

    SockHandle(int            const sock     ) : m_sock(sock) {}
    SockHandle(std::nullptr_t const = nullptr) : m_sock(nullsock  ) {}

    explicit operator bool() const { return m_sock != nullsock; }
    operator int       &(  )       { return m_sock; }
    operator int const &(  ) const { return m_sock; }

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
    void operator()(pointer p) { close(p); }
}; // class SockDeleter

using SockPtr = std::unique_ptr<int, SockDeleter>;

class IP
{
public:
    using ip = std::array<uint8_t, 4>;

    IP(ip const &ip_);
    operator std::string() const;

    operator in_addr_t() const;
    in_addr_t operator()(void) const;

private:
    ip const m_ip;
}; // class IP

/// Provides one definiton of defaults for multicast address and interface
struct IPMulti : public IP { IPMulti(ip const &ip_ = {255, 0, 20, 20}); };
struct IPIFace : public IP { IPIFace(ip const &ip_ = {127, 0,  0,  1}); };

/// Provides one definition of default port
struct Port
{
    Port(uint16_t const port = 2020);
    uint16_t operator()(void) const;
    uint16_t const m_port;
};

struct TxGroup
{
public:
    TxGroup(Port const &port = Port(), IPMulti const &multicast = IPMulti());

    void init(Port const &port = Port(), IPMulti const &multicast = IPMulti());

    operator sockaddr const*(       ) const;
    sockaddr  const *operator()(void) const;
    size_t           size(      void) const;

private:
    sockaddr_in m_txGroup;
}; // struct TxGroup

class UDPLoRaSim : public PhysicalLayer
{
public:
    /// So that base class methods that aren't overridden here are accessible
    using PhysicalLayer::transmit;
    using PhysicalLayer::receive;
    using PhysicalLayer::startTransmit;
    using PhysicalLayer::readData;

    UDPLoRaSim(
        Port      const &port      = Port()
        , IPMulti const &multicast = IPMulti()
        , IPIFace const &interface = IPIFace()
        , size_t  const  maxPacket = (1<<16)-1
        , float   const  freqStep  = 0.f
    );

    virtual ~UDPLoRaSim(void);

    bool isOpen(void) const;

    static bool openRx(SockPtr &sock, Port const &port, IPMulti const &multicast = IPMulti(), IPIFace const &interface = IPIFace());
    static bool openTx(SockPtr &sock, Port const &port,                                       IPIFace const &interface = IPIFace());

    bool open(void);
    
    void close(void);

    int16_t transmit(uint8_t* data, size_t len, uint8_t addr = 0) override;

    int16_t receive(uint8_t* data, size_t len) override;

    int16_t startTransmit(uint8_t* data, size_t len, uint8_t addr = 0) override;

    int16_t readData(uint8_t* data, size_t len) override;

    size_t getPacketLength(bool update = true) override;

    /// I don't think I need to implement anything for these?
    int16_t standby(                               ) override;
    int16_t transmitDirect(       uint32_t FRF = 0 ) override;
    int16_t receiveDirect(                         ) override;
    int16_t setFrequencyDeviation(float    freqDev ) override;
    int16_t setDataShaping(       float    sh      ) override;
    int16_t setEncoding(          uint8_t  encoding) override;

private:
    Port     m_port;
    IPMulti  m_multicast;
    IPIFace  m_interface;

    SockPtr  m_rxSocket;
    SockPtr  m_txSocket;

    TxGroup  m_txGroup;
    ssize_t  m_rxCurr  = -1;
    ssize_t  m_rxPrev  = -1;
}; // class UDPLoRaSim
