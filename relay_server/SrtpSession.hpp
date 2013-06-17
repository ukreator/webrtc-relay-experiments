#ifndef ___SrtpSession_hpp__
#define ___SrtpSession_hpp__

#include <IntTypes.hpp>
#include <boost/shared_ptr.hpp>
#include <vector>

struct srtp_ctx_t;
typedef srtp_ctx_t* srtp_t;

extern const int SRTCP_MAX_TRAILER_LEN;

enum SrtpSessionDirection
{
    SRTP_SESSION_DIRECTION_INBOUND,
    SRTP_SESSION_DIRECTION_OUTBOUND
};

class SrtpSession
{
public:
    SrtpSession();

    void setKey(const std::vector<sm_uint8_t>& keySalt,
                                SrtpSessionDirection direction);

    size_t protect(sm_uint8_t* data, size_t size);

    size_t protectRtcp(sm_uint8_t* data, size_t size);

    size_t unprotect(sm_uint8_t* data, size_t size);

    size_t unprotectRtcp(sm_uint8_t* data, size_t size);

private:

    static void freeCtx(srtp_t* ctx);
    boost::shared_ptr<srtp_t> _srtpCtx;    
};

#endif
