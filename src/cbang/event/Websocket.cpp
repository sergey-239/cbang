/******************************************************************************\

          This file is part of the C! library.  A.K.A the cbang library.

                Copyright (c) 2003-2019, Cauldron Development LLC
                   Copyright (c) 2003-2017, Stanford University
                               All rights reserved.

         The C! library is free software: you can redistribute it and/or
        modify it under the terms of the GNU Lesser General Public License
       as published by the Free Software Foundation, either version 2.1 of
               the License, or (at your option) any later version.

        The C! library is distributed in the hope that it will be useful,
          but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
                 Lesser General Public License for more details.

         You should have received a copy of the GNU Lesser General Public
                 License along with the C! library.  If not, see
                         <http://www.gnu.org/licenses/>.

        In addition, BSD licensing may be granted on a case by case basis
        by written permission from at least one of the copyright holders.
           You may request written permission by emailing the authors.

                  For information regarding this software email:
                                 Joseph Coffland
                          joseph@cauldrondevelopment.com

\******************************************************************************/

#include "Websocket.h"
#include "HTTPConn.h"

#include <cbang/Catch.h>
#include <cbang/net/Swab.h>
#include <cbang/util/Random.h>

#ifdef HAVE_OPENSSL
#include <cbang/openssl/Digest.h>
#endif

#include <cstring> // memcpy()

using namespace std;
using namespace cb;
using namespace cb::Event;


bool Websocket::isActive() const {
  return active && hasConnection() && getConnection()->isConnected();
}


void Websocket::send(const char *data, unsigned length) {
  if (!active) return Request::send(data, length);

  const unsigned frameSize = 0xffff;

  for (unsigned i = 0; length; i += frameSize) {
    unsigned bytes = frameSize < length ? frameSize : length;
    length -= bytes;
    writeFrame(i ? WS_OP_CONTINUE : WS_OP_TEXT, !length, data + i, bytes);
  }

  msgSent++;
}


void Websocket::send(const string &s) {send(CBANG_CPP_TO_C_STR(s), s.length());}


void Websocket::close(WebsockStatus status, const std::string &msg) {
  LOG_DEBUG(4, CBANG_FUNC << '(' << status << ", " << msg << ')');

  pingEvent.release();
  pongEvent.release();

  if (!isActive()) return; // Already closed

  uint16_t data = hton16(status);
  writeFrame(WS_OP_CLOSE, true, &data, 2);

  active = false;
  TRY_CATCH_ERROR(onClose(status, msg));
}


void Websocket::ping(const string &payload) {
  writeFrame(WS_OP_PING, true, payload.data(), payload.size());
}


bool Websocket::upgrade() {
  // Check if websocket upgrade is allowed
  bool upgrade = getInputHeaders().keyContains("Connection", "upgrade");
  string key = inFind("Sec-WebSocket-Key");

  if (!upgrade || key.empty() || getVersion() < Version(1, 1)) return false;

  try {
    // Callback
    if (!onUpgrade()) return false;

    // Send handshake
    key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
#ifdef HAVE_OPENSSL
    key = Digest::base64(key, "sha1");
#else
    THROW("Cannot open Websocket, C! not built with openssl support");
#endif

    setVersion(Version(1, 1));
    outSet("Upgrade", "websocket");
    outSet("Connection", "upgrade");
    outSet("Sec-WebSocket-Accept", key);

    active = true;
    reply(HTTP_SWITCHING_PROTOCOLS);

    // Start connection heartbeat
    schedulePing();

    return true;
  } CATCH_ERROR;

  return false;
}


void Websocket::readHeader() {
  auto cb =
    [this] (bool success) {
      if (!success) return close(WS_STATUS_PROTOCOL);

      uint8_t header[2];
      input.copy((char *)header, 2);

      // Client must set mask bit
      bool mask = header[1] & 0x80;
      if (!mask) return close(WS_STATUS_PROTOCOL);

      // Compute header size
      uint8_t bytes = 6;
      uint8_t size = header[1] & 0x7f;
      if (size == 126) bytes = 8;
      if (size == 127) bytes = 14;

      auto cb =
        [this, bytes, size] (bool success) {
          if (!success) return close(WS_STATUS_PROTOCOL);

          uint8_t header[14];
          input.remove((char *)header, bytes);

          // Compute frame size
          if (size == 126) bytesToRead = hton16(*(uint16_t *)&header[2]);
          else if (size == 127) bytesToRead = hton64(*(uint64_t *)&header[2]);
          else bytesToRead = size;
          if (bytesToRead & (1ULL << 63))
            return close(WS_STATUS_PROTOCOL);

          // Check opcode
          uint8_t opcode = header[0] & 0xf;
          wsOpCode = (WebsockOpCode::enum_t)opcode;

          if (wsOpCode != WS_OP_CONTINUE) wsMsg.clear();

          switch (wsOpCode) {
          case WS_OP_TEXT:
          case WS_OP_BINARY:
          case WS_OP_CONTINUE: {
            // Check total message size
            auto maxBodySize = getConnection()->getMaxBodySize();
            if (maxBodySize && maxBodySize < wsMsg.size() + bytesToRead)
              return close(WS_STATUS_TOO_BIG);

            // Copy mask
            memcpy(wsMask, &header[bytes - 4], 4);

            // Last part of message?
            wsFinish = 0x80 & header[0];
            break;
          }

          case WS_OP_CLOSE:
          case WS_OP_PING:
          case WS_OP_PONG:
            break; // Control codes

          default: return close(WS_STATUS_PROTOCOL);
          }

          if (bytesToRead) readBody();
          else readHeader();
        };

      // Read rest of header
      getConnection()->read(cb, input, bytes);
    };

  // Read first part of header
  getConnection()->read(cb, input, 2);
}


