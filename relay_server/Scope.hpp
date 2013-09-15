#ifndef ___Scope_hpp__
#define ___Scope_hpp__

#include <TransportEndpoint.hpp>
#include <SrtpSession.hpp>
#include <User.hpp>

#include <string>

class Scope
{
public:
	Scope(const std::string& scopeId)
	{
	}

    std::vector<std::pair<TransportEndpoint, SrtpSession> > getDownlinkEndpointsFor(int userId)
    {
        std::vector<std::pair<TransportEndpoint, SrtpSession> > endpoints;
        // enumerate all users in this room except uplink one
        for (auto& u: _users)
        {
            if (u.first != userId) //< exclude media source
            {
                // search for userId in downlink connections for the specific user
                auto it = u.second->_downlinks.find(userId);
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
        auto it = _users.find(userId);
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
