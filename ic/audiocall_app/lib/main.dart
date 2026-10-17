// IC AudioCall app
//
// Audio-only calls through the IC Janus servers (PC1 and PC2), using the
// custom janus.plugin.audiocall plugin over the Janus WebSocket API.
// No FreeSWITCH and no SIP: each user registers with a username on its own
// Janus server, and the two servers' plugins connect calls between them.
//
// Runs on Android (flutter run on a phone) and on Linux (flutter run -d linux),
// and interoperates with the browser test page (html/ic/audiocall-test.html).

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_webrtc/flutter_webrtc.dart';

// ignore_for_file: avoid_print

void main() {
  runApp(const AudioCallApp());
}

/// The Janus servers of the IC setup. Both run the AudioCall plugin.
class ServerPreset {
  const ServerPreset(this.name, this.host);

  final String name;
  final String host;
}

const List<ServerPreset> kServers = [
  ServerPreset('PC1', '192.168.97.135'),
  ServerPreset('PC2', '192.168.99.112'),
];
const String kJanusPort = '8188';
const String kPlugin = 'janus.plugin.audiocall';

/// Where the app is in a call.
enum CallPhase {
  idle, // no call
  calling, // we called someone, waiting for them to answer
  ringing, // someone is calling us, the Accept/Decline dialog is open
  connecting, // call answered, audio connection being set up
  inCall, // audio is flowing
}

class AudioCallApp extends StatelessWidget {
  const AudioCallApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'IC AudioCall',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(seedColor: Colors.indigo),
        useMaterial3: true,
      ),
      home: const AudioCallPage(),
    );
  }
}

class AudioCallPage extends StatefulWidget {
  const AudioCallPage({super.key});

  @override
  State<AudioCallPage> createState() => _AudioCallPageState();
}

class _AudioCallPageState extends State<AudioCallPage> {
  // ---------------------------------------------------------------------------
  // Settings
  // ---------------------------------------------------------------------------

  final TextEditingController _hostController =
      TextEditingController(text: kServers.first.host);
  final TextEditingController _portController =
      TextEditingController(text: kJanusPort);
  final TextEditingController _usernameController = TextEditingController();
  final TextEditingController _destinationController = TextEditingController();

  /// 'PC1', 'PC2' or 'Custom'.
  String _serverChoice = kServers.first.name;

  // ---------------------------------------------------------------------------
  // Registration and call state
  // ---------------------------------------------------------------------------

  String _status = 'Choose a server, enter your username and register.';
  bool _isRegistering = false;
  bool _isRegistered = false;
  String? _registeredAs;

  CallPhase _phase = CallPhase.idle;
  String _peerName = '';
  bool _peerIsRemote = false;

  DateTime? _callStartedAt;
  Timer? _callTimer;
  Duration _callDuration = Duration.zero;

  bool _isMuted = false;
  bool _isSpeakerOn = false;

  BuildContext? _incomingDialogContext;
  bool _incomingCancelledByCaller = false;

  // Online users, from the plugin's "list" request
  String? _thisServerName;
  String? _peerServerName;
  bool? _peerOnline; // null = peer link not configured
  List<String> _localUsers = [];
  List<String> _peerUsers = [];

  // ---------------------------------------------------------------------------
  // Janus WebSocket transport
  // ---------------------------------------------------------------------------

  int? _sessionId;
  int? _handleId;
  WebSocket? _socket;
  StreamSubscription<dynamic>? _socketSub;
  Timer? _keepAliveTimer;
  final Map<String, Completer<Map<String, dynamic>>> _pending = {};
  Completer<void>? _registrationCompleter;
  Future<void> _eventChain = Future<void>.value();
  int _transactionCounter = 0;

  // ---------------------------------------------------------------------------
  // WebRTC
  // ---------------------------------------------------------------------------

  MediaStream? _localStream;
  RTCPeerConnection? _peerConnection;

  bool get _isMobile => Platform.isAndroid || Platform.isIOS;

  String get _janusWsUrl =>
      'ws://${_hostController.text.trim()}:${_portController.text.trim()}';

  String get _platformName =>
      Platform.isAndroid ? 'Android' : (Platform.isLinux ? 'Linux' : Platform.operatingSystem);

  @override
  void dispose() {
    _callTimer?.cancel();
    _keepAliveTimer?.cancel();
    _hostController.dispose();
    _portController.dispose();
    _usernameController.dispose();
    _destinationController.dispose();
    unawaited(_disposeWebRtc());
    unawaited(_closeWebSocket());
    super.dispose();
  }

