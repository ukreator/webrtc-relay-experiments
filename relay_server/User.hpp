#ifndef ___User_hpp__
#define ___User_hpp__

#include <IntTypes.hpp>
#include <IceCredentials.hpp>
#include <TransportEndpoint.hpp>
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
    //explicit LinkInfo(boost::mt19937& gen): iceCredentials(gen)
    //{}

    boost::shared_ptr<IceCredentials> iceCredentials;
    TransportEndpoint transportEndpoint;
    //MediaLinkType 
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
            _uplink.transportEndpoint = te;
        }
        else if (linkType == MEDIA_LINK_TYPE_DOWNLINK)
        {
            _downlinks[downlinkUserId].transportEndpoint = te;
        }
        else
        {
            assert(!"link type is not set");
        }
    }

//private:
    int _userId;
    std::string _scopeId;
    unsigned _audioSsrc;
    unsigned _videoSsrc;
    LinkInfo _uplink;

    // mapping between userId and WebRTC link info
    typedef std::map<int, LinkInfo> DownlinksMap;
    DownlinksMap _downlinks;
};

typedef boost::shared_ptr<User> UserPtr;

#endif