#ifndef ___User_hpp__
#define ___User_hpp__

#include <IntTypes.hpp>
#include <IceCredentials.hpp>
#include <TransportEndpoint.hpp>
#include <Log.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <map>
#include <string>

enum MediaLinkType
{
    MEDIA_LINK_TYPE_UPLINK,
    MEDIA_LINK_TYPE_DOWNLINK,
    MEDIA_LINK_TYPE_UNKNOWN
};

struct LinkInfo
{
    LinkInfo(): peerAudioSsrc(0), peerVideoSsrc(0), streamerAudioSsrc(0), streamerVideoSsrc(0)
    {}

    boost::shared_ptr<IceCredentials> iceCredentials;
    TransportEndpoint transportEndpoint;
    sm_uint32_t peerAudioSsrc;
    sm_uint32_t peerVideoSsrc;
    sm_uint32_t streamerAudioSsrc;
    sm_uint32_t streamerVideoSsrc;
};

class User
{
public:
    User(int userId, const std::string& scopeId, const IceCredentialsPtr& iceCreds):
      _userId(userId), _scopeId(scopeId)
    {
        _uplink.iceCredentials = iceCreds;
    }

    void getIcePassword(MediaLinkType linkType, int downlinkUserId,
        sm_uint8_t **password, size_t *passwordLen)
    {
        IceCredentialsPtr icp;
        if (linkType == MEDIA_LINK_TYPE_UPLINK)
        {
            icp = _uplink.iceCredentials;
        }
        else if (linkType == MEDIA_LINK_TYPE_DOWNLINK)
        {
            icp = _downlinks[downlinkUserId].iceCredentials;
        }
        else
        {
            assert(!"link type is not set");
        }

        icp->verifyingPwd(password, passwordLen);
    }

    void updateIceEndpoint(MediaLinkType linkType, int downlinkUserId,
        const TransportEndpoint& te)
    {
        if (linkType == MEDIA_LINK_TYPE_UPLINK)
        {
            if (!_uplink.transportEndpoint.isSet())
            {
                _uplink.transportEndpoint = te;
                LOG_D("Updated uplink endpoint for user " << _userId);
            }
        }
        else if (linkType == MEDIA_LINK_TYPE_DOWNLINK)
        {
            if (!_downlinks[downlinkUserId].transportEndpoint.isSet())
            {
                _downlinks[downlinkUserId].transportEndpoint = te;
                LOG_D("Updated downlink endpoint for user " << _userId
                    << " to receive media from user " << downlinkUserId);
            }
        }
        else
        {
            assert(!"link type is not set");
        }
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

typedef boost::shared_ptr<User> UserPtr;

#endif