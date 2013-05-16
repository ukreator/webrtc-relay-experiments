
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
    log.debug('Sending auth request');
    socket.emit(CA.MessageType.AUTH_REQUEST,
      authRequest);
  };
  
  CA.RealtimeTransport.sendIceCandidate = function (mediaType, foundation, priority, ipAddr, port) {
    var iceCandidate = {
      mediaType: mediaType,
      ipAddr: ipAddr,
      port: port,
      foundation: foundation,
      priority: priority
    };
    log.debug('Sending ICE candidate');
    socket.emit(CA.MessageType.ICE_CANDIDATE,
      iceCandidate);
  };
  
  /*
  CA.RealtimeTransport.joinScope = function (scopeId, clientId) {
    log.debug("[RT] = Joining scope with id: " + scopeId);
    socket.emit('joinScope', {scopeId:scopeId, clientId:clientId});
  };

  CA.RealtimeTransport.leaveScope = function (scopeId) {
    log.debug("[RT] = Leaving scope with id: " + scopeId);
    socket.emit('leaveScope', scopeId);
  };

  CA.RealtimeTransport.emitPeerMsg = function (msg) {
    log.debug("[RT] = Emitting peer message in scope: " + msg.scopeId
        + ' for user with id: ' + msg.recipientId);
    socket.emit('peerMsg', msg);
  };
  
  */

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

  function _onPeerMsg(msg) {
    msgListener.onPeerMsg(msg);
  }


  function _onMessage(e) {
    var serverMessage = JSON.parse(e.data);
    var h;
    switch (serverMessage.type) {
      case CA.MessageType.AUTH_RESPONSE:
        h = _onAuthResponse;
        break;
      //case 'clientLeft':
      //  h = _onClientLeft;
      //  break;
      //case 'peerMsg':
      //  h = _onPeerMsg;
      //  break;
      default:
        log.error('Message type ' + serverMessage.type + ' is unknown');
        return;
    }
    h(serverMessage.data);
  }

  /*
  CA.PeerMessage = function (scopeId, senderId, recipientId, type, data) {
    this.scopeId = scopeId;
    this.senderId = senderId;
    this.recipientId = recipientId;
    this.type = type;
    this.data = data;
  };
  */

  CA.MessageType = {
    AUTH_REQUEST:'authRequest',
    AUTH_RESPONSE:'authResponse',
    ICE_CANDIDATE:'iceCandidate'
  };


}(window, jQuery));