  void _setStatus(String message) {
    print('STATUS: $message');
    if (!mounted) return;
    setState(() => _status = message);
  }

  String _newTransaction() {
    _transactionCounter++;
    return '${DateTime.now().microsecondsSinceEpoch}-$_transactionCounter';
  }

  // ---------------------------------------------------------------------------
  // WebSocket connection
  // ---------------------------------------------------------------------------

  Future<void> _connectWebSocket() async {
    await _closeWebSocket();
    final url = _janusWsUrl;
    _setStatus('Connecting to $url ...');

    final socket = await WebSocket.connect(
      url,
      protocols: ['janus-protocol'],
    ).timeout(const Duration(seconds: 10));
    socket.pingInterval = const Duration(seconds: 20);
    _socket = socket;

    _socketSub = socket.listen(
      _onSocketData,
      onError: (Object error) => print('DEBUG: WebSocket error: $error'),
      onDone: () => _onSocketDone(socket),
      cancelOnError: false,
    );
  }

  Future<void> _closeWebSocket() async {
    _keepAliveTimer?.cancel();
    _keepAliveTimer = null;

    final subscription = _socketSub;
    final socket = _socket;
    _socketSub = null;
    _socket = null;

    for (final completer in _pending.values) {
      if (!completer.isCompleted) {
        completer.completeError(Exception('WebSocket closed'));
      }
    }
    _pending.clear();

    try {
      await subscription?.cancel();
    } catch (_) {}
    try {
      await socket?.close();
    } catch (_) {}
  }

  void _onSocketDone(WebSocket socket) {
    if (!identical(socket, _socket)) return;
    _handleConnectionLost(
      'Connection to Janus closed (code ${socket.closeCode}).',
    );
  }

  void _handleConnectionLost(String reason) {
    print('CONNECTION LOST: $reason');
    _keepAliveTimer?.cancel();
    _keepAliveTimer = null;
    _socketSub?.cancel();
    _socketSub = null;
    _socket = null;

    for (final completer in _pending.values) {
      if (!completer.isCompleted) completer.completeError(Exception(reason));
    }
    _pending.clear();

    final registration = _registrationCompleter;
    if (registration != null && !registration.isCompleted) {
      registration.completeError(Exception(reason));
    }

    _closeIncomingDialog();
    unawaited(_disposeWebRtc());
    _stopCallTimer();

    if (!mounted) return;
    setState(() {
      _isRegistered = false;
      _registeredAs = null;
      _sessionId = null;
      _handleId = null;
      _phase = CallPhase.idle;
      _status = '$reason Register again.';
    });
  }

  void _onSocketData(dynamic data) {
    if (data is! String) return;
    print('DEBUG: WS <- $data');

    Map<String, dynamic> message;
    try {
      message = jsonDecode(data) as Map<String, dynamic>;
    } catch (error) {
      print('DEBUG: Invalid JSON from Janus: $error');
      return;
    }

    final type = message['janus'] as String?;
    final transaction = message['transaction'] as String?;

    // Replies to our requests (success / error / ack) complete the waiting request
    if (transaction != null &&
        (type == 'success' || type == 'error' || type == 'ack')) {
      final pending = _pending.remove(transaction);
      if (pending != null && !pending.isCompleted) {
        pending.complete(message);
        return;
      }
    }

    // Everything else (plugin events, timeouts) is handled in order
    _eventChain = _eventChain
        .then((_) => _handleJanusMessage(message))
        .catchError((Object error, StackTrace stackTrace) {
      print('EVENT HANDLER ERROR: $error');
      print(stackTrace);
    });
  }

  /// Sends a request and waits for Janus's success / ack / error reply.
  Future<Map<String, dynamic>> _request(
    Map<String, dynamic> payload, {
    bool needSession = true,
    bool needHandle = false,
    Duration timeout = const Duration(seconds: 10),
  }) async {
    final socket = _socket;
    if (socket == null) throw Exception('Not connected to Janus');

    final transaction = _newTransaction();
    final message = <String, dynamic>{...payload, 'transaction': transaction};

    if (needSession) {
      final sessionId = _sessionId;
      if (sessionId == null) throw Exception('No Janus session');
      message['session_id'] = sessionId;
    }
    if (needHandle) {
      final handleId = _handleId;
      if (handleId == null) throw Exception('No AudioCall plugin handle');
      message['handle_id'] = handleId;
    }

    final completer = Completer<Map<String, dynamic>>();
    _pending[transaction] = completer;

    final body = message['body'];
    print('DEBUG: WS -> ${message['janus']} '
        '${body is Map ? body['request'] ?? '' : ''}');
    socket.add(jsonEncode(message));

    try {
      final response = await completer.future.timeout(timeout);
      if (response['janus'] == 'error') {
        final error = response['error'];
        final code = error is Map ? error['code'] : '';
        final reason = error is Map ? error['reason'] : error;
        throw Exception('Janus error $code: $reason');
      }
      return response;
    } on TimeoutException {
      _pending.remove(transaction);
      throw Exception('Janus did not answer "${payload['janus']}" '
          'within ${timeout.inSeconds}s');
    }
  }

