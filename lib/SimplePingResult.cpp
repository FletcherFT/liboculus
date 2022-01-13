/*
 * Copyright (c) 2017-2022 University of Washington
 * Author: Aaron Marburg <amarburg@uw.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of University of Washington nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "liboculus/SimplePingResult.h"
#include "liboculus/SonarConfiguration.h"

namespace liboculus {

SimplePingResult::SimplePingResult(const std::shared_ptr<ByteVector> &buffer)
  : MessageHeader(buffer),
    _flags(fireMsg()->flags),
    _bearings(),
    _gains(),
    _image() {
  assert(buffer->size() >= sizeof(OculusSimplePingResult));

  // Bearing data is packed into an array of shorts at the end of the
  // OculusSimpleFireMessage
  const int16_t *bearingData = reinterpret_cast<const short*>(buffer->data() + sizeof(OculusSimplePingResult));
  _bearings = BearingData(bearingData, ping()->nBeams);

  const uint8_t *imageData = reinterpret_cast<const uint8_t*>(buffer->data() + ping()->imageOffset);

  if (_flags.getSendGain()) { 
    // If sent, the gain is included as the first 4 bytes in each "row" of data 
    const uint16_t offsetBytes = 4;

    // The size of one "row" of data in bytes
    const uint16_t strideBytes = SizeOfDataSize(ping()->dataSize)*ping()->nBeams + offsetBytes;
    _image = ImageData(imageData,
                          ping()->imageSize,
                          ping()->nRanges,
                          ping()->nBeams,
                          SizeOfDataSize(ping()->dataSize),
                          strideBytes,
                          offsetBytes);

    _gains = GainData_t(reinterpret_cast<const GainData_t::DataType *>(imageData),
                          ping()->imageSize,
                          strideBytes, 
                          ping()->nRanges);
  } else {
    _image = ImageData(imageData,
                          ping()->imageSize,
                          ping()->nRanges,
                          ping()->nBeams,
                          SizeOfDataSize(ping()->dataSize));
  }
}

bool SimplePingResult::valid() const {
  if (_buffer->size() < sizeof(OculusMessageHeader)) return false;
  if (_buffer->size() < packetSize()) return false;

  MessageHeader hdr(_buffer);
  if (!hdr.valid()) {
    LOG(WARNING) << "Header not valid";
    return false;
  }

  int num_pixels = ping()->nRanges * ping()->nBeams;
  size_t expected_size = SizeOfDataSize(ping()->dataSize) * num_pixels;

  if (flags().getSendGain()) {
    expected_size += sizeof(uint32_t) * ping()->nRanges;
  }

  if (ping()->imageSize != expected_size) {
    LOG(WARNING) << "ImageSize in header " << ping()->imageSize
                 << " does not match expected data size of "
                 << expected_size;
    return false;
  }

  CHECK(ping()->imageOffset > sizeof(OculusSimplePingResult));
  return true;
}

void SimplePingResult::dump() const {
  LOG(DEBUG) << "--------------";
  MessageHeader::dump();
  LOG(DEBUG) << "        Mode: " << FreqModeToString(fireMsg()->masterMode);

  const int pingRate = PingRateToHz(fireMsg()->pingRate);
  if (pingRate >= 0 ) {
    LOG(DEBUG) << "   Ping rate: " << pingRate;
  } else {
    LOG(DEBUG) << "   Ping rate: (unknown) " << static_cast<int>(fireMsg()->pingRate);
  }

  LOG(DEBUG) << "     Ping ID: " << ping()->pingId;
  LOG(DEBUG) << "      Status: " << ping()->status;
  LOG(DEBUG) << "   Ping start time: " << ping()->pingStartTime;

  LOG(DEBUG) << "   Frequency: " << ping()->frequency;
  LOG(DEBUG) << " Temperature: " << ping()->temperature;
  LOG(DEBUG) << "    Pressure: " << ping()->pressure;
  LOG(DEBUG) << "Spd of Sound: " << ping()->speedOfSoundUsed;
  LOG(DEBUG) << "   Range res: " << ping()->rangeResolution << " m";

  LOG(DEBUG) << "   Num range: " << ping()->nRanges;
  LOG(DEBUG) << "   Num beams: " << ping()->nBeams;

  LOG(DEBUG) << "  Image size: " << ping()->imageSize;
  LOG(DEBUG) << "Image offset: " << ping()->imageOffset;
  LOG(DEBUG) << "   Data size: " << DataSizeToString(ping()->dataSize);
  LOG(DEBUG) << "   Send gain: " << (flags().getSendGain() ? "Yes" : "No");
  LOG(DEBUG) << "Message size: " << ping()->messageSize;
  LOG(DEBUG) << "--------------";
}

} // namespace liboculus
