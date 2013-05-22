
(function (w, $) {

  'use strict';

  CA.RealtimeTransport = {};

  /**
   * ===================================================================
   * Scope constants
   * ===================================================================
   */


  /**
   * ===================================================================
   * Scope variables
   * ===================================================================
   */

  /**
   * Global socket - for listening on global events (e.g. room created)
   */
  var socket;

  var msgListener;

  /**
   * ===================================================================
   * Public API
   * ===================================================================
   */

  CA.RealtimeTransport.setMsgListener = function (l) {
    log.debug("[RT] = Setting client listener");
    msgListener = l;
  };

  CA.RealtimeTransport.connect = function (url) {
    log.debug('[RT] = Connecting WebSocket to : ' + url);

    socket = new WebSocket(url); 

    socket.onopen = function () {
      log.debug("[RT] = Successfully connected");
    };
    socket.emit = function (msgType, msg) {
      var clientMsg = {
        type: msgType,
        data: msg
      };
      socket.send(JSON.stringify(clientMsg));
    };
    socket.onmessage = _onMessage;
    socket.onerror = function () {
      log.debug("[RT] = Connection to WS server closed");
    };
  };

  CA.RealtimeTransport.authRequest = function (userId, scopeId, iceUfrag, icePwd) {
    var authRequest = {
      userId: CA.ownClientId,
      scopeId: scopeId,
      iceUfrag: iceUfrag,
      icePwd: icePwd
    };
    log.debug('[RT] = Sending auth request');
    socket.emit(CA.MessageType.AUTH_REQUEST,
      authRequest);
  };
  
  CA.RealtimeTransport.newDownlinkConnection = function (userId, iceUfrag, icePwd) {
    var request = {
      eventType: 'startNewDownlink',
      userId: userId,
      iceUfrag: iceUfrag,
      icePwd: icePwd
    };
    log.debug('[RT] = Sending request to start new downlink connection: ' + request);
    socket.emit(CA.MessageType.USER_EVENT,
      request);
  };
  
  CA.RealtimeTransport.sendIceCandidate = function (mediaType, foundation, priority, ipAddr, port) {
    var iceCandidate = {
      mediaType: mediaType,
      ipAddr: ipAddr,
      port: port,
      foundation: foundation,
      priority: priority
    };
    log.debug('[RT] = Sending ICE candidate');
    socket.emit(CA.MessageType.ICE_CANDIDATE,
      iceCandidate);
  };
  

  /**
   * ===================================================================
   * Private helpers
   * ===================================================================
   */
  function _onAuthResponse(data) {
    msgListener.onAuthResponse(data);
  }

  function _onClientLeft(data) {
    msgListener.onClientLeft(data);
  }

  function _onUserEvent(msg) {
    msgListener.onUserEvent(msg);
  }


  function _onMessage(e) {
    var serverMessage = JSON.parse(e.data);
    var h;
    switch (serverMessage.type) {
      case CA.MessageType.AUTH_RESPONSE:
        h = _onAuthResponse;
        break;
      case CA.MessageType.USER_EVENT:
        h = _onUserEvent;
        break;
      default:
        log.error('Message type ' + serverMessage.type + ' is unknown');
        return;
    }
    h(serverMessage.data);
  }

  CA.MessageType = {
    AUTH_REQUEST:'authRequest',
    AUTH_RESPONSE:'authResponse',
    ICE_CANDIDATE:'iceCandidate',
    USER_EVENT: 'userEvent'
  };

  CA.UserEventTypes = {
    NEW_USER: 'newUser',
    DOWNLINK_CONNECTION_ANSWER: 'downlinkConnectionAnswer'
  };

}(window, jQuery));