  /// Sends a message without waiting for a reply (keepalive, trickle).
  void _sendNoWait(Map<String, dynamic> payload, {bool needHandle = false}) {
    final socket = _socket;
    final sessionId = _sessionId;
    if (socket == null || sessionId == null) return;

    final message = <String, dynamic>{
      ...payload,
      'transaction': _newTransaction(),
      'session_id': sessionId,
    };
    if (needHandle) {
      final handleId = _handleId;
      if (handleId == null) return;
      message['handle_id'] = handleId;
    }
    socket.add(jsonEncode(message));
  }

  /// Sends a request to the AudioCall plugin (optionally with an SDP).
  Future<void> _pluginRequest(
    Map<String, dynamic> body, {
    Map<String, dynamic>? jsep,
  }) async {
    await _request(
      {
        'janus': 'message',
        'body': body,
        if (jsep != null) 'jsep': jsep,
      },
      needHandle: true,
      timeout: const Duration(seconds: 15),
    );
  }

  void _startKeepAlive() {
    _keepAliveTimer?.cancel();
    _keepAliveTimer = Timer.periodic(const Duration(seconds: 25), (_) {
      _sendNoWait({'janus': 'keepalive'});
    });
  }

  // ---------------------------------------------------------------------------
  // Register / disconnect
  // ---------------------------------------------------------------------------

  Future<void> _register() async {
    if (_isRegistering) return;
    final username = _usernameController.text.trim();
    if (username.isEmpty) {
      _setStatus('Enter a username (for example 1000) first.');
      return;
    }

    setState(() {
      _isRegistering = true;
      _isRegistered = false;
      _registeredAs = null;
      _phase = CallPhase.idle;
      _status = 'Connecting...';
    });

    try {
      await _disposeWebRtc();
      await _closeWebSocket();
      _sessionId = null;
      _handleId = null;

      await _connectWebSocket();

      _setStatus('Creating Janus session...');
      final created = await _request({'janus': 'create'}, needSession: false);
      _sessionId = (created['data'] as Map)['id'] as int;
      _startKeepAlive();

      _setStatus('Attaching the AudioCall plugin...');
      final attached = await _request({'janus': 'attach', 'plugin': kPlugin});
      _handleId = (attached['data'] as Map)['id'] as int;

      final registration = Completer<void>();
      registration.future.ignore();
      _registrationCompleter = registration;

      _setStatus('Registering $username...');
      await _pluginRequest({'request': 'register', 'username': username});
      await registration.future.timeout(const Duration(seconds: 10));

      if (!mounted) return;
      setState(() {
        _isRegistered = true;
        _registeredAs = username;
        _status = 'Registered as $username on $_serverChoice. '
            'Ready to make or receive audio calls.';
      });
      unawaited(_refreshUsers());
    } on TimeoutException {
      _setStatus('Registration timed out.');
      await _closeWebSocket();
    } catch (error) {
      print('REGISTER ERROR: $error');
      _setStatus('Registration failed: $error');
      await _closeWebSocket();
    } finally {
      _registrationCompleter = null;
      if (mounted) setState(() => _isRegistering = false);
    }
  }

  Future<void> _disconnect() async {
    if (_phase != CallPhase.idle) await _hangup();
    // Destroying the session also removes our username from the plugin
    _sendNoWait({'janus': 'destroy'});
    await _closeWebSocket();
    if (!mounted) return;
    setState(() {
      _isRegistered = false;
      _registeredAs = null;
      _sessionId = null;
      _handleId = null;
      _localUsers = [];
      _peerUsers = [];
      _peerOnline = null;
      _status = 'Disconnected.';
    });
  }

  Future<void> _refreshUsers() async {
    if (!_isRegistered) return;
    try {
      await _pluginRequest({'request': 'list'});
    } catch (error) {
      print('LIST ERROR: $error');
    }
  }

