#ifndef ___SrtpSession_hpp__
#define ___SrtpSession_hpp__

#include <IntTypes.hpp>
#include <srtp.h>
#include <string>

// TODO: create class with hidden libsrtp calls and RAII style

void initSrtpSession(srtp_t* srtpCtx, const std::vector<sm_uint8_t>& keySalt,
                                ssrc_type_t direction)
{
    srtp_policy_t policy;
    memset(&policy, 0, sizeof(policy));
    crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
    policy.ssrc.type = direction;
    policy.ssrc.value = 0;
    policy.window_size = 1024;
    policy.allow_repeat_tx = 1; // enable for now
    policy.next = NULL;

    //std::vector<sm_uint8_t> key = keySalt;
    policy.key = (unsigned char*)&keySalt[0];

    err_status_t status = srtp_create(srtpCtx, &policy);
    assert(status == err_status_ok);
    // TODO: add events handling
}

#endif
