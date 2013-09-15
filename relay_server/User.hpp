#ifndef ___User_hpp__
#define ___User_hpp__

#include <IntTypes.hpp>
#include <IceCredentials.hpp>
#include <TransportEndpoint.hpp>
#include <SrtpSession.hpp>
#include <Log.hpp>
#include <memory>
#include <map>
#include <string>


enum class MediaLinkType
{
    MEDIA_LINK_TYPE_UPLINK,
    MEDIA_LINK_TYPE_DOWNLINK,
    MEDIA_LINK_TYPE_UNKNOWN
};

struct LinkInfo
{
    std::shared_ptr<IceCredentials> iceCredentials;

    std::vector<sm_uint8_t> peerKeySalt;
    std::vector<sm_uint8_t> streamerKeySalt;
    SrtpSession srtpPeerSession; // peer -> streamer
    SrtpSession srtpStreamerSession; // streamer -> peer

    TransportEndpoint transportEndpoint;
    sm_uint32_t peerAudioSsrc = 0;
    sm_uint32_t peerVideoSsrc = 0;
    sm_uint32_t streamerAudioSsrc = 0;
    sm_uint32_t streamerVideoSsrc = 0;
};

class User
{
public:
    User(int userId, const std::string& scopeId):
      _userId(userId), _scopeId(scopeId)
    {
    }

    void getIcePassword(MediaLinkType linkType, int downlinkUserId,
        sm_uint8_t **password, size_t *passwordLen)
    {
        IceCredentialsPtr icp;
        if (linkType == MediaLinkType::MEDIA_LINK_TYPE_UPLINK)
        {
            icp = _uplink.iceCredentials;
        }
        else if (linkType == MediaLinkType::MEDIA_LINK_TYPE_DOWNLINK)
        {
            icp = _downlinks[downlinkUserId].iceCredentials;
        }
        else
        {
            assert(!"link type is not set");
        }

        icp->verifyingPwd(password, passwordLen);
    }

    bool updateIceEndpoint(MediaLinkType linkType, int downlinkUserId,
        const TransportEndpoint& te)
    {
        if (linkType == MediaLinkType::MEDIA_LINK_TYPE_UPLINK)
        {
            if (!_uplink.transportEndpoint.isSet())
            {
                _uplink.transportEndpoint = te;
                LOG_D("Updated uplink endpoint for user " << _userId);
                return true;
            }
        }
        else if (linkType == MediaLinkType::MEDIA_LINK_TYPE_DOWNLINK)
        {
            if (!_downlinks[downlinkUserId].transportEndpoint.isSet())
            {
                _downlinks[downlinkUserId].transportEndpoint = te;
                LOG_D("Updated downlink endpoint for user " << _userId
                    << " to receive media from user " << downlinkUserId);
                return true;
            }
        }
        else
        {
            assert(!"link type is not set");
        }

        return false;
    }


//private:
    int _userId;
    std::string _scopeId;
    std::string _uplinkOfferSdp;
    LinkInfo _uplink;

    // mapping between userId and WebRTC link info
    typedef std::map<int, LinkInfo> DownlinksMap;
    DownlinksMap _downlinks;
};

typedef std::shared_ptr<User> UserPtr;

#endif