  // ---------------------------------------------------------------------------
  // Plugin events
  // ---------------------------------------------------------------------------

  Future<void> _handleJanusMessage(Map<String, dynamic> message) async {
    final type = message['janus'] as String?;

    if (type == 'timeout') {
      unawaited(_closeWebSocket());
      _handleConnectionLost('Janus session timed out.');
      return;
    }
    if (type == 'hangup') {
      // Janus says the PeerConnection is gone; the plugin event follows
      print('DEBUG: Janus PeerConnection hangup: ${message['reason']}');
      return;
    }
    if (type != 'event') return;

    final pluginData = message['plugindata'];
    if (pluginData is! Map) return;
    final data = pluginData['data'];
    if (data is! Map) return;

    final jsepRaw = message['jsep'];
    final jsep = jsepRaw is Map ? Map<String, dynamic>.from(jsepRaw) : null;

    // Plugin errors, e.g. 476 username taken, 478 no such user, 484 audio only
    if (data['error'] != null) {
      final text = 'Error ${data['error_code'] ?? ''}: ${data['error']}';
      print('PLUGIN ERROR: $text');
      final registration = _registrationCompleter;
      if (registration != null && !registration.isCompleted) {
        registration.completeError(Exception(text));
        return;
      }
      if (_phase != CallPhase.idle) {
        await _endCall(text);
      } else {
        _setStatus(text);
      }
      return;
    }

    final result = data['result'];
    if (result is! Map) return;
    final r = Map<String, dynamic>.from(result);

    // Reply to "list"
    if (r['list'] is List) {
      _updateUserLists(r);
      return;
    }

    final event = r['event'] as String?;
    print('DEBUG: AudioCall event: $event');

    switch (event) {
      case 'registered':
        final registration = _registrationCompleter;
        if (registration != null && !registration.isCompleted) {
          registration.complete();
        }
        return;

      case 'calling':
        if (_phase == CallPhase.calling) {
          _peerIsRemote = r['remote'] == true;
          _setStatus('Ringing $_peerName'
              '${_peerIsRemote ? ' on the other server' : ''}...');
        }
        return;

      case 'incomingcall':
        final caller = (r['username'] ?? 'unknown').toString();
        final remote = r['remote'] == true;
        unawaited(_handleIncomingCall(caller, remote, jsep));
        return;

      case 'accepted':
        // Caller side: carries the callee's answer. Callee side: no SDP.
        if (jsep != null && _phase == CallPhase.calling) {
          await _applyRemoteAnswer(jsep);
        }
        if (_phase == CallPhase.calling) {
          if (mounted) setState(() => _phase = CallPhase.connecting);
          _setStatus('$_peerName answered. Connecting audio...');
        }
        return;

      case 'hangup':
        await _handleHangupEvent(r);
        return;

      default:
        return;
    }
  }

  void _updateUserLists(Map<String, dynamic> r) {
    final me = _registeredAs;
    List<String> toList(dynamic value) => value is List
        ? value.map((e) => e.toString()).where((u) => u != me).toList()
        : <String>[];
    if (!mounted) return;
    setState(() {
      _thisServerName = r['server']?.toString();
      _localUsers = toList(r['list']);
      _peerOnline = r['peer_online'] is bool ? r['peer_online'] as bool : null;
      _peerServerName = r['peer_server']?.toString();
      _peerUsers = toList(r['peer_list']);
    });
  }

  Future<void> _handleHangupEvent(Map<String, dynamic> r) async {
    final reason = (r['reason'] ?? 'unknown').toString();

    // The caller gave up while our Accept/Decline dialog was open
    if (_phase == CallPhase.ringing) {
      _incomingCancelledByCaller = true;
      _closeIncomingDialog();
      return;
    }
    // Ignore the plugin's confirmation of a hangup we already handled
    if (_phase == CallPhase.idle) return;

    await _endCall('Call with $_peerName ended ($reason).');
  }

  // ---------------------------------------------------------------------------
  // WebRTC
  // ---------------------------------------------------------------------------

