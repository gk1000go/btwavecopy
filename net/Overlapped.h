#ifndef OVERLAPPED_H
#define OVERLAPPED_H

#include "SocketImpl.h"
#include "../base/BaseTypes.h"
#include <functional>
#include <string.h>
#include <WinSock2.h>

namespace bittorrent {
namespace net {

    enum OverlappedType
    {
        ACCEPT,
        CONNECT,
        RECEIVE,
        SEND
    };

    struct Overlapped
    {
        OVERLAPPED overlapped;
        OverlappedType type;
        int transfered_bytes;
        int error;

    protected:
        explicit Overlapped(OverlappedType t)
            : type(t)
        {
            memset(&overlapped, 0, sizeof(overlapped));
        }
    };

    // an Overlapped for iocp service AsyncAccept
    class AcceptOverlapped : public Overlapped
    {
    public:
        typedef std::tr1::function<void (bool, SocketImpl, sockaddr_in)> Handler;

        template<typename Service>
        AcceptOverlapped(Handler handler, Service& service)
            : Overlapped(ACCEPT),
              handler_(handler),
              accept_socket_(service)
        {
        }

        SOCKET GetAcceptSocket() const
        {
            return accept_socket_.Get();
        }

        char * GetAddressBuf() const
        {
            return address_buffer;
        }

        int GetAddressLength() const
        {
            return address_length;
        }

        void Invoke()
        {
            sockaddr *local;
            sockaddr *remote;
            int local_len;
            int remote_len;

            ::GetAcceptExSockaddrs(
                    address_buffer, 0, address_length, address_length,
                    &local, &local_len, &remote, &remote_len);

            bool success = (error == ERROR_SUCCESS);
            handler_(success, accept_socket_, *(sockaddr_in *)remote);

            if (!success)
                accept_socket_.Close();
        }

    private:
        static const int address_length = sizeof(sockaddr_in) + 16;
        static const int address_buffer_size = 2 * address_length;

        Handler handler_;
        SocketImpl accept_socket_;
        char address_buffer[address_buffer_size];
    };

    // an Overlapped for iocp service AsyncConnect
    class ConnectOverlapped : public Overlapped
    {
    public:
        typedef std::tr1::function<void (bool)> Handler;

        explicit ConnectOverlapped(Handler handler)
            : Overlapped(CONNECT),
              handler_(handler)
        {
        }

        void Invoke()
        {
            handler_(error == ERROR_SUCCESS);
        }

    private:
        Handler handler_;
    };

    // an Overlapped for iocp service AsyncReceive
    class ReceiveOverlapped : public Overlapped
    {
    public:
        typedef std::tr1::function<void (bool, int)> Handler;

        template<typename Buffer>
        ReceiveOverlapped(Handler handler, Buffer& buffer)
            : Overlapped(RECEIVE),
              handler_(handler),
              wsabuf_()
        {
            wsabuf_.buf = buffer.GetBuffer();
            wsabuf_.len = buffer.BufferLen();
        }

        LPWSABUF GetWsaBuf()
        {
            return &wsabuf_;
        }

        DWORD GetWsaBufCount() const
        {
            return 1;
        }

        void Invoke()
        {
            bool success = (error == ERROR_SUCCESS) && (transfered_bytes != 0);
            handler_(success, transfered_bytes);
        }

    private:
        Handler handler_;
        WSABUF wsabuf_;
    };

    // an Overlapped for iocp service AsyncSend
    class SendOverlapped : public Overlapped
    {
    public:
        typedef std::tr1::function<void (bool, int)> Handler;

        template<typename Buffer>
        SendOverlapped(Handler handler, const Buffer& buffer)
            : Overlapped(SEND),
              handler_(handler),
              wsabuf_()
        {
            wsabuf_.buf = buffer.GetBuffer();
            wsabuf_.len = buffer.BufferLen();
        }

        LPWSABUF GetWsaBuf()
        {
            return &wsabuf_;
        }

        DWORD GetWsaBufCount() const
        {
            return 1;
        }

        void Invoke()
        {
            bool success = (error == ERROR_SUCCESS) && (transfered_bytes != 0);
            handler_(success, transfered_bytes);
        }

    private:
        Handler handler_;
        WSABUF wsabuf_;
    };

    // an exception safe Overlappeds helper template class
    template<typename Type>
    class OverlappedPtr : private NotCopyable
    {
    public:
        explicit OverlappedPtr(Type *overlapped)
            : overlapped_(overlapped)
        {
        }

        ~OverlappedPtr()
        {
            if (!overlapped_)
                return ;
            delete overlapped_;
        }

        OVERLAPPED * Get() const
        {
            return (OVERLAPPED *)overlapped_;
        }

        void Release()
        {
            overlapped_ = 0;
        }

        Type * operator -> () const
        {
            return overlapped_;
        }

        Type& operator * () const
        {
            return *overlapped_;
        }

    private:
        Type *overlapped_;
    };

    class OverlappedOps
    {
    public:
        static void ApplyOverlappedResult(Overlapped *overlapped, int bytes, int error)
        {
            overlapped->transfered_bytes = bytes;
            overlapped->error = error;
        }

        static void InvokeOverlapped(Overlapped *overlapped)
        {
            switch (overlapped->type)
            {
            case ACCEPT:
                reinterpret_cast<AcceptOverlapped *>(overlapped)->Invoke();
                break;
            case CONNECT:
                reinterpret_cast<ConnectOverlapped *>(overlapped)->Invoke();
                break;
            case RECEIVE:
                reinterpret_cast<ReceiveOverlapped *>(overlapped)->Invoke();
                break;
            case SEND:
                reinterpret_cast<SendOverlapped *>(overlapped)->Invoke();
                break;
            }
        }

        static void DeleteOverlapped(Overlapped *overlapped)
        {
            switch (overlapped->type)
            {
            case ACCEPT:
                delete reinterpret_cast<AcceptOverlapped *>(overlapped);
                break;
            case CONNECT:
                delete reinterpret_cast<ConnectOverlapped *>(overlapped);
                break;
            case RECEIVE:
                delete reinterpret_cast<ReceiveOverlapped *>(overlapped);
                break;
            case SEND:
                delete reinterpret_cast<SendOverlapped *>(overlapped);
                break;
            }
        }
    };

} // namespace net
} // namespace bittorrent

#endif // OVERLAPPED_H
