/**
 * Created with PyCharm.
 * User: ted
 * Date: 7/13/12
 * Time: 12:46 PM
 * To change this template use File | Settings | File Templates.
 */

(function (w, $) {
  'use strict';

  var CA = w.CA,
      PeerConnection = w.PeerConnection,
      RTCSessionDescription = w.RTCSessionDescription,
      RTCIceCandidate = w.RTCIceCandidate,
      URL = w.URL,
      log;


  CA.PeerConnection = function (localStream, rendererId) {
    log = w.log;
    log.debug("[PC] = Creating new PeerConnection");
    var pc_config = {"iceServers":[
      {"url":"stun:stun.l.google.com:19302"}
    ]};
    var pc_constraints = {"optional":[
      {"DtlsSrtpKeyAgreement":false}
    ]};

    this._nativePC = new PeerConnection(pc_config, pc_constraints);
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
    this.rendererId = rendererId;
    this._localEndpoints = [];
    this.localStream = localStream;


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
      'OfferToReceiveAudio': true,
      'OfferToReceiveVideo': true }};

    var self = this;
    this._nativePC.addStream(this.localStream);

    var onOffer = function (sdp) {
      self.state = CA.PeerConnection.ConnectionState.CONNECTING;
      log.debug("[PC] = Offer prepared");
      
      // save vanilla offer created by browser, without candidates info
      self._initialOfferSdp = sdp;
      
      var mgSdp = new ManageableSDP(sdp);
      
      // use from audio, as we assume bundled connection
      resultH(mgSdp.mediaSections[0].attributes['ice-ufrag'],
              mgSdp.mediaSections[0].attributes['ice-pwd']);
    };
    
    function onOfferFailed(inf) {
      log.error("createOffer failed: " + inf);
    }

    this._nativePC.createOffer(onOffer, onOfferFailed, sdpConstraints);
  };

  CA.PeerConnection.prototype.handleAuthResponse = function (params, resultHandler) {
      log.debug("[PC] = Handling auth response");
      var self = this;
      var sdp = self._initialOfferSdp;
      log.debug('Parsing and modifying initial candidateless offer: \r\n' + sdp.sdp);
      var mgSdp = new ManageableSDP(sdp);
      
      // set values in offer generated by streamer
      mgSdp.mediaSections[0].crypto.key = params.cryptoKey;
      mgSdp.mediaSections[1].crypto.key = params.cryptoKey;
      mgSdp.mediaSections[0].ssrc = params.audioSsrc;
      mgSdp.mediaSections[1].ssrc = params.videoSsrc;     
      mgSdp.flush();
      var offerSdp = mgSdp.toRtcSessionDescription();

      log.debug('Modified offer: ' + offerSdp.sdp);
      self._nativePC.setLocalDescription(offerSdp, onLocalDescriptorSet, function(msg){log.error(msg)});
      
      function onLocalDescriptorSet() {
        var mgAnswer = new ManageableSDP(offerSdp); //< reuse the offer
        mgAnswer.type = "answer";
        mgAnswer.originator = "addlive 20518 0 IN IP4 " + params.address;
        //mgAnswer.globalAttributes['msid-semantic'] = 'WMS';
        
        mgAnswer.mediaSections[0].port = params.port;
        mgAnswer.mediaSections[1].port = params.port;
               
        mgAnswer.mediaSections[0].direction = 'recvonly';
        mgAnswer.mediaSections[1].direction = 'recvonly';
        
        // todo: add to parser
        //mgAnswer.mediaSections[0].attributes['c'] = 'IN IP4 ' + params.address;
        //mgAnswer.mediaSections[1].attributes['c'] = 'IN IP4 ' + params.address;
        
        mgAnswer.mediaSections[0].ssrcLabels = [];
        mgAnswer.mediaSections[1].ssrcLabels = [];

        mgAnswer.mediaSections[0].rtcp.addrInfo = 'IN IP4 ' + params.address;
        mgAnswer.mediaSections[0].rtcp.port = params.port;
        mgAnswer.mediaSections[1].rtcp.addrInfo = 'IN IP4 ' + params.address;
        mgAnswer.mediaSections[1].rtcp.port = params.port;
        
        // common values between a/v for bundled connection
        mgAnswer.mediaSections[0].attributes['ice-ufrag'] = params.iceUfrag;
        mgAnswer.mediaSections[0].attributes['ice-pwd'] = params.icePwd;
        mgAnswer.mediaSections[1].attributes['ice-ufrag'] = params.iceUfrag;
        mgAnswer.mediaSections[1].attributes['ice-pwd'] = params.icePwd;
        
        mgAnswer.mediaSections[0].attributes['candidate'] = params.candidate;
        mgAnswer.mediaSections[1].attributes['candidate'] = params.candidate;
            
        // msid-semantic?
        mgAnswer.flush();
        var answerSdp = mgAnswer.toRtcSessionDescription();
        log.debug("[PC] = Answer created to feed setRemoteDescription: " + answerSdp.sdp);
      
        function onRemoteDescriptionSet() {
          log.debug('[PC] = Remote SDP set');
          // TODO: this._nativePC.addIceCandidate(new RTCIceCandidate({sdpMLineIndex:c.label, candidate:c.candidate}))?
        }
      
        self._nativePC.setRemoteDescription(answerSdp, onRemoteDescriptionSet, function(msg){log.error(msg)});
      }
    
  };

  CA.PeerConnection.prototype.close = function () {
    log.debug("[PC] = Closing a connection");
    this._nativePC.close();
    this.state = CA.PeerConnection.ConnectionState.NOT_CONNECTED;
  };

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
      // TODO: check who is 'this' here
      if (!this._candidatesMap) this._candidatesMap = {}
      if (!this._candidatesMap[candidateId]) {
        this._candidatesMap[candidateId] = 1;
        this.signalingTransport.sendIceCandidate(e.candidate.sdpMid, foundation, priority, ipAddr, port);
      } else {
        log.debug("[PC] = Skipped repeated ICE candidate");
      }
      
    } else {
      log.debug("[PC] = ICE candidates gathering finished");

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
    renderer = document.getElementById(this.rendererId);
    renderer.src = URL.createObjectURL(stream);
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

  var a = 'a', c = 'c', m = 'm', o = 'o', s = 's', t = 't', v = 'v';

  function ManageableSDP(rtcSdp) {
    this.type = rtcSdp.type;
    this.sdp = rtcSdp.sdp;
    this.mediaSections = [];
    this.globalAttributes = [];
    var sdpLines = rtcSdp.sdp.split('\r\n'),
        sdpEntries = [];

    for (var i = 0; i < sdpLines.length; i++) {
      sdpEntries.push({key:sdpLines[i][0], value:sdpLines[i].substring(2)});
    }
    this.globalAttributes = [];
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
          this.globalAttributes.push(value);
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
              log.w("Got unsupported media type: " + mediaEntry.mediaType);
          }
          break;
        default:
          log.w('Got unhandled SDP key type: ' + key);
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
      var result = {sdp:''};
      var addEntry = _genAddEntryFunctor(result);
      addEntry(v, 0);
      addEntry(o, this.originator);
      addEntry(s, this.sessionName);
      addEntry(t, this.time);
      for (var i = 0; i < this.globalAttributes.length; i++) {
        addEntry(a, this.globalAttributes[i]);
      }
      for (i = 0; i < this.mediaSections.length; i++) {
        this.mediaSections[i].serialize(addEntry);
      }
      this.sdp = result.sdp;
    },
    toRtcSessionDescription:function () {
      return new RTCSessionDescription(this);
    },
    removeBundle:function () {
      for (var i = 0; i < this.globalAttributes.length; i++) {
        if (this.globalAttributes[i].indexOf('group:BUNDLE') == 0) {
          this.globalAttributes.splice(i, 1);
          break;
        }
      }
    }
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