  Future<RTCPeerConnection> _createMediaAndPeerConnection() async {
    await _disposeWebRtc();

    final stream = await navigator.mediaDevices.getUserMedia({
      'audio': {
        'echoCancellation': true,
        'noiseSuppression': true,
        'autoGainControl': true,
      },
      'video': false,
    });
    if (stream.getAudioTracks().isEmpty) {
      throw Exception('No microphone found.');
    }
    _localStream = stream;
    _applyMute();

    // LAN only: host candidates are enough, no STUN/TURN server needed
    final pc = await createPeerConnection({
      'iceServers': <Map<String, dynamic>>[],
      'sdpSemantics': 'unified-plan',
    });
    _peerConnection = pc;

    pc.onIceCandidate = (candidate) {
      if (candidate.candidate == null) return;
      _sendNoWait({
        'janus': 'trickle',
        'candidate': {
          'candidate': candidate.candidate,
          'sdpMid': candidate.sdpMid,
          'sdpMLineIndex': candidate.sdpMLineIndex,
        },
      }, needHandle: true);
    };

    pc.onIceGatheringState = (state) {
      if (state == RTCIceGatheringState.RTCIceGatheringStateComplete) {
        _sendNoWait({
          'janus': 'trickle',
          'candidate': {'completed': true},
        }, needHandle: true);
      }
    };

    pc.onConnectionState = (state) {
      print('DEBUG: PeerConnection state: $state');
      if (state == RTCPeerConnectionState.RTCPeerConnectionStateConnected) {
        if (_phase == CallPhase.connecting || _phase == CallPhase.calling) {
          if (mounted) setState(() => _phase = CallPhase.inCall);
          _startCallTimer();
          _setStatus('In call with $_peerName'
              '${_peerIsRemote ? ' (other server)' : ''}.');
          unawaited(_applySpeaker());
        }
      } else if (state ==
          RTCPeerConnectionState.RTCPeerConnectionStateFailed) {
        unawaited(_hangup(reason: 'Audio connection failed'));
        _setStatus('Call failed: the audio connection could not be set up.');
      }
    };

    for (final track in stream.getAudioTracks()) {
      await pc.addTrack(track, stream);
    }
    await _applySpeaker();
    return pc;
  }

  Future<void> _applyRemoteAnswer(Map<String, dynamic> jsep) async {
    final pc = _peerConnection;
    final sdp = jsep['sdp'] as String?;
    final type = jsep['type'] as String?;
    if (pc == null || sdp == null || type == null) return;
    await pc.setRemoteDescription(RTCSessionDescription(sdp, type));
  }

  Future<void> _disposeWebRtc() async {
    final stream = _localStream;
    final pc = _peerConnection;
    _localStream = null;
    _peerConnection = null;
    try {
      if (stream != null) {
        for (final track in stream.getTracks()) {
          await track.stop();
        }
        await stream.dispose();
      }
    } catch (error) {
      print('Stream cleanup warning: $error');
    }
    try {
      await pc?.close();
    } catch (error) {
      print('PeerConnection cleanup warning: $error');
    }
  }

  // ---------------------------------------------------------------------------
  // In-call controls
  // ---------------------------------------------------------------------------

  void _applyMute() {
    for (final track
        in _localStream?.getAudioTracks() ?? const <MediaStreamTrack>[]) {
      track.enabled = !_isMuted;
    }
  }

  Future<void> _applySpeaker() async {
    if (!_isMobile) return; // the desktop always plays through its speakers
    try {
      await Helper.setSpeakerphoneOn(_isSpeakerOn);
    } catch (error) {
      print('Speakerphone warning: $error');
    }
  }

  void _toggleMute() {
    setState(() => _isMuted = !_isMuted);
    _applyMute();
  }

  Future<void> _toggleSpeaker() async {
    setState(() => _isSpeakerOn = !_isSpeakerOn);
    await _applySpeaker();
  }

