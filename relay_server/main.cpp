#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <json/json.h>
#include <boost/foreach.hpp>
#include <iostream>
#include <set>

#define LOG_D(x) std::cout << x << std::endl
#define LOG_E(x) LOG_D(x)

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

class BroadcastServer
{
public:
    BroadcastServer()
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
		if (msgType == "joinScope")
		{
			result["type"] = "newClient";
			Json::Value data;
			data["scopeId"] = "1";
			data["clientId"] = root["data"]["clientId"];
			result["data"] = data;
		}
		else
		{
			result = root;
		}

		Json::FastWriter writer;
		std::string resultMsg = writer.write(result);

        BOOST_FOREACH (connection_hdl it, _connections)
        {
			if (hdl.lock() != it.lock())
				_server.send(it, resultMsg, websocketpp::frame::opcode::TEXT);
        }
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
};

int main()
{
    LOG_D("starting server");
    BroadcastServer server;
    boost::thread thr(bind(&BroadcastServer::run, &server, 10000));
    
    thr.join();
    LOG_D("server finished working");

    return 0;
}