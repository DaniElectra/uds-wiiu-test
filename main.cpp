#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/systeminfo.h>
#include <coreinit/memdefaultheap.h>
#include <nn/uds.h>

#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_console.h>

#include <array>
#include <codecvt>
#include <future>
#include <locale>
#include <thread>
#include <stdlib.h>
#include <cstring>

// Helper macro for printing the raw result
#define RAW_RESULT(res) (reinterpret_cast<NNResult*>(&res)->value)

int new_client_func(std::shared_future<uint16_t> future_node_id) {
   // Wait until a new client connects to the network
   future_node_id.wait();
   uint16_t node_id = future_node_id.get();

   // Show node information about the new client
   nn::uds::Cafe::NodeInformation info = {};
   nn::Result res = nn::uds::Cafe::GetNodeInformation(&info, node_id);
   if (res.IsFailure()) {
      WHBLogPrintf("nn::uds::Cafe::GetNodeInformation failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
   } else {
      WHBLogPrintf("Node information:");
      WHBLogPrintf("Scrambled friend code: %016X", info.scrambledLocalFriendCode.localFriendCode);
      WHBLogPrintf("Scrambled node ID: %d", info.scrambledLocalFriendCode.networkNodeId);
      WHBLogPrintf("Scrambled XOR key: %d", info.scrambledLocalFriendCode.xorKey);
      std::u16string username(info.username.name);
      std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t> convert;
      std::string normalUsername = convert.to_bytes(username);
      WHBLogPrintf("Username: %s", normalUsername.c_str());
      WHBLogPrintf("Node ID: %d", info.networkNodeId);
   }

   // Make a new endpoint
   nn::uds::Cafe::EndpointDescriptor fd = 0;
   int last_tm_sec = -1;
   res = nn::uds::Cafe::CreateEndpoint(&fd);
   if (res.IsFailure()) {
      WHBLogPrintf("nn::uds::Cafe::CreateEndpoint failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
   }

   // Attach the endpoint to all nodes and data channel 1 (the same as the libctru example)
   res = nn::uds::Cafe::Attach(&fd, UDS_BROADCAST_NODE_ID, 1, 0x600);
   if (res.IsFailure()) {
      WHBLogPrintf("nn::uds::Cafe::Attach failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
   }

   std::array<uint8_t, 0x600> data;
   char dataString[0x600 * 2];
   uint32_t receivedSize = 0;
   while(WHBProcIsRunning()) {
      OSCalendarTime tm;
      OSTicksToCalendarTime(OSGetTime(), &tm);

      // Print debugging to test blocking flags
      if (tm.tm_sec != last_tm_sec) {
         WHBLogPrintf("%02d/%02d/%04d %02d:%02d:%02d I'm still here.",
                      tm.tm_mday, tm.tm_mon, tm.tm_year,
                      tm.tm_hour, tm.tm_min, tm.tm_sec);
         last_tm_sec = tm.tm_sec;
      }

      // Receive data from the client
      res = nn::uds::Cafe::Receive(fd, data.data(), &receivedSize, data.size(), nn::uds::Cafe::UDS_RECEIVE_NONBLOCK);
      if (res.IsFailure()) {
         WHBLogPrintf("nn::uds::Cafe::Receive failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
      } else if (receivedSize != 0) { // If we have received data...
         for (int i = 0; i < receivedSize; i++) {
            std::snprintf(dataString + i * 2, 0x600 * 2 - i * 2, "%02X", data[i]);
         }
         WHBLogPrintf("Received data: 0x%s", dataString);

         // Send the same data to all nodes on the network over data channel 1
         res = nn::uds::Cafe::SendTo(fd, data.data(), receivedSize, UDS_BROADCAST_NODE_ID, 1, 0);
         if (res.IsFailure()) {
            WHBLogPrintf("nn::uds::Cafe::SendTo failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
         }
      }
      OSSleepTicks(OSMillisecondsToTicks(100));
   }

   // We are exiting, destroy the endpoint
   res = nn::uds::Cafe::DestroyEndpoint(&fd);
   if (res.IsFailure()) {
      WHBLogPrintf("nn::uds::Cafe::DestroyEndpoint failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
   }

   return 0;
}

int
hello_thread(std::promise<uint16_t>& future_node_id)
{
   // int last_tm_sec = -1;
   WHBLogPrintf("Hello World from a std::thread!");

   // Initialize with recommended memory size and alignment 0x40 (same as MH3U Packet Relay)
   void *workMem = calloc(nn::uds::Cafe::kWorkMemorySize, 0x40);
   nn::Result res = nn::uds::Cafe::Initialize(workMem, nn::uds::Cafe::kWorkMemorySize);
   if (res.IsFailure()) {
      WHBLogPrintf("nn::uds::Cafe::Initialize failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
   }

   // The 3DS example of UDS uses 0x48425710 as the local communication ID, however we can generate this with
   // CreateLocalCommunicationId since it truncates the unique ID to 20 bits
   // WHBLogPrintf("Local communication ID: %d", nn::uds::Cafe::CreateLocalCommunicationId(0x484257, false));

   // Create a new network with the same parameters as the libctru example, but with maxNodes=9 and channel chosen automatically
   const char* passphrase = "udsdemo passphrase c186093cd2652741";
   uint8_t appdata[0x14] = {0x69, 0x8a, 0x05, 0x5c};
   strncpy(reinterpret_cast<char*>(&appdata[4]), "Wii U appdata", sizeof(appdata)-4);
   res = nn::uds::Cafe::CreateNetwork(0, 9, 0x48425710, passphrase, strlen(passphrase)+1, 0, appdata, sizeof(appdata));
   if (res.IsFailure()) {
      WHBLogPrintf("nn::uds::Cafe::CreateNetwork failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
   }

   while(WHBProcIsRunning()) {
      // OSCalendarTime tm;
      // OSTicksToCalendarTime(OSGetTime(), &tm);
      //
      // if (tm.tm_sec != last_tm_sec) {
      //    WHBLogPrintf("%02d/%02d/%04d %02d:%02d:%02d I'm still here.",
      //                 tm.tm_mday, tm.tm_mon, tm.tm_year,
      //                 tm.tm_hour, tm.tm_min, tm.tm_sec);
      //    last_tm_sec = tm.tm_sec;
      // }

      // Poll until the connection status changes
      res = nn::uds::Cafe::PollStateChange(nn::uds::Cafe::UDS_POLL_NONBLOCK);
      if (res.IsFailure() && res.GetDescription() != 1018) { // LEGACY_DESCRIPTION_NOT_FOUND
         WHBLogPrintf("nn::uds::Cafe::PollStateChange failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
      }

      if (res.IsSuccess()) {
         WHBLogPrintf("Successfully polled!");
         nn::uds::Cafe::ConnectionStatus connectionStatus{};
         res = nn::uds::Cafe::GetConnectionStatus(&connectionStatus);
         if (res.IsFailure()) {
            WHBLogPrintf("nn::uds::Cafe::GetConnectionStatus failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
         } else if (connectionStatus.nodeBitmask != 0) {
            WHBLogPrintf("Connection status:");
            WHBLogPrintf("Status: %d", connectionStatus.status);
            WHBLogPrintf("Reason: %d", connectionStatus.reason);
            for (int i = 0; i < connectionStatus.maxNodes; i++) {
               if (connectionStatus.changedNodes & (1 << i)) {
                  if (connectionStatus.nodes[i] == 0) {
                     // This is technically incorrect, the array index won't always match the node ID
                     WHBLogPrintf("Node ID %d disconnected!", i + 1);
                  } else if (connectionStatus.nodes[i] == connectionStatus.networkNodeId) {
                     // Print our node ID (always 1)
                     WHBLogPrintf("Network setup! Node ID: %d", i + 1);
                  } else {
                     WHBLogPrintf("Node ID %d connected!", connectionStatus.nodes[i]);
                     future_node_id.set_value(connectionStatus.nodes[i]);
                  }
               }
            }
         } else {
            WHBLogPrintf("Nothing changed!");
         }
      }

      WHBLogConsoleDraw();
      OSSleepTicks(OSMillisecondsToTicks(100));
   }

   // Exiting, destroy the network
   res = nn::uds::Cafe::DestroyNetwork();
   if (res.IsFailure()) {
      WHBLogPrintf("nn::uds::Cafe::DestroyNetwork failed! Error: 0x%X, Sum: %d, Desc: %d", RAW_RESULT(res), res.GetSummary(), res.GetDescription());
   }

   // Finalize UDS
   nn::uds::Cafe::Finalize();

   // Free the previously allocated memory
   free(workMem);

   WHBLogPrintf("Exiting... good bye.");
   WHBLogConsoleDraw();
   OSSleepTicks(OSMillisecondsToTicks(1000));
   return 0;
}

int
main(int argc, char **argv)
{
   WHBProcInit();
   WHBLogConsoleInit();
   std::promise<uint16_t> node_id;
   std::shared_future<uint16_t> future_node_id(node_id.get_future());

   std::thread t([&]{hello_thread(node_id);});
   std::thread new_client([&]{new_client_func(future_node_id);});
   t.join();
   new_client.join();

   WHBLogConsoleFree();
   WHBProcShutdown();

   return 0;
}
