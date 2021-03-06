#ifndef URI_H
#define URI_H

#include <string>
#include <sstream>

namespace bitwave {
namespace http {

    // a simply URI class to present a uri.
    // use a valid url string to construct a object, then we can get host
    // from the url, and we can add query to URI, then we can get a query uri string
    class URI
    {
    public:
        explicit URI(const std::string& url)
            : numofquery_(0),
              query_(url),
              host_()
        {
            ParseHostFromUri();
        }

        std::string GetHost() const
        {
            return host_;
        }

        std::string GetQueryString() const
        {
            return query_;
        }

        void AddQuery(const char *kbegin, const char *kend,
                      const char *vbegin, const char *vend);

        template<typename Key, typename Value>
        void AddQuery(const Key& key, const Value& value)
        {
            std::string kstr = TransformToString(key);
            std::string vstr = TransformToString(value);
            AddQuery(kstr.c_str(), kstr.c_str() + kstr.size(), vstr.c_str(), vstr.c_str() + vstr.size());
        }

        template<typename Key>
        void AddQuery(const Key& key, const char *binary, int binlen)
        {
            std::string kstr = TransformToString(key);
            AddQuery(kstr.c_str(), kstr.c_str() + kstr.size(), binary, binary + binlen);
        }

    private:
        template<typename T>
        std::string TransformToString(const T& t) const
        {
            std::ostringstream oss;
            oss << t;
            return oss.str();
        }

        bool IsUnReserved(char c) const;
        void AppendPercentEncode(char c);
        void AppendQueryLinkChar();
        void AppendQueryBeginChar();
        void AppendEscapeData(const char *begin, const char *end);
        void ParseHostFromUri();

        int numofquery_;
        std::string query_;
        std::string host_;
    };

} // namespace http
} // namespace bitwave

#endif // URI_H
