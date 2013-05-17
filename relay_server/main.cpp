#include <UdpServer.hpp>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <json/json.h>
#include <stun/usages/ice.h>
#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>
#include <set>

#define LOG_D(x) std::cout << x << std::endl
#define LOG_E(x) LOG_D(x)

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

class Scope
{
public:
	Scope(const std::string& scopeId): _scopeId(scopeId)
	{
		_keyAndSalt = "jJv7OTSY+x5YFIisLr59b5OVIBCrHT+5gK6OZmNd";
	}

	

//private:
	std::string _scopeId;
	std::string _keyAndSalt;
};

Scope gGlobalScope("1");


class User
{
public:
	User(int userId, const std::string& scopeId): _userId(userId), _scopeId(scopeId),
		_localIceUfrag("2PwlB+YBOsVDyQOa"), _localIcePwd("o1OpQyxdcTf529zMnCuylkqq")
	{}

//private:
	int _userId;
	std::string _scopeId;
	unsigned _audioSsrc;
	unsigned _videoSsrc;
	std::string _localIceUfrag;
	std::string _localIcePwd;
	std::string _remoteIceUfrag;
	std::string _remoteIcePwd;
};

typedef boost::shared_ptr<User> UserPtr;

class BroadcastServer
{
public:
	BroadcastServer(): _ssrcCounter(0)
    {
        //_server.set_access_channels(websocketpp::log::alevel::all);
        //_server.clear_access_channels(websocketpp::log::alevel::frame_payload);
        _server.init_asio();
                
        _server.set_open_handler(bind(&BroadcastServer::onOpen,this,::_1));
        _server.set_close_handler(bind(&BroadcastServer::onClose,this,::_1));
        _server.set_message_handler(bind(&BroadcastServer::onMessage,this,::_1,::_2));
    }
    
    void onOpen(connection_hdl hdl)
    {
        _connections.insert(hdl);
        LOG_D("New connection opened");
    }
    
    void onClose(connection_hdl hdl)
    {
        _connections.erase(hdl);
        LOG_D("Connection closed");
    }
    
    void onMessage(connection_hdl hdl, server::message_ptr msg)
    {
		// 1. auth message handling, get client ID, return
		//  -- crypto keys
		//  -- SSRCs
		//  -- ICE credentials
		//  -- ICE candidate
		// 2. Generate answer on JS client side based on info from auth response
		// 3. create SDP offer on client, get needed info from it, replace some attributes
		// - consume and analyze offer (in JSON, not SDP):
		//  -- SSRCs
		//  -- audio/video published status
		//  -- ICE credentials
		// - wait for candidates, put them in map or send to libnice
        LOG_D("Got message: " << msg->get_payload());

		Json::Reader reader;
		Json::Value root;
		bool parsed = reader.parse(msg->get_payload(), root);
		if (!parsed)
		{
			LOG_E("Failed to parse message");
			return;
		}

		Json::Value result;

		std::string msgType = root["type"].asString();
		if (msgType == "authRequest")
		{
			int userId = root["data"]["userId"].asInt();
			std::string scopeId = root["data"]["scopeId"].asString();
			UserPtr user = boost::make_shared<User>(userId, scopeId);
			user->_remoteIceUfrag = root["data"]["iceUfrag"].asString();
			user->_remoteIcePwd = root["data"]["icePwd"].asString();

			unsigned audioSsrc = newSsrc();
			unsigned videoSsrc = newSsrc();

			_signalingUsers.insert(std::make_pair(hdl, user));
			_ssrcUsers.insert(std::make_pair(audioSsrc, user));
			_ssrcUsers.insert(std::make_pair(videoSsrc, user));


			result["type"] = "authResponse";
			Json::Value data;
			data["cryptoKey"] = gGlobalScope._keyAndSalt;
			data["audioSsrc"] = audioSsrc;
			data["videoSsrc"] = videoSsrc;
			data["iceUfrag"] = user->_localIceUfrag;
			data["icePwd"] = user->_localIcePwd;
			// foundation component-id protocol priority address port type
			data["candidate"] = "0 1 UDP 2113667327 192.168.1.33 7000 typ host";
			data["port"] = "7000"; //< for m= lines
			data["address"] = "192.168.1.33"; //< for c= lines
			

			result["data"] = data;
			sendJson(hdl, result);
		}
		else if (msgType == "iceCandidate")
		{
			// start ICE probing on new candidates
			/*
			    var iceCandidate = {
				  mediaType: mediaType,
				  ipAddr: ipAddr,
				  port: port,
				  foundation: foundation,
				  priority: priority
				};
			*/

			SignalingMap::iterator it = _signalingUsers.find(hdl);
			if (it == _signalingUsers.end())
			{
				LOG_E("Not authenticated user");
				return;
			}

			UserPtr user = it->second;

			unsigned short port = boost::lexical_cast<unsigned short>(root["data"]["port"].asString());
			std::string ipAddr = root["data"]["ipAddr"].asString();

			// - add candidate to candidate list (?)
			// - create a pair to check with local address:port
			// - put this pair to check list for a specific user
			// - sort check list, do ICE operations
			// - if nominated pair is found, stop ICE operations for specific user

			// UDP server side:
			// - maintain global remote IP:port mapping to User
			// - for production: filter off private IP addresses
			// - get remote endpoint from incoming STUN packet
			// - find corresponding User
			// - STUN credentials for packets coming from Chrome:
			//  - server_ufrag:chrome_ufrag chrome_pass
			// - for response - same uname and pass, without USERNAME in packet

			// ICE-LITE:
			// - looks nice (no candidates gathering via STUN and no candidates sending to server)
			// - problem: how to answer connectivity checks? where to find credentials?
		}

		//Json::FastWriter writer;
		//std::string resultMsg = writer.write(result);

  //      BOOST_FOREACH (connection_hdl it, _connections)
  //      {
		//	if (hdl.lock() != it.lock())
		//		_server.send(it, resultMsg, websocketpp::frame::opcode::TEXT);
  //      }
    }

	void sendJson(connection_hdl hdl, const Json::Value& msg)
	{
		Json::FastWriter writer;
		std::string resultMsg = writer.write(msg);
		_server.send(hdl, resultMsg, websocketpp::frame::opcode::TEXT);
	}

	unsigned newSsrc()
	{
		return ++_ssrcCounter;
	}

    void run(uint16_t port)
    {
        _server.listen(port);
        _server.start_accept();

        try
        {
            _server.run();
        }
        catch (const std::exception & e)
        {
            std::cout << e.what() << std::endl;
        }
        catch (websocketpp::lib::error_code e)
        {
            std::cout << e.message() << std::endl;
        }
        catch (...)
        {
            std::cout << "other exception" << std::endl;
        }
    }
private:
    server _server;
    std::set<connection_hdl> _connections;
	unsigned _ssrcCounter;

    // for SSRC mapping
    std::map<unsigned, UserPtr> _ssrcUsers;
    // for signaling connection mapping
    typedef std::map<connection_hdl, UserPtr> SignalingMap;
    SignalingMap _signalingUsers;
};

int main()
{
    LOG_D("starting server");
    BroadcastServer server;
    UdpServer udpServer(boost::asio::ip::address_v4::from_string("192.168.1.33"));

    boost::thread thr(bind(&BroadcastServer::run, &server, 10000));
    udpServer.start(7000);
    

    //udpServer.stop();
    thr.join();
    LOG_D("server finished working");

    return 0;
}