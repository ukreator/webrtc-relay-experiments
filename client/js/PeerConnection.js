

(function (w, $) {
  'use strict';

  var CA = w.CA,
      PeerConnection = w.PeerConnection,
      RTCSessionDescription = w.RTCSessionDescription,
      RTCIceCandidate = w.RTCIceCandidate,
      URL = w.URL,
      log;

  CA.PeerConnectionType = {
    PC_TYPE_UPLINK: 'pc_type_uplink',
    PC_TYPE_DOWNLINK: 'pc_type_downlink'
  };
      

  CA.PeerConnection = function (connectionType, renderingParams) {
    log = w.log;
    log.debug("[PC] = Creating new PeerConnection of type " + connectionType);
    var pcConfig = {"iceServers":[
      {"url":"stun:stun.l.google.com:19302"}
    ]};
    var pcConstraints = {"optional":[
      {"DtlsSrtpKeyAgreement":false}
    ]};
    
    this.connectionType = connectionType;

    this._nativePC = new PeerConnection(pcConfig, pcConstraints);
    this._nativePC.onicecandidate = this._createProxy('_onLocalIceCandidate');
    this._nativePC.oniceconnectionstatechange = this._createProxy('_onIceConnectionStateChange');
    this._nativePC.onconnecting =
        this._createProxy('_onPeerConnectionConnecting');
    this._nativePC.onopen =
        this._createProxy('_onPeerConnectionOpen');
    this._nativePC.onaddstream =
        this._createProxy('_onPeerConnectionStreamAdded');
    this._nativePC.onremovestream =
        this._createProxy('_onPeerConnectionStreamRemoved');
    this._nativePC.onstatechanged =
        this._createProxy('_onPeerConnectionStateChanged');
    this._localEndpoints = [];
    this.renderingParams = renderingParams;


    this.state = CA.PeerConnection.ConnectionState.NOT_CONNECTED;
    log.debug("[PC] = PeerConnection created");
  };


  CA.PeerConnection.ConnectionState = {

    /**
     * Initial state - after constructor and close
     */
    NOT_CONNECTED:'NOT_CONNECTED',

    /**
     * After sending an offer, before receiving an answer.
     */
    CONNECTING:'CONNECTING',

    /**
     * After receiving an offer and preparing answer to it;
     * After receiving an answer.
     */
    CONNECTED:'CONNECTED'
  };



  CA.PeerConnection.prototype.setSignalingTransportHandler = function (transport) {
    log.debug("[PC] = Got transport handler registered");
    this.signalingTransport = transport;
  };



  CA.PeerConnection.prototype.makeOffer = function (resultH) {
    log.debug("[PC] = Preparing an offer");
    var sdpConstraints = {'mandatory': {
      'OfferToReceiveAudio': (this.connectionType == CA.PeerConnectionType.PC_TYPE_DOWNLINK),
      'OfferToReceiveVideo': (this.connectionType == CA.PeerConnectionType.PC_TYPE_DOWNLINK) }};

    var self = this;
    // add data source for uplink connection only
    if (this.renderingParams.localStream) {
      this._nativePC.addStream(this.renderingParams.localStream);
    }

    var onOffer = function (sdp) {
      log.debug("[PC] = Offer prepared");     
      var mgSdp = new ManageableSDP(sdp);
      
      // use from audio, as we assume bundled connection
      resultH(mgSdp.mediaSections[0].attributes['ice-ufrag'],
              mgSdp.mediaSections[0].attributes['ice-pwd'],
              // For use in handleStreamAnswer, JS -> relay -> JS
              JSON.stringify(sdp));
    };
    
    var onOfferFailed = function (inf) {
      log.error("[PC] = createOffer failed: " + inf);
    };

    this._nativePC.createOffer(onOffer, onOfferFailed, sdpConstraints);
  };

  CA.PeerConnection.prototype.handleStreamerAnswer = function (params) {
    log.debug("[PC] = Handling streamer answer");
    var self = this;

    var mgOfferSdp = ManageableSDP.fromString(params.offerSdp);
    var mgAnswerSdp;

    var mediaDirection;
    var uplink = this.connectionType === CA.PeerConnectionType.PC_TYPE_UPLINK;
    if (uplink) {
      mediaDirection = 'sendonly';
    } else {
      mediaDirection = 'recvonly';
      mgAnswerSdp = ManageableSDP.fromString(params.answerSdp);
    }
    
    // assume audio first, then video m line
    params.offerSsrc = [params.peerAudioSsrc, params.peerVideoSsrc];
    params.answerSsrc = [params.streamerAudioSsrc, params.streamerVideoSsrc];

    log.debug('[PC] = Parsing and modifying initial offer without candidates: \r\n' + mgOfferSdp.sdp);
    var mgSdp = this._prepareOffer(params, mgOfferSdp, mediaDirection);
    mgSdp.flush();
    var adjustedOfferSdp = mgSdp.toRtcSessionDescription();

    log.debug('[PC] = Adjusted offer: ' + adjustedOfferSdp.sdp);
    self._nativePC.setLocalDescription(adjustedOfferSdp, onLocalDescriptorSet, function(msg){log.error(msg)});
   
    function onLocalDescriptorSet() {
      // we have all required data to create SDP answer in behalf of streamer
      var answerSdp;
      if (uplink) {
        // use offer SDP as a template for now
        answerSdp = self._prepareUplinkAnswer(params, adjustedOfferSdp);
      } else {
        answerSdp = self._prepareDownlinkAnswer(params, mgAnswerSdp);
      }
      log.debug("[PC] = Answer created to feed setRemoteDescription: " + answerSdp.sdp);
    
      function onRemoteDescriptionSet() {
        log.debug('[PC] = Remote SDP set');
      }
    
      self._nativePC.setRemoteDescription(answerSdp, onRemoteDescriptionSet, function(msg){log.error(msg)});
    }
  };

  CA.PeerConnection.prototype.close = function () {
    log.debug("[PC] = Closing a connection");
    this._nativePC.close();
    this.state = CA.PeerConnection.ConnectionState.NOT_CONNECTED;
  };

  CA.PeerConnection.prototype._prepareOffer = function (params, offerSdp, mediaDirection) {
    var mgSdp = new ManageableSDP(offerSdp);
    for (var i = 0; i < mgSdp.mediaSections.length; i++) {
      this._prepareOfferMediaSection(mgSdp.mediaSections[i], params.cryptoKey, params.offerSsrc[i], mediaDirection);
    }
    return mgSdp;
  }

  CA.PeerConnection.prototype._prepareOfferMediaSection = function (mediaSection, cryptoKey, ssrc, mediaDirection) {
    // set values from relay
    // 1. common SRTP key for all peers in a scope
    mediaSection.crypto.key = cryptoKey;
    // 2. unique server-generated SSRCs
    mediaSection.ssrc = ssrc;
    // 3. RFC 5245 and http://tools.ietf.org/html/draft-ivov-mmusic-trickle-ice-01
    //    compatibility
    mediaSection.attributes['ice-options'] = 'trickle';
    // 4. sendonly for uplink, recvonly for downlinks
    mediaSection.direction = mediaDirection;
  }
  
  CA.PeerConnection.prototype._prepareUplinkAnswer = function (params, offerSdp) {
    var mgAnswer = new ManageableSDP(offerSdp);
    this._setStreamerEndpointParams(mgAnswer, params);
    
    // required field even if no media to send
    // see http://tools.ietf.org/html/draft-ietf-mmusic-msid-00
    mgAnswer.globalAttributes['msid-semantic'] = 'WMS'; // random string is not needed
    
    for (var i = 0; i < mgAnswer.mediaSections.length; i++) {
      // relay side is downlink-only connection
      mgAnswer.mediaSections[i].direction = 'recvonly';
      // remove SSRC lines from original offer
      // TODO: add streamer SSRCs if necessary (params.answerSsrc has these values)
      mgAnswer.mediaSections[i].ssrcLabels = [];
    }

    mgAnswer.flush();
    return mgAnswer.toRtcSessionDescription();
  }
  
  CA.PeerConnection.prototype._prepareDownlinkAnswer = function (params, offerSdp) {
    var mgAnswer = new ManageableSDP(offerSdp);
    for (var i = 0; i < mgAnswer.mediaSections.length; i++) {
      // SSRC values from remote user's uplink connection
      mgAnswer.mediaSections[i].ssrc = params.answerSsrc[i];
    }
    this._setStreamerEndpointParams(mgAnswer, params);
    mgAnswer.flush();
    return mgAnswer.toRtcSessionDescription();
  }
  
  CA.PeerConnection.prototype._setStreamerEndpointParams = function (manageableSdp, params) {
    // relay is always answerer:
    manageableSdp.type = "answer";
    manageableSdp.originator = "relay 20518 0 IN IP4 " + params.address;

    // comply to http://tools.ietf.org/html/draft-ivov-mmusic-trickle-ice-01:
    manageableSdp.globalAttributes['end-of-candidates'] = "";
    manageableSdp.globalAttributes['ice-lite'] = "";

    for (var i = 0; i < manageableSdp.mediaSections.length; i++) {
      this._setStreamerEndpointParamsMediaSection(manageableSdp.mediaSections[i], params);
    }
  }

  CA.PeerConnection.prototype._setStreamerEndpointParamsMediaSection = function (mediaSection, params) {
    mediaSection.attributes['ice-options'] = 'trickle';
    // the only candidate with public relay's IP and fixed port
    mediaSection.attributes['candidate'] = params.candidate;
    mediaSection.port = params.port;

    // todo: add to parser
    //mediaSection.attributes['c'] = 'IN IP4 ' + params.address;

    mediaSection.rtcp.addrInfo = 'IN IP4 ' + params.address;
    mediaSection.rtcp.port = params.port;

    // ICE parameters generated on relay side; same values for both a/v for bundled connection:
    mediaSection.attributes['ice-ufrag'] = params.iceUfrag;
    mediaSection.attributes['ice-pwd'] = params.icePwd;
  }
  
  //noinspection JSUnusedGlobalSymbols
  CA.PeerConnection.prototype._onLocalIceCandidate = function (e) {
    if (e.candidate) {
      var cString = JSON.stringify(e.candidate);
      log.debug("[PC] = Got local ICE candidate: " + cString);
      // a=candidate:3590110870 1 tcp 1509957375 192.168.1.33 51580 typ host generation 0
      //          0             1  2      3           4         5
      var components = e.candidate.candidate.split(' ');
      var protocol = components[2].toLowerCase();
      if (protocol !== 'udp') {
        log.debug('[PC] = Skipping non-UDP candidate');
        return;
      }
      var foundation = components[0].split(':')[1];
      var priority = components[3];
      var ipAddr = components[4];
      var port = components[5];
      
      var candidateId = JSON.stringify({ip: ipAddr, port: port});

      if (!this._candidatesMap) this._candidatesMap = {}
      if (!this._candidatesMap[candidateId]) {
        this._candidatesMap[candidateId] = 1;
        this.signalingTransport.sendIceCandidate(e.candidate.sdpMid, foundation, priority, ipAddr, port);
      } else {
        log.debug("[PC] = Skipped repeated ICE candidate");
      }
      
    } else {
      log.debug("[PC] = ICE candidates gathering finished");
      //var onOffer = function (sdp) {
      //  log.debug("[PC] = Offer after connection done:\n" + sdp.sdp);
      //};
      //var onFailed = function (inf) {
      //  log.error("[PC] = createOffer failed: " + inf);
      //};
      //this._nativePC.createOffer(onOffer, onFailed);
    }
  };

  CA.PeerConnection.prototype._onIceConnectionStateChange = function (e) {
    log.error("[PC] = New ICE state: " + e.target.iceConnectionState);
  };

  //noinspection JSUnusedGlobalSymbols
  CA.PeerConnection.prototype._onPeerConnectionConnecting = function (msg) {
    log.debug("[PC] = PeerConnection Session connecting " + JSON.stringify(msg));
  };

  //noinspection JSUnusedGlobalSymbols
  CA.PeerConnection.prototype._onPeerConnectionOpen = function (msg) {
    log.debug("[PC] = PeerConnection Session opened " + JSON.stringify(msg));
  };
  //noinspection JSUnusedGlobalSymbols
  CA.PeerConnection.prototype._onPeerConnectionStreamAdded = function (e) {
    log.debug("[PC] = PeerConnection Stream Added");
    var stream = e.stream;
    var renderer;
    if (this.connectionType == CA.PeerConnectionType.PC_TYPE_DOWNLINK) {
      log.debug("[PC] = Adding stream from remote peer connection");
      renderer = document.getElementById(this.renderingParams.rendererId);
      renderer.src = URL.createObjectURL(stream);
    }
  };
  //noinspection JSUnusedGlobalSymbols
  CA.PeerConnection.prototype._onPeerConnectionStreamRemoved = function (e) {
    log.debug("[PC] = PeerConnection Stream Removed: " + JSON.stringify(e));
  };

  CA.PeerConnection.prototype._onPeerConnectionStateChanged = function (e) {
    log.debug("[PC] = PeerConnection State Changed: " + JSON.stringify(e));
  };

  CA.PeerConnection.prototype._createProxy = function (method) {
    log.debug("[PC] = Creating proxy from this for method: " + method);
    return $.proxy(CA.PeerConnection.prototype[method], this);
  };


  /**
   * Represents complete client media description. Contains the streams
   * description (SDP) as well as complete list of available network endpoints
   * (obtained from ICE);
   *
   * @param {String} sdp
   * @param {CA.ClientEndpoint[]} endpoints
   * @constructor
   */
  CA.ClientDetails = function (sdp, endpoints) {
    this.sdp = sdp;
    this.endpoints = endpoints;
  };

  /**
   * Represents a client ICE endpoint.
   *
   * @param {IceCandidate} candidate
   * @constructor
   */
  CA.ClientEndpoint = function (candidate) {
    this.label = candidate.label;
    this.sdp = candidate.toSdp();
  };


  /**
   * Means to parse and create SDP
   */

  var a = 'a', c = 'c', m = 'm', o = 'o', s = 's', t = 't', v = 'v';

  function ManageableSDP(rtcSdp) {
    this.type = rtcSdp.type;
    this.sdp = rtcSdp.sdp;
    this.mediaSections = [];
    this.globalAttributes = {};
    var sdpLines = rtcSdp.sdp.split('\r\n'),
        sdpEntries = [];

    for (var i = 0; i < sdpLines.length; i++) {
      sdpEntries.push({key:sdpLines[i][0], value:sdpLines[i].substring(2)});
    }

    for (i = 0; i < sdpEntries.length; i++) {
      var key = sdpEntries[i].key,
          value = sdpEntries[i].value;
      switch (key) {
        case v:
          this.version = value;
          break;
        case o:
          this.originator = value;
          break;
        case s:
          this.sessionName = value;
          break;
        case t:
          this.time = value;
          break;
        case a:
          var pattrib = value.split(':');
          var pvalue = pattrib[1] || "";
          this.globalAttributes[pattrib[0]] = pvalue;
          break;
        case m:
          var mediaEntry = new SdpMediaSection(sdpEntries, i);
          // -1 here to suppress the i++ from the for loop stmnt
          i += mediaEntry.attributesCount - 1;
          this.mediaSections.push(mediaEntry);
          switch (mediaEntry.mediaType) {
            case 'audio':
              break;
            case 'video':
              break;
            default:
              log.warn("Got unsupported media type: " + mediaEntry.mediaType);
          }
          break;
        default:
          log.warn('Got unhandled SDP key type: ' + key);
      }
    }

  }

  function _genAddEntryFunctor(result) {
    return function (k, v) {
      result.sdp += k + '=' + v + '\r\n';
    };
  }

  ManageableSDP.prototype = {
    serialize:function () {
      return JSON.stringify({sdp:this.sdp, type:this.type});
    },
    flush:function () {
      var i, k;
      var result = {sdp:''};
      var addEntry = _genAddEntryFunctor(result);
      addEntry(v, 0);
      addEntry(o, this.originator);
      addEntry(s, this.sessionName);
      addEntry(t, this.time);
      
      for (k in this.globalAttributes) {
        if (Object.prototype.hasOwnProperty.call(this.globalAttributes, k)) {
          var attrVal = this.globalAttributes[k];
          if (attrVal !== "") {
            attrVal = ":" + attrVal;
          }
          addEntry(a, k + attrVal);
        }
      }
      
      for (i = 0; i < this.mediaSections.length; i++) {
        this.mediaSections[i].serialize(addEntry);
      }
      this.sdp = result.sdp;
    },
    toRtcSessionDescription:function () {
      return new RTCSessionDescription(this);
    }/* ,
    removeBundle:function () {
      for (var i = 0; i < this.globalAttributes.length; i++) {
        if (this.globalAttributes[i].indexOf('group:BUNDLE') == 0) {
          this.globalAttributes.splice(i, 1);
          break;
        }
      }
    } */
  };

  /**
   *
   * @param {String} input
   * @return {ManageableSDP}
   */
  ManageableSDP.fromString = function (input) {
    return new ManageableSDP(JSON.parse(input));
  };

  function SdpMediaSection(sdpEntries, startIdx) {
    var mLine = sdpEntries[startIdx],
        mLineItems = mLine.value.split(' ');

    this.attributes = {};
    this.codecsMap = {};
    this.codecs = [];
    this.ssrcLabels = [];
    this.rtcpfbLabels = [];
    this.iceCandidates = [];

    this.mediaType = mLineItems[0];
    this.port = mLineItems[1];
    this.profile = mLineItems[2];

    for (var i = 3; i < mLineItems.length; i++) {
      this.codecs.push(mLineItems[i]);
    }
    this.connInfo = sdpEntries[startIdx + 1].value;
    this.attributesCount = 2;
    for (i = startIdx + 2; i < sdpEntries.length; i++) {
      var key = sdpEntries[i].key, value = sdpEntries[i].value;
      if (key === m) {
        return;
      }
      this.attributesCount++;
      var colonPos = value.indexOf(':');
      if (colonPos < 0) {
        switch (value) {
          case 'rtcp-mux':
            this.rtcpMux = true;
            break;
          case 'send':
          case 'recv':
          case 'sendrecv':
          case 'recvonly':
          case 'sendonly':
          case 'inactive':
            this.direction = value;
            break;
        }
      } else {
        var pkey = value.substring(0, colonPos),
            pvalue = value.substring(colonPos + 1);

        switch (pkey) {
          case 'crypto':
            var cryptoItms = pvalue.split(' ');
            this.crypto = {
              tag: cryptoItms[0],
              hash: cryptoItms[1],
              key: cryptoItms[2].substring('inline:'.length)
            };
            break;
          case 'rtpmap':
            var codecItms = pvalue.split(' ');
            this.codecsMap[codecItms[0]] =
            {id:codecItms[0], label:codecItms[1], options:[]};
            break;
          case 'fmtp':
            var formatItms = pvalue.split(' ');
            this.codecsMap[formatItms[0]].options.push(formatItms[1]);
            break;
          case 'ssrc':
            var ssrcItms = pvalue.split(' ');
            this.ssrc = ssrcItms[0];
            this.ssrcLabels.push(pvalue.substr(pvalue.indexOf(' ') + 1));
            break;
          case 'rtcp-fb':
            var rtcpfbItms = pvalue.split(' ');
            this.rtcpfb = rtcpfbItms[0];
            this.rtcpfbLabels.push(pvalue.substr(pvalue.indexOf(' ') + 1));
            break;
          case 'rtcp':
            var spacePos = pvalue.indexOf(' ');
            this.rtcp = {
              port: pvalue.substring(0, spacePos),
              addrInfo: pvalue.substring(spacePos + 1)
            };
            break;
          default:
            this.attributes[pkey] = pvalue;
        }
      }
    }
  }

  SdpMediaSection.prototype = {

    serialize:function (addEntry) {
      var i, j, k,
          mLine = this.mediaType + ' ' + this.port + ' ' + this.profile + ' ';
      mLine += this.codecs.join(' ');
      addEntry(m, mLine);

      addEntry(c, this.connInfo);

      for (k in this.attributes) {
        if (Object.prototype.hasOwnProperty.call(this.attributes, k)) {
          addEntry(a, k + ':' + this.attributes[k]);
        }
      }
      if (this.direction && this.direction.length > 0) {
        addEntry(a, this.direction);
      }
      if (this.rtcp) {
        addEntry(a, 'rtcp:' + this.rtcp.port + ' ' + this.rtcp.addrInfo);
      }
      if (this.rtcpMux) {
        addEntry(a, 'rtcp-mux');
      }
      if (this.crypto) {
        addEntry(a, 'crypto:' + this.crypto.tag + ' ' + this.crypto.hash +
            ' inline:' + this.crypto.key);
      }
      for (i = 0; i < this.codecs.length; i++) {
        var codec = this.codecsMap[this.codecs[i]];
        addEntry(a, 'rtpmap:' + codec.id + ' ' + codec.label);
        for (j = 0; j < codec.options.length; j++) {
          addEntry(a, 'fmtp:' + codec.id + ' ' + codec.options[j]);
        }
      }
      for (i = 0; i < this.rtcpfbLabels.length; i++) {
        addEntry(a, 'rtcp-fb:' + this.rtcpfb + ' ' + this.rtcpfbLabels[i]);
      }
      for (i = 0; i < this.ssrcLabels.length; i++) {
        addEntry(a, 'ssrc:' + this.ssrc + ' ' + this.ssrcLabels[i]);
      }
      for (i = 0; i < this.iceCandidates.length; i++) {
        addEntry(a, this.iceCandidates[i]);
      }
    }

  };

})(window, window.jQuery);

