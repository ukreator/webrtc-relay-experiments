#ifndef ___Scope_hpp__
#define ___Scope_hpp__

#include <string>
#include <TransportEndpoint.hpp>
#include <User.hpp>

#include <boost/foreach.hpp>

class Scope
{
public:
	Scope(const std::string& scopeId): _scopeId(scopeId)
	{
		_keyAndSalt = "jJv7OTSY+x5YFIisLr59b5OVIBCrHT+5gK6OZmNd";
	}

    std::vector<TransportEndpoint> getDownlinkEndpointsFor(int userId)
    {
        std::vector<TransportEndpoint> endpoints;
        // enumerate all users in this room except uplink one
        BOOST_FOREACH(UsersMap::value_type& u, _users)
        {
            if (u.first != userId) //< exclude media source
            {
                // search for userId in downlink connections for the specific user
                User::DownlinksMap::iterator it = u.second->_downlinks.find(userId);
                if (it != u.second->_downlinks.end())
                    endpoints.push_back(it->second.transportEndpoint);
            }
        }
        return endpoints;
    }

//private:
	std::string _scopeId;
	std::string _keyAndSalt;

    typedef std::map<int, UserPtr> UsersMap;
    UsersMap _users;
};


#endif
