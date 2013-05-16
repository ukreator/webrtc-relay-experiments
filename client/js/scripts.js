/**
 * Copyright (C) SayMama Ltd 2012
 *
 * All rights reserved. Any use, copying, modification, distribution and selling
 * of this software and it's documentation for any purposes without authors'
 * written permission is hereby prohibited.
 */
/**
 * @fileoverview
 * @TODO file description
 *
 * @author Tadeusz Kozak
 * @date 13-07-2012 10:28
 */


/**
 * CA like Call App
 * @namespace
 */
CA = {};

(function (w, $) {

  /**
   * ===========================================================================
   * Public API
   * ===========================================================================
   */

  var clients = {};

  CA.onDomReady = function () {
    _initLogging();
    _initUI();
    _initRTTransport();
    CA.initDevs();
  };

  CA.join = function () {
    var scopeId = $('#scopeIdInput').val();
    log.debug("[CA] = Joining scope with id; " + scopeId);
    CA.ownClientId = _genRandomUserId();
    log.debug("[CA] = Generated client id: " + CA.ownClientId);
    
    var clientPC =
        new CA.PeerConnection(CA.selectedDevsSet, 'remoteVideoRenderer');

    clientPC.setSignalingTransportHandler(CA.RealtimeTransport);

    var onOffer = function (iceUfrag, icePwd) {
      CA.RealtimeTransport.authRequest(CA.ownClientId, scopeId, iceUfrag, icePwd);
    };
    
    // this will trigger ICE discovery:
    clientPC.makeOffer(onOffer);
    CA.clientPC = clientPC;
    //clients[clientId] = clientPC;
    
    CA.joinedScope = scopeId;
  };

  CA.leave = function () { /*
    log.debug("[CA] = Leaving scope: " + CA.joinedScope);
    CA.RealtimeTransport.leaveScope(CA.joinedScope);
    for (var clientId in clients) {
      var pc = clients[clientId].close();
    }
    clients = {};
    delete CA.joinedScope; */
  };


  /**
   * ===========================================================================
   * Real-time transport (signaling protocol) events processing.
   * ===========================================================================
   */


  function _onAuthResponse(data) {
  
    log.debug("Got auth response: " + JSON.stringify(data));
    
    // create offer
    // replace crypto keys and ssrsc
    // call setLocalDescription()
    // start ICE discovery
    // create answer manually from remote data (try ICE-LITE)
    // call setRemoteDescription
  
    CA.clientPC.handleAuthResponse(data); 
  }


  function _onPeerMsg(msg) {

  }


  function _onClientLeft(clientId) {
    log.debug("[CA] = Got client left " + clientId);
    var clientPC = clients[clientId];
    if (clientPC) {
      clientPC.close();
    } else {
      log.warn("[CA] = Got client left for unknown client");
    }
  }

  /**
   * ===========================================================================
   * Initialization
   * ===========================================================================
   */


  function _initLogging() {
    CA.log = log4javascript.getLogger();
    w.log = CA.log;
    CA.inPageAppender = new log4javascript.InPageAppender("logsContainer");
    CA.inPageAppender.setHeight("500px");
    CA.log.addAppender(CA.inPageAppender);

  }


  function _initUI() {
    $('#joinBtn').click(CA.join);
    $('#leaveBtn').click(CA.leave);
  }


  function _initRTTransport() {
    var url = 'ws://' + window.location.hostname + ':10000';
    setTimeout(function () {
      CA.RealtimeTransport.connect(url);
      CA.RealtimeTransport.setMsgListener(
          {
            onAuthResponse:_onAuthResponse,
            onClientLeft:_onClientLeft,
            onPeerMsg:_onPeerMsg
          }
      );
    }, 1000);

  }


  function _genRandomUserId() {
    return Math.floor(Math.random() * 10000);
  }

  w.onerror = function (message, url, line) {
    message += '';
    url += '';
    var lastSlash = url.lastIndexOf('/');
    if (lastSlash) {
      url = url.substring(lastSlash + 1, url.length);
    }
    w.log.error("[CA] = Got uncaught JS error: " + message + ' (' + url + ':' +
        line + ')');
  };

  $(CA.onDomReady);

})(window, jQuery);

