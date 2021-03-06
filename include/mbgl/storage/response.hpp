#ifndef MBGL_STORAGE_RESPONSE
#define MBGL_STORAGE_RESPONSE

#include <string>
#include <memory>

namespace mbgl {

class Response {
public:
    bool isExpired() const;

public:
    enum Status { Error, Successful, NotFound };

    Status status = Error;
    bool stale = false;
    std::string message;
    int64_t modified = 0;
    int64_t expires = 0;
    std::string etag;
    std::shared_ptr<const std::string> data;
};

}

#endif
