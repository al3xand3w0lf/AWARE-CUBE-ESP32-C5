// tcp_stream.h — Plain-TCP UBX-Realtime-Stream fuer ROLE_IOT_LOGGER_TCP.
// Ziel: host:port aus WifiProv::uploadUrl() (Format "host:port").
// Kein Handshake, kein Framing — reine TCP-Bytes.

#ifndef TCP_STREAM_H
#define TCP_STREAM_H

#include <Arduino.h>

namespace TcpStream {
  bool begin();
  void task(void* arg);
  bool isStreaming();
  uint32_t bytesSent();
}

#endif // TCP_STREAM_H
