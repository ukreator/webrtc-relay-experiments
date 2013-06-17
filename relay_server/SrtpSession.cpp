#include <SrtpSession.hpp>
#include <Log.hpp>
#include <srtp.h>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <string>

const int SRTCP_INDEX_LEN = 4;
const int SRTCP_MAX_TRAILER_LEN = SRTP_MAX_TRAILER_LEN + SRTCP_INDEX_LEN;

namespace
{
/**
 * Helper class for lazy initialization of libsrtp context
 * and automatic clean up on application close.
 */
class GlobalLibsrtpContext
{
public:
    GlobalLibsrtpContext(): _initialized(false)
    {
        err_status_t status = srtp_init();
        assert(status == err_status_ok);
        if (status == err_status_ok)
            _initialized = true;
        LOG_D("libsrtp initialized");
    }

    ~GlobalLibsrtpContext()
    {
        if (_initialized)
            srtp_shutdown();
        LOG_D("libsrtp shut down");
    }

    static void init()
    {
        boost::call_once(_flag, initOnce);
    }

private:

    static void initOnce()
    {
        _globalCtx.reset(new GlobalLibsrtpContext());
    }

    bool _initialized;

    static boost::once_flag _flag;
    static boost::shared_ptr<GlobalLibsrtpContext> _globalCtx;
};

boost::once_flag GlobalLibsrtpContext::_flag;
boost::shared_ptr<GlobalLibsrtpContext> GlobalLibsrtpContext::_globalCtx;
}

SrtpSession::SrtpSession()
{
    GlobalLibsrtpContext::init();
}

void SrtpSession::setKey(const std::vector<sm_uint8_t>& keySalt,
                            SrtpSessionDirection direction)
{
    srtp_policy_t policy;
    memset(&policy, 0, sizeof(policy));
    crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
    policy.ssrc.type = (direction == SRTP_SESSION_DIRECTION_INBOUND) ?
        ssrc_any_inbound: ssrc_any_outbound;
    policy.ssrc.value = 0;
    // these values are used in Chrome
    policy.window_size = 1024;
    policy.allow_repeat_tx = 1;
    policy.next = NULL;

    policy.key = (unsigned char*)&keySalt[0];

    srtp_t* srtpCtx = new srtp_t;
    err_status_t status = srtp_create(srtpCtx, &policy);
    // TODO: error handling
    assert(status == err_status_ok);
    _srtpCtx = boost::shared_ptr<srtp_t>(srtpCtx, &SrtpSession::freeCtx);
}

size_t SrtpSession::protect(sm_uint8_t* data, size_t size)
{
    int packetLen = (int)size;
    err_status_t res = srtp_protect(*_srtpCtx, data, &packetLen);
    if (res != err_status_ok)
    {
        // TODO: error handling
        LOG_E("Failed to encode incoming RTP packet: " << res);
        return 0;
    }
    return packetLen;
}

size_t SrtpSession::protectRtcp(sm_uint8_t* data, size_t size)
{
    int packetLen = (int)size;
    err_status_t res = srtp_protect_rtcp(*_srtpCtx, data, &packetLen);
    if (res != err_status_ok)
    {
        // TODO: error handling
        LOG_E("Failed to encode incoming RTCP packet: " << res);
        return 0;
    }
    return packetLen;
}

size_t SrtpSession::unprotect(sm_uint8_t* data, size_t size)
{
    int packetLen = (int)size;
    err_status_t res = srtp_unprotect(*_srtpCtx, data, &packetLen);
    if (res != err_status_ok)
    {
        // TODO: error handling
        LOG_E("Failed to decode incoming RTP packet: " << res);
        return 0;
    }
    return packetLen;
}

size_t SrtpSession::unprotectRtcp(sm_uint8_t* data, size_t size)
{
    int packetLen = (int)size;
    err_status_t res = srtp_unprotect_rtcp(*_srtpCtx, data, &packetLen);
    if (res != err_status_ok)
    {
        // TODO: error handling
        LOG_E("Failed to decode incoming RTCP packet: " << res);
        return 0;
    }
    return packetLen;
}

void SrtpSession::freeCtx(srtp_t* ctx)
{
    srtp_dealloc(*ctx);
    delete ctx;
}


