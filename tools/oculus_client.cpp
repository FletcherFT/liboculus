
#include <memory>
#include <thread>
#include <string>
#include <fstream>

using std::string;

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "g3log_ros/ROSLogSink.h"
#include "g3log_ros/g3logger.h"
#include <CLI11/CLI11.hpp>

#include "liboculus/DataRx.h"
#include "liboculus/StatusRx.h"
#include "liboculus/IoServiceThread.h"
#include "liboculus/SonarPlayer.h"
#include "liboculus/PingAgreesWithConfig.h"

using std::ofstream;
using std::ios_base;
using std::shared_ptr;

using liboculus::SonarConfiguration;
using liboculus::IoServiceThread;
using liboculus::DataRx;
using liboculus::StatusRx;
using liboculus::SimplePingResult;
using liboculus::SonarStatus;
using liboculus::SonarPlayerBase;
//using liboculus::SonarPlayer;

int playbackSonarFile(const std::string &filename, ofstream &output,
                      int stopAfter = -1);

// Make these global so signal handler can access it
std::unique_ptr<liboculus::IoServiceThread> _io_thread;
bool doStop = false;

// Catch signals
void signalHandler(int signo) {
  if (_io_thread) _io_thread->stop();
  doStop = true;
}

int main(int argc, char **argv) {
  
  libg3logger::G3Logger<ROSLogSink> logger("ocClient");

  CLI::App app{"Simple Oculus Sonar app"};

  int verbosity = 0;
  app.add_flag("-v,--verbose", verbosity, "Additional output (use -vv for even more!)");

  string ipAddr("auto");
  app.add_option("ip", ipAddr, "IP address of sonar or \"auto\" to automatically detect.");

  string outputFilename("");
  app.add_option("-o,--output", outputFilename, "Saves raw sonar data to specified file.");

  string inputFilename("");
  app.add_option("-i,--input", inputFilename, 
                  "Reads raw sonar data from specified file.   Plays file contents rather than contacting \"real\" sonar on network.");

  int bitDepth(8);
  app.add_option("-b,--bits", bitDepth, 
                  "Bit depth oof data (8,16,32)");

  int stopAfter = -1;
  app.add_option("-n,--frames", stopAfter, "Stop after (n) frames.");

  float range = 4;
  app.add_option("-r,--range", range, "Range in meters");

  CLI11_PARSE(app, argc, argv);

  if (verbosity == 1) {
    logger.setLevel(INFO);
  } else if (verbosity > 1) {
    logger.setLevel(DEBUG);
  }

  if ((bitDepth != 8) && (bitDepth != 16) && (bitDepth != 32)) {
    LOG(FATAL) << "Invalid bit depth " << bitDepth;
    exit(-1);
  }

  ofstream output;

  if (!outputFilename.empty()) {
    LOG(DEBUG) << "Opening output file " << outputFilename;
    output.open(outputFilename, ios_base::binary | ios_base::out);

    if (!output.is_open()) {
      LOG(WARNING) << "Unable to open " << outputFilename << " for output.";
      exit(-1);
    }
  }

  // If playing back an input file, run a different main loop ...
  if (!inputFilename.empty()) {
     playbackSonarFile(inputFilename, output, stopAfter);
     return 0;
  }

  int count = 0;

  signal(SIGHUP, signalHandler);

  LOG(DEBUG) << "Starting loop";

  SonarConfiguration config;
  config.setPingRate(pingRateNormal);
  config.setRange(range);

  if (bitDepth == 8) {
    config.setDataSize(dataSize8Bit);
  } else if (bitDepth == 16) {
    config.setDataSize(dataSize16Bit);
  } else if (bitDepth == 32) {
    config.sendGain()
          .noGainAssistance()
          .setDataSize(dataSize32Bit);
  }

  _io_thread.reset(new IoServiceThread);
  DataRx _data_rx(_io_thread->context());
  StatusRx _status_rx(_io_thread->context());

  // Callback for a SimplePingResultV1
  _data_rx.setCallback<liboculus::SimplePingResultV1>([&](const liboculus::SimplePingResultV1 &ping) {
    // Pings send to the callback are always valid

    {
      const auto valid = checkPingAgreesWithConfig(ping, config);
      if (!valid) {
        LOG(WARNING) << "Mismatch between requested config and ping";
      }
    }

    ping.dump();

    if (output.is_open()) {
      const char *cdata = reinterpret_cast<const char *>(ping.buffer()->data());
      output.write(cdata, ping.buffer()->size());
    }

    count++;
    if ((stopAfter > 0) && (count >= stopAfter)) _io_thread->stop();
  });

  // Callback for a SimplePingResultV2
  _data_rx.setCallback<liboculus::SimplePingResultV2>([&](const liboculus::SimplePingResultV2 &ping) {
    // Pings send to the callback are always valid

    // {
    //   const auto valid = checkPingAgreesWithConfig(ping, config);
    //   if (!valid) {
    //     LOG(WARNING) << "Mismatch between requested config and ping";
    //   }
    // }

    ping.dump();

    // const auto gains = ping.gains();
    // if (gains.size() > 0) {
    //   LOG(INFO) << "First five gains " << gains[10] << ", " << gains[11] << ", " 
    //             << gains[12] << ", " << gains[13] << ", " << gains[14];
    // }

    if (output.is_open()) {
      const char *cdata = reinterpret_cast<const char *>(ping.buffer()->data());
      output.write(cdata, ping.buffer()->size());
    }

    count++;
    if ((stopAfter > 0) && (count >= stopAfter)) doStop=true;
  });

  // Callback when connection to a sonar
  _data_rx.setOnConnectCallback([&]() {
    config.dump();
    _data_rx.sendSimpleFireMessage(config);
  });

  // Connect the client
  if (ipAddr == "auto") {
    // To auto-detect, when the StatusRx connects, 
    // configure the DataRx
    _status_rx.setCallback([&](const SonarStatus &status, bool is_valid){
      if (!is_valid || _data_rx.isConnected()) return;
      _data_rx.connect(status.ipAddr());
    });
  } else {
    _data_rx.connect(ipAddr);
  }
  _io_thread->start();

  int lastCount = 0;
  while (!doStop) {

    // Very rough Hz calculation right now
    const auto c = count;
    LOG(INFO) << "Received pings at " << c-lastCount << " Hz";

    lastCount = c;
    sleep(1);
  }

  _io_thread->stop();
  _io_thread->join();

  if (output.is_open()) output.close();

  LOG(INFO) << "At exit";

  return 0;
}


template <typename Ping_t>
  void pingCallback(const Ping_t &ping) {
    std::cout << ping << std::endl;
  }


int playbackSonarFile(const std::string &filename, ofstream &output, int stopAfter) {
  shared_ptr<SonarPlayerBase> player(SonarPlayerBase::OpenFile(filename));

  if( !player ) {
    LOG(WARNING) << "Unable to open sonar file";
    return -1;
  }

  if( !player->open(filename) ) {
    LOG(INFO) << "Failed to open " << filename;
    return -1;
  }

  int count = 0;
  std::cout << player->isOpen() << std::endl;
  bool what = player->nextPing();
  // OculusSimplePingResult;
  // while( player->nextPing() && !player->eof() ) {
  //   if (!ping.valid()) {
  //     LOG(WARNING) << "Invalid ping";
  //     continue;
  //   }

  //   ping.dump();

  //   if (output.is_open()) {
  //const char *cdata = reinterpret_cast<const char *>(ping.buffer().data());
  //     output.write(cdata, ping.buffer().size());
  //   }

  //   count++;
  //   if( (stopAfter > 0) && (count >= stopAfter) ) break;
  // }

  LOG(INFO) << count << " sonar packets decoded";

  return 0;
}