  void _startCallTimer() {
    _callTimer?.cancel();
    _callStartedAt = DateTime.now();
    _callDuration = Duration.zero;
    _callTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      final start = _callStartedAt;
      if (start == null || !mounted) return;
      setState(() => _callDuration = DateTime.now().difference(start));
    });
  }

  void _stopCallTimer() {
    _callTimer?.cancel();
    _callTimer = null;
    _callStartedAt = null;
  }

  // ---------------------------------------------------------------------------
  // Outgoing call
  // ---------------------------------------------------------------------------

  Future<void> _startCall() async {
    final destination = _destinationController.text.trim();
    if (!_isRegistered) {
      _setStatus('Register first.');
      return;
    }
    if (destination.isEmpty) {
      _setStatus('Enter the username to call.');
      return;
    }
    if (destination == _registeredAs) {
      _setStatus("You can't call yourself.");
      return;
    }
    if (_phase != CallPhase.idle) return;

    setState(() {
      _phase = CallPhase.calling;
      _peerName = destination;
      _peerIsRemote = _peerUsers.contains(destination);
      _isMuted = false;
      _isSpeakerOn = false;
      _status = 'Preparing the microphone...';
    });

    try {
      final pc = await _createMediaAndPeerConnection();
      final offer = await pc.createOffer({
        'offerToReceiveAudio': 1,
        'offerToReceiveVideo': 0,
      });
      await pc.setLocalDescription(offer);

      _setStatus('Calling $destination...');
      await _pluginRequest(
        {'request': 'call', 'username': destination},
        jsep: {'type': offer.type, 'sdp': offer.sdp},
      );
    } catch (error) {
      print('CALL ERROR: $error');
      await _endCall('Call failed: $error');
    }
  }

  // ---------------------------------------------------------------------------
  // Incoming call
  // ---------------------------------------------------------------------------

  Future<void> _handleIncomingCall(
    String caller,
    bool remote,
    Map<String, dynamic>? jsep,
  ) async {
    if (_phase != CallPhase.idle) {
      // Should not happen (the plugin answers "User busy" for us), but be safe
      await _sendHangup('Busy');
      return;
    }
    if (jsep == null) {
      _setStatus('Incoming call from $caller had no SDP offer.');
      await _sendHangup('No SDP');
      return;
    }

    setState(() {
      _phase = CallPhase.ringing;
      _peerName = caller;
      _peerIsRemote = remote;
      _isMuted = false;
      _isSpeakerOn = false;
      _status = 'Incoming call from $caller'
          '${remote ? ' (other server)' : ''}';
    });
    _incomingCancelledByCaller = false;

    final accept = await _showIncomingCallDialog(caller, remote);
    _incomingDialogContext = null;

    if (_incomingCancelledByCaller) {
      _incomingCancelledByCaller = false;
      if (mounted) setState(() => _phase = CallPhase.idle);
      _setStatus('Missed call from $caller (caller hung up).');
      return;
    }
    if (_phase != CallPhase.ringing) return; // connection lost meanwhile

    if (!accept) {
      if (mounted) setState(() => _phase = CallPhase.idle);
      await _sendHangup('Declined');
      _setStatus('Declined the call from $caller.');
      return;
    }

    try {
      setState(() {
        _phase = CallPhase.connecting;
        _status = 'Answering $caller...';
      });
      final pc = await _createMediaAndPeerConnection();
      await pc.setRemoteDescription(
        RTCSessionDescription(jsep['sdp'] as String?, jsep['type'] as String?),
      );
      final answer = await pc.createAnswer({
        'offerToReceiveAudio': 1,
        'offerToReceiveVideo': 0,
      });
      await pc.setLocalDescription(answer);
      await _pluginRequest(
        {'request': 'accept'},
        jsep: {'type': answer.type, 'sdp': answer.sdp},
      );
      _setStatus('Answered. Connecting audio with $caller...');
    } catch (error) {
      print('ACCEPT ERROR: $error');
      await _sendHangup('Answer failed');
      await _endCall('Could not answer: $error');
    }
  }

  Future<bool> _showIncomingCallDialog(String caller, bool remote) async {
    if (!mounted) return false;
    final accepted = await showDialog<bool>(
      context: context,
      barrierDismissible: false,
      builder: (dialogContext) {
        _incomingDialogContext = dialogContext;
        return AlertDialog(
          icon: const Icon(Icons.call, size: 36),
          title: Text('Incoming call from $caller'),
          content: Text(remote
              ? 'This user is registered on the other Janus server.'
              : 'This user is registered on this Janus server.'),
          actions: [
            TextButton.icon(
              onPressed: () => Navigator.of(dialogContext).pop(false),
              icon: const Icon(Icons.call_end, color: Colors.red),
              label: const Text('Decline'),
            ),
            FilledButton.icon(
              onPressed: () => Navigator.of(dialogContext).pop(true),
              icon: const Icon(Icons.call),
              label: const Text('Accept'),
            ),
          ],
        );
      },
    );
    return accepted ?? false;
  }

  void _closeIncomingDialog() {
    final dialogContext = _incomingDialogContext;
    _incomingDialogContext = null;
    if (dialogContext != null && dialogContext.mounted) {
      Navigator.of(dialogContext).pop(false);
    }
  }

  // ---------------------------------------------------------------------------
  // Hang up
  // ---------------------------------------------------------------------------

  /// Tells the plugin to end (or refuse) the current call.
  Future<void> _sendHangup(String reason) async {
    if (_socket == null || _handleId == null) return;
    try {
      await _pluginRequest({'request': 'hangup', 'reason': reason});
    } catch (error) {
      print('HANGUP ERROR: $error');
    }
  }

  /// Local hang-up button.
  Future<void> _hangup({String reason = 'Hung up'}) async {
    if (_phase == CallPhase.idle) return;
    final peer = _peerName;
    await _endCall('Call with $peer ended.');
    await _sendHangup(reason);
  }

  /// Back to idle: release microphone and PeerConnection.
  Future<void> _endCall(String status) async {
    _stopCallTimer();
    if (mounted) {
      setState(() {
        _phase = CallPhase.idle;
        _isMuted = false;
        _isSpeakerOn = false;
        _status = status;
      });
    }
    await _disposeWebRtc();
    unawaited(_refreshUsers());
  }

  // ---------------------------------------------------------------------------
  // UI
  // ---------------------------------------------------------------------------

  void _selectServer(String choice) {
    setState(() {
      _serverChoice = choice;
      for (final server in kServers) {
        if (server.name == choice) _hostController.text = server.host;
      }
    });
  }

  String _formatDuration(Duration d) {
    final minutes = d.inMinutes.remainder(60).toString().padLeft(2, '0');
    final seconds = d.inSeconds.remainder(60).toString().padLeft(2, '0');
    return d.inHours > 0 ? '${d.inHours}:$minutes:$seconds' : '$minutes:$seconds';
  }

  String get _phaseText {
    switch (_phase) {
      case CallPhase.idle:
        return 'Idle';
      case CallPhase.calling:
        return 'Calling...';
      case CallPhase.ringing:
        return 'Incoming call';
      case CallPhase.connecting:
        return 'Connecting audio...';
      case CallPhase.inCall:
        return 'In call  ${_formatDuration(_callDuration)}';
    }
  }

  Widget _input(
    TextEditingController controller,
    String label, {
    TextInputType? keyboardType,
    bool enabled = true,
  }) {
    return TextField(
      controller: controller,
      enabled: enabled,
      keyboardType: keyboardType,
      decoration: InputDecoration(
        labelText: label,
        border: const OutlineInputBorder(),
        isDense: true,
      ),
    );
  }

  Widget _buildServerCard() {
    final locked = _isRegistered || _isRegistering;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text('Janus server', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),
            Wrap(
              spacing: 8,
              children: [
                for (final name in [...kServers.map((s) => s.name), 'Custom'])
                  ChoiceChip(
                    label: Text(name),
                    selected: _serverChoice == name,
                    onSelected: locked ? null : (_) => _selectServer(name),
                  ),
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                Expanded(
                  flex: 3,
                  child: _input(
                    _hostController,
                    'Janus IP',
                    keyboardType: TextInputType.url,
                    enabled: !locked && _serverChoice == 'Custom',
                  ),
                ),
                const SizedBox(width: 8),
                Expanded(
                  flex: 2,
                  child: _input(
                    _portController,
                    'WS port',
                    keyboardType: TextInputType.number,
                    enabled: !locked,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            _input(
              _usernameController,
              'My username (e.g. 1000)',
              enabled: !locked,
            ),
            const SizedBox(height: 12),
            if (!_isRegistered)
              FilledButton.icon(
                onPressed: _isRegistering ? null : _register,
                icon: _isRegistering
                    ? const SizedBox(
                        width: 18,
                        height: 18,
                        child: CircularProgressIndicator(strokeWidth: 2),
                      )
                    : const Icon(Icons.login),
                label: const Text('Register'),
              )
            else
              OutlinedButton.icon(
                onPressed: _phase == CallPhase.idle ? _disconnect : null,
                icon: const Icon(Icons.logout),
                label: Text('Disconnect ($_registeredAs)'),
              ),
          ],
        ),
      ),
    );
  }

  Widget _buildUserChips(List<String> users, bool remote) {
    if (users.isEmpty) {
      return const Text('nobody else', style: TextStyle(color: Colors.grey));
    }
    return Wrap(
      spacing: 8,
      runSpacing: 4,
      children: [
        for (final user in users)
          ActionChip(
            avatar: Icon(remote ? Icons.public : Icons.computer, size: 16),
            label: Text(user),
            onPressed: _phase == CallPhase.idle
                ? () => setState(() => _destinationController.text = user)
                : null,
          ),
      ],
    );
  }

  Widget _buildDialCard() {
    final idle = _phase == CallPhase.idle;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Row(
              children: [
                Expanded(
                  child: Text('Online users',
                      style: Theme.of(context).textTheme.titleMedium),
                ),
                IconButton(
                  tooltip: 'Refresh',
                  onPressed: _refreshUsers,
                  icon: const Icon(Icons.refresh),
                ),
              ],
            ),
            Text('This server (${_thisServerName ?? _serverChoice}):'),
            const SizedBox(height: 4),
            _buildUserChips(_localUsers, false),
            const SizedBox(height: 8),
            if (_peerOnline == null)
              const Text('Other server: peer link not configured',
                  style: TextStyle(color: Colors.grey))
            else if (_peerOnline == false)
              const Text('Other server: not reachable',
                  style: TextStyle(color: Colors.red))
            else ...[
              Text('Other server (${_peerServerName ?? '?'}):'),
              const SizedBox(height: 4),
              _buildUserChips(_peerUsers, true),
            ],
            const Divider(height: 24),
            _input(
              _destinationController,
              'Username to call',
              enabled: idle,
            ),
            const SizedBox(height: 12),
            FilledButton.icon(
              onPressed: idle ? _startCall : null,
              icon: const Icon(Icons.call),
              label: const Text('Audio call'),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildCallCard() {
    final scheme = Theme.of(context).colorScheme;
    return Card(
      color: scheme.primaryContainer,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            const Icon(Icons.person, size: 56),
            Text(
              _peerName,
              style: Theme.of(context).textTheme.headlineSmall,
            ),
            if (_peerIsRemote)
              const Text('on the other server', style: TextStyle(fontSize: 12)),
            const SizedBox(height: 4),
            Text(_phaseText, style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 16),
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceEvenly,
              children: [
                _roundButton(
                  icon: _isMuted ? Icons.mic_off : Icons.mic,
                  label: _isMuted ? 'Unmute' : 'Mute',
                  onPressed: _toggleMute,
                  active: _isMuted,
                ),
                _roundButton(
                  icon: _isSpeakerOn ? Icons.volume_up : Icons.hearing,
                  label: _isSpeakerOn ? 'Speaker' : 'Earpiece',
                  onPressed: _isMobile ? _toggleSpeaker : null,
                  active: _isSpeakerOn,
                ),
                _roundButton(
                  icon: Icons.call_end,
                  label: 'Hang up',
                  onPressed: () => _hangup(),
                  color: Colors.red,
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _roundButton({
    required IconData icon,
    required String label,
    required VoidCallback? onPressed,
    bool active = false,
    Color? color,
  }) {
    final scheme = Theme.of(context).colorScheme;
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        IconButton.filled(
          onPressed: onPressed,
          iconSize: 28,
          style: IconButton.styleFrom(
            backgroundColor: color ?? (active ? scheme.primary : scheme.surface),
            foregroundColor:
                (color != null || active) ? Colors.white : scheme.onSurface,
          ),
          icon: Icon(icon),
        ),
        const SizedBox(height: 4),
        Text(label, style: const TextStyle(fontSize: 12)),
      ],
    );
  }

  Widget _buildStatusCard() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Status', style: Theme.of(context).textTheme.titleMedium),
            const SizedBox(height: 8),
            SelectableText(_status),
            const Divider(height: 24),
            SelectableText(
              'Platform: $_platformName\n'
              'Janus: $_janusWsUrl (${_socket != null ? 'connected' : 'disconnected'})\n'
              'Session: ${_sessionId ?? '-'}   Handle: ${_handleId ?? '-'}\n'
              'Registered: ${_registeredAs ?? 'no'}   Call: ${_phase.name}',
              style: const TextStyle(fontSize: 12),
            ),
          ],
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final bottomPadding = MediaQuery.of(context).viewPadding.bottom;
    final showCall = _phase == CallPhase.calling ||
        _phase == CallPhase.connecting ||
        _phase == CallPhase.inCall;

    return Scaffold(
      appBar: AppBar(
        title: const Text('IC AudioCall'),
        actions: [
          if (_isRegistered)
            Padding(
              padding: const EdgeInsets.only(right: 16),
              child: Center(child: Text('$_registeredAs @ $_serverChoice')),
            ),
        ],
      ),
      body: SafeArea(
        child: Center(
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 560),
            child: ListView(
              padding: EdgeInsets.fromLTRB(16, 16, 16, 24 + bottomPadding),
              children: [
                if (showCall) ...[
                  _buildCallCard(),
                  const SizedBox(height: 12),
                ],
                _buildServerCard(),
                if (_isRegistered) ...[
                  const SizedBox(height: 12),
                  _buildDialCard(),
                ],
                const SizedBox(height: 12),
                _buildStatusCard(),
              ],
            ),
          ),
        ),
      ),
    );
  }
}
