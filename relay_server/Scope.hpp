#ifndef ___Scope_hpp__
#define ___Scope_hpp__

#include <string>
#include <TransportEndpoint.hpp>
#include <User.hpp>

#include <boost/foreach.hpp>

class Scope
{
public:
	Scope(const std::string& scopeId)
	{
	}

    std::vector<std::pair<TransportEndpoint, srtp_t> > getDownlinkEndpointsFor(int userId)
    {
        std::vector<std::pair<TransportEndpoint, srtp_t> > endpoints;
        // enumerate all users in this room except uplink one
        BOOST_FOREACH(UsersMap::value_type& u, _users)
        {
            if (u.first != userId) //< exclude media source
            {
                // search for userId in downlink connections for the specific user
                User::DownlinksMap::iterator it = u.second->_downlinks.find(userId);
                if (it != u.second->_downlinks.end())
                    endpoints.push_back(std::make_pair(it->second.transportEndpoint,
                        it->second.srtpStreamerSession));
            }
        }
        return endpoints;
    }

    void addUser(int userId, UserPtr user)
    {
        _users.insert(std::make_pair(userId, user));
    }

    UserPtr getUser(int userId)
    {
        UsersMap::iterator it = _users.find(userId);
        if (it != _users.end())
            return it->second;
        return UserPtr();
    }

private:
	std::string _scopeId;

    typedef std::map<int, UserPtr> UsersMap;
    UsersMap _users;
};


#endif
