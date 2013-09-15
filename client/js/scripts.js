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
    CA.initDevs({audio: true, video: true});
  };

  CA.join = function () {
    var scopeId = $('#scopeIdInput').val();
    log.debug("[CA] = Joining scope with id; " + scopeId);
    CA.ownClientId = _genRandomUserId();
    log.debug("[CA] = Generated client id: " + CA.ownClientId);
    
    var uplinkConnection =
        new CA.PeerConnection(CA.PeerConnectionType.PC_TYPE_UPLINK, {localStream: CA.selectedDevsSet});

    uplinkConnection.setSignalingTransportHandler(CA.RealtimeTransport);

    var onOffer = function (iceUfrag, icePwd, cryptoKey, offerSdp) {
      CA.RealtimeTransport.authRequest(CA.ownClientId, scopeId, iceUfrag, icePwd, cryptoKey, offerSdp);
    };
    
    // this will trigger ICE discovery:
    uplinkConnection.makeOffer(onOffer);
    CA.uplinkConnection = uplinkConnection;
    CA.downlinkConnections = {};
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

  CA.toggleVideo = function () {
    CA.videoEnabled = !CA.videoEnabled;
    CA.selectedDevsSet.getVideoTracks()[0].enabled = CA.videoEnabled;
    //CA.uplinkConnection.changeMediaStatus(1, CA.videoEnabled);
    CA.RealtimeTransport.mediaPublishStatus(CA.ownClientId, true, CA.videoEnabled);
  };

  /**
   * ===========================================================================
   * Real-time transport (signaling protocol) events processing.
   * ===========================================================================
   */


  function _onAuthResponse(data) {
    log.debug("Got auth response: " + JSON.stringify(data));
    CA.uplinkConnection.handleStreamerAnswer(data);
  }


  function _onUserEvent(uevent) {
    log.debug('Got user event: ' + JSON.stringify(uevent));
    switch (uevent.eventType) {
      case CA.UserEventTypes.NEW_USER:
        // todo: assign new renderer for each new user
        var downlinkConnection =
            new CA.PeerConnection(CA.PeerConnectionType.PC_TYPE_DOWNLINK, {rendererId: 'remoteVideoRenderer'});
        downlinkConnection.setSignalingTransportHandler(CA.RealtimeTransport);
        var onOffer = function (iceUfrag, icePwd, cryptoKey, sdp) {
          CA.RealtimeTransport.newDownlinkConnection(uevent.userId, iceUfrag, icePwd, cryptoKey, sdp);
        };
        downlinkConnection.makeOffer(onOffer);
        CA.downlinkConnections[uevent.userId] = downlinkConnection;
        break;
      case CA.UserEventTypes.DOWNLINK_CONNECTION_ANSWER:
        CA.downlinkConnections[uevent.userId].handleStreamerAnswer(uevent);
        break;
      case CA.UserEventTypes.MEDIA_PUBLISH_STATUS:
        _onRemoteUserMediaStatus(uevent);
        break;
    }
  }

  function _onRemoteUserMediaStatus(msg) {
    log.debug('Handling publish/unpublish event from remote user');
    // TODO: offer-answer adjustment (not quite safe), renderer disposing
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
    CA.videoEnabled = true;
    $('#toggleVideoBtn').click(CA.toggleVideo);
  }


  function _initRTTransport() {
    var url = 'ws://' + window.location.hostname + ':10000';
    setTimeout(function () {
      CA.RealtimeTransport.connect(url);
      CA.RealtimeTransport.setMsgListener(
          {
            onAuthResponse:_onAuthResponse,
            onUserEvent:_onUserEvent
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

