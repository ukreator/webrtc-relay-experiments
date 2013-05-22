#ifndef ___UserServer_hpp__
#define ___UserServer_hpp__

#include <IntTypes.hpp>
#include <IceCredentials.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <map>
#include <string>

class UserEndpoint
{
public:

    boost::asio::ip::udp::endpoint _remoteEndpoint;
};

class User
{
public:
	User(int userId, const std::string& scopeId, const IceCredentials& iceCreds):
      _userId(userId), _scopeId(scopeId), _uplinkIceCreds(iceCreds)
	{}

//private:
	int _userId;
	std::string _scopeId;
	unsigned _audioSsrc;
	unsigned _videoSsrc;
	IceCredentials _uplinkIceCreds;

    //UserEndpoint _uplinkEndpoint;

    // mapping between userId and ICE credentials
    typedef std::map<int, boost::shared_ptr<IceCredentials> > DownlinkIceCredentials;
    DownlinkIceCredentials _downlinkIceCredentials;
};

typedef boost::shared_ptr<User> UserPtr;

#endif