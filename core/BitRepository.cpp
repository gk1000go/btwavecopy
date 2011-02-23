#include "BitRepository.h"
#include "BitData.h"

namespace bittorrent {
namespace core {

    BitRepository::BitRepository()
        : listen_port_(0),
          bitdata_map_()
    {
    }

    BitRepository::~BitRepository()
    {
    }

    // static
    BitRepository& BitRepository::GetSingleton()
    {
        static BitRepository repository;
        return repository;
    }

    short BitRepository::GetListenPort() const
    {
        return listen_port_;
    }

    void BitRepository::SetListenPort(short port)
    {
        listen_port_ = port;
    }

    BitRepository::BitDataPtr BitRepository::CreateBitData(const std::string& torrent_file)
    {
        BitDataPtr ptr(new BitData(torrent_file));
        Sha1Value key = ptr->GetInfoHash();
        std::pair<BitDataMap::iterator, bool> result =
            bitdata_map_.insert(std::make_pair(key, ptr));
        return result.first->second;
    }

    void BitRepository::GetAllBitData(std::vector<BitDataPtr>& data) const
    {
        data.reserve(bitdata_map_.size());
        for (BitDataMap::const_iterator it = bitdata_map_.begin();
                it != bitdata_map_.end(); ++it)
        {
            data.push_back(it->second);
        }
    }

} // namespace core
} // namespace bittorrent