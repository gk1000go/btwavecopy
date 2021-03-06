#ifndef IOCP_H
#define IOCP_H

#include "Address.h"
#include "NetException.h"
#include "Overlapped.h"
#include "ServiceBase.h"
#include "../base/BaseTypes.h"
#include "../thread/Thread.h"
#include "../thread/Mutex.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include <WinSock2.h>
#include <MSWSock.h>

namespace bitwave {
namespace net {

    // a class supply iocp service for sockets, sockets could use the
    // AsyncAccept, AsyncConnect, AsyncReceive, AsyncSend methods to
    // communicate with other socket
    class IocpService : public BasicService<IocpService>
    {
    public:
        IocpService()
            : iocp_(),
              service_threads_(),
              completion_status_(),
              status_mutex_()
        {
            // init iocp
            iocp_.handle_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
            if (!iocp_.IsValid())
                throw NetException(CREATE_SERVICE_ERROR);

            InitServiceThreads();
        }

        ~IocpService()
        {
            Shutdown();
        }

        // register socket to iocp service
        void RegisterSocket(SOCKET socket)
        {
            if (!::CreateIoCompletionPort((HANDLE)socket, iocp_.handle_, 0, 0))
                throw NetException(REGISTER_SOCKET_ERROR);
        }

        template<typename SocketImplement, typename Handler>
        void AsyncAccept(const SocketImplement& impl, const Handler& handler)
        {
            unsigned long bytes = 0;
            LPFN_ACCEPTEX AcceptEx = 0;
            GUID guid = WSAID_ACCEPTEX;

            int error = ::WSAIoctl(impl.Get(), SIO_GET_EXTENSION_FUNCTION_POINTER,
                                   &guid, sizeof(guid), &AcceptEx, sizeof(AcceptEx),
                                   &bytes, 0, 0);
            if (error)
                throw NetException(GET_ACCEPTEX_FUNCTION_ERROR);

            OverlappedPtr<AcceptOverlapped> ptr(new AcceptOverlapped(handler, *this));

            const int address_length = ptr->GetAddressLength();
            error = AcceptEx(impl.Get(), ptr->GetAcceptSocket(), ptr->GetAddressBuf(),
                             0, address_length, address_length, 0, ptr.Get());

            if (!error && ::WSAGetLastError() != ERROR_IO_PENDING)
                throw NetException(CALL_ACCEPTEX_FUNCTION_ERROR);

            ptr.Release();
        }

        template<typename SocketImplement, typename Handler>
        void AsyncConnect(const SocketImplement& impl, const Address& address,
                          const Port& port, const Handler& handler)
        {
            unsigned long bytes = 0;
            LPFN_CONNECTEX ConnectEx = 0;
            GUID guid = WSAID_CONNECTEX;

            int error = ::WSAIoctl(impl.Get(), SIO_GET_EXTENSION_FUNCTION_POINTER,
                                   &guid, sizeof(guid), &ConnectEx, sizeof(ConnectEx),
                                   &bytes, 0, 0);
            if (error)
                throw NetException(GET_CONNECTEX_FUNCTION_ERROR);

            OverlappedPtr<ConnectOverlapped> ptr(new ConnectOverlapped(handler));

            sockaddr_in local = Ipv4Address(Address(), Port(unsigned short(0)));
            error = ::bind(impl.Get(), (sockaddr *)&local, sizeof(local));
            if (error == SOCKET_ERROR)
                throw NetException(CONNECT_BIND_LOCAL_ERROR);

            sockaddr_in end_point = Ipv4Address(address, port);
            error = ConnectEx(impl.Get(), (sockaddr *)&end_point, sizeof(end_point), 0, 0, 0, ptr.Get());
            if (!error && ::WSAGetLastError() != ERROR_IO_PENDING)
                throw NetException(CALL_CONNECTEX_FUNCTION_ERROR);

            ptr.Release();
        }

        template<typename SocketImplement, typename Buffer, typename Handler>
        void AsyncReceive(const SocketImplement& impl, Buffer& buffer, const Handler& handler)
        {
            DWORD flags = 0;
            OverlappedPtr<ReceiveOverlapped> ptr(new ReceiveOverlapped(handler, buffer));

            int error = ::WSARecv(impl.Get(), ptr->GetWsaBuf(), ptr->GetWsaBufCount(),
                                  0, &flags, (LPWSAOVERLAPPED)ptr.Get(), 0);
            if (error == SOCKET_ERROR && ::WSAGetLastError() != WSA_IO_PENDING)
                throw NetException(CALL_WSARECV_FUNCTION_ERROR);

            ptr.Release();
        }