void Websocket::readBody() {
  LOG_DEBUG(4, CBANG_FUNC << "() bytesToRead=" << bytesToRead);

  auto cb = [this] (bool success) {
    if (!success) return close(WS_STATUS_PROTOCOL);

    uint64_t offset = wsMsg.size();
    wsMsg.resize(offset + bytesToRead);
    input.remove(&wsMsg[offset], bytesToRead);

    // Demask
    for (uint64_t i = 0; i < bytesToRead; i++)
      wsMsg[offset +  i] ^= wsMask[i & 3];

    LOG_DEBUG(5, "Frame body\n"
              << String::hexdump(string(wsMsg.begin() + offset, wsMsg.end()))
              << '\n');

    switch (wsOpCode) {
    case WS_OP_CONTINUE:
    case WS_OP_TEXT:
    case WS_OP_BINARY:
      if (wsFinish) {
        message(wsMsg.data(), wsMsg.size());
        wsMsg.clear();
      }
      break;

    case WS_OP_CLOSE: {
      // Get close status
      WebsockStatus status = WS_STATUS_NONE;
      if (1 < bytesToRead)
        status = (WebsockStatus::enum_t)hton16(*(uint16_t *)wsMsg.data());

      // Send close response and close payload if any
      string payload;
      if (2 < wsMsg.size()) payload = string(wsMsg.begin() + 2, wsMsg.end());
      return close(status, payload);
    }

    case WS_OP_PING:
      // Schedule pong to aggregate backlogged pings
      pongPayload = string(wsMsg.begin(), wsMsg.end());
      schedulePong();
      schedulePing();
      break;

    case WS_OP_PONG: schedulePing(); break;

    default: return close(WS_STATUS_PROTOCOL);
    }

    // Read next message
    readHeader();
  };

  getConnection()->read(cb, input, bytesToRead);
}


void Websocket::onMessage(const char *data, uint64_t length) {
  if (cb) cb(data, length);
}



void Websocket::writeFrame(WebsockOpCode opcode, bool finish,
                           const void *data, uint64_t len) {
  LOG_DEBUG(4, CBANG_FUNC << '(' << opcode << ", " << finish << ", " << len
            << ')');

  if (!isActive()) THROW("Not active");

  uint8_t header[14];
  uint8_t bytes = 2;

  // Opcode
  header[0] = (finish ? 0x80 : 0) | opcode;

  // Format payload length
  if (len < 126) header[1] = len;

  else if (len <= 0xffff) {
    header[1] = 126;
    *(uint16_t *)&header[2] = hton16(len);
    bytes = 4;

  } else {
    header[1] = 127;
    *(uint64_t *)&header[2] = hton64(len);
    bytes = 10;
  }

  // Create mask
  bool maskData = !getConnection()->isIncoming();
  if (maskData) {
    header[1] &= 1 << 7; // Set mask bit

    // Generate random mask
    Random::instance().bytes(header + bytes, 4);
    bytes += 4;
  }

  Buffer out;
  out.expand(len + bytes);
  out.add((char *)header, bytes);
  out.add((char *)data, len);

  // Mask data
  if (maskData) {
    uint8_t *ptr = (uint8_t *)out.pullup(len + bytes) + bytes;
    uint8_t *mask = &header[bytes - 4];

    for (uint64_t i = 0; i < len; i++)
      ptr[i] ^= mask[i & 3];
  }

  auto cb =
    [this, opcode] (bool success) {
      // Close connection if write fails or this is a close op code
      if (!success || opcode == WS_OP_CLOSE)
        getConnection()->close();
    };

  getConnection()->write(cb, out);
}


void Websocket::pong() {
  writeFrame(WS_OP_PONG, true, pongPayload.data(), pongPayload.size());
  pongPayload.clear();
}


void Websocket::schedulePong() {
  if (pongEvent.isNull()) {
    auto cb = [this] () {pong();};
    pongEvent = getConnection()->getBase().newEvent(cb, EVENT_NO_SELF_REF);
  }

  if (pongEvent.isSet() && !pongEvent->isPending()) pongEvent->add(5);
}


void Websocket::schedulePing() {
  if (pingEvent.isNull()) {
    auto cb = [this] () {ping(); schedulePing();};
    pingEvent = getConnection()->getBase().newEvent(cb, EVENT_NO_SELF_REF);
  }

  if (pingEvent.isSet()) {
    double timeout = getConnection()->getReadTimeout();
    pingEvent->add(timeout / 2);
  }
}


void Websocket::message(const char *data, uint64_t length) {
  msgReceived++;
  schedulePing();
  TRY_CATCH_ERROR(return onMessage(data, length));
  close(WS_STATUS_UNACCEPTABLE, "Message rejected");
}
