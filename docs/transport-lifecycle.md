# Stock transport / session lifecycle (R06)

This documents existing lifecycle ownership without changing its behaviour.

* `CClient::Stop` ceases streaming and sends a connectionless disconnect.
  Delivery of that datagram is **not guaranteed**.
* `CServer::FreeChannel` removes an endpoint's channel from the active order.
* `CChannel::Disconnect` owns disconnect signalling; `CChannel::SetEnable`
  owns the socket-buffer/transport enable state. Reinitialization of
  `CNetBufWithStats` belongs to channel audio stream configuration.
* The stock protocol neither identifies a fresh epoch nor authorizes replacement
  of an active endpoint via a new feature handshake. That is deliberately **not**
  changed here: extension-specific session replacement belongs to F06.

No new timeout, retransmission, admission or callback behaviour is introduced.