        template<typename SocketImplement, typename Buffer, typename Handler>
        void AsyncSend(const SocketImplement& impl, const Buffer& buffer, const Handler& handler)
        {
            OverlappedPtr<SendOverlapped> ptr(new SendOverlapped(handler, buffer));

            int error = ::WSASend(impl.Get(), ptr->GetWsaBuf(), ptr->GetWsaBufCount(),
                                  0, 0, (LPWSAOVERLAPPED)ptr.Get(), 0);
            if (error == SOCKET_ERROR && ::WSAGetLastError() != WSA_IO_PENDING)
                throw NetException(CALL_WSASEND_FUNCTION_ERROR);

            ptr.Release();
        }

    private:
        typedef std::tr1::shared_ptr<Thread> ThreadPtr;
        typedef std::vector<ThreadPtr> ServiceThreads;
        typedef std::vector<OverlappedInvoker> CompletionStatus;

        // an iocp handle exception safe helper class
        struct Iocp
        {
            Iocp()
                : handle_(INVALID_HANDLE_VALUE)
            {
            }

            ~Iocp()
            {
                if (!IsValid())
                    return ;

                ::CloseHandle(handle_);
            }

            bool IsValid() const
            {
                return handle_ != INVALID_HANDLE_VALUE;
            }

            HANDLE handle_;
        };

        virtual void DoRun()
        {
            ProcessCompletionStatus();
        }

        // init iocp service threads, then these threads can process iocp
        // status by call GetQueuedCompletionStatus function
        void InitServiceThreads()
        {
            // calculate appropriate service thread number
            SYSTEM_INFO system_info;
            ::GetSystemInfo(&system_info);
            int num = system_info.dwNumberOfProcessors * 2;

            // create service threads to get all iocp operations result
            for (int i = 0; i < num; ++i)
            {
                ThreadPtr ptr(new Thread(std::tr1::bind(&IocpService::Service, this)));
                service_threads_.push_back(ptr);
            }
        }

        // Service thread function
        unsigned Service()
        {
            DWORD bytes = 0;
            ULONG completion_key = 0;
            LPOVERLAPPED overlapped = 0;

            while (true)
            {
                BOOL result = ::GetQueuedCompletionStatus(
                        iocp_.handle_, &bytes,
                        &completion_key, &overlapped, INFINITE);

                if (result)
                {
                    // exit the service thread
                    if (completion_key == -1)
                        break;

                    // a success status
                    AddNewCompletionStatus(
                            reinterpret_cast<Overlapped *>(overlapped),
                            bytes, ERROR_SUCCESS);
                }
                else if (overlapped)
                {
                    // an error occur
                    AddNewCompletionStatus(
                            reinterpret_cast<Overlapped *>(overlapped),
                            bytes, ::GetLastError());
                }
            }

            return 0;
        }

        void AddNewCompletionStatus(Overlapped *overlapped, DWORD bytes, int error)
        {
            // add into completion_status_, completion_status_ read write in many
            // service threads and the thread which call Run function. so we use
            // mutex and lock
            SpinlocksMutexLocker locker(status_mutex_);
            OverlappedInvoker invoker(overlapped, bytes, error);
            completion_status_.push_back(invoker);
        }

        void ProcessCompletionStatus()
        {
            CompletionStatus completion;
            // get all completion_status_, then we can process the completion_status_
            // in the thread which call this function
            {
                SpinlocksMutexLocker locker(status_mutex_);
                completion.swap(completion_status_);
            }

            std::for_each(completion.begin(), completion.end(),
                    std::mem_fun_ref(&OverlappedInvoker::Invoke));
        }

        void Shutdown()
        {
            std::size_t num = service_threads_.size();
            for (std::size_t i = 0; i < num; ++i)
            {
                // post completion status let all service_threads_ exit
                ::PostQueuedCompletionStatus(iocp_.handle_, 0, -1, 0);
            }

            std::vector<HANDLE> handles;
            for (ServiceThreads::iterator it = service_threads_.begin();
                    it != service_threads_.end(); ++it)
            {
                HANDLE handle = (*it)->GetHandle();
                handles.push_back(handle);
            }
            // wait all service threads exit
            ::WaitForMultipleObjects(handles.size(), &handles[0], true, INFINITE);
        }

        Iocp iocp_;
        ServiceThreads service_threads_;
        CompletionStatus completion_status_;
        SpinlocksMutex status_mutex_;
    };

} // namespace net
} // namespace bitwave

#endif // IOCP_H
