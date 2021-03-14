#pragma once
#include "MCTPlugin.h"
#include "Sockets.h"
#include "Networking/Public/Interfaces/IPv4/IPv4Endpoint.h"
#include "VoxelNetPackets.h"
#include "HAL/Runnable.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/ArrayReader.h"

class APagedWorld;

//  make all buffers persistent

namespace VoxelNetThreads {

	class VoxelNetClient : public FRunnable {
	public:
		FSocket* socket;
		FDateTime lastUpdate = FDateTime::MinValue();
		TQueue<Packet::RegionData, EQueueMode::Mpsc> downloadedRegions;
		TQueue<int64, EQueueMode::Spsc> handshakes;
		APagedWorld* world;
		bool running;

		uint32 remainingRegionsToDownload = 0;
		
		VoxelNetClient(APagedWorld* world, FSocket* socket)
			: socket(socket), world(world), running(true) {
		}


		void Updated() { lastUpdate = FDateTime::Now(); }

		void KeepAlive() {
			if(socket && socket != nullptr && running){
				if (lastUpdate + FTimespan::FromSeconds(SOCKET_TIMEOUT / 2) < FDateTime::Now()) {
					FBufferArchive keepalive(true);
					int32 BytesSent = 0;
					Packet::MakeKeepAlive(keepalive);
					if(socket) { // still getting nullptr crashes
						socket->Send(keepalive.GetData(), keepalive.Num(), BytesSent);
						Updated();
					} else {
						Stop();
					}
					//UE_LOG(LogVoxelNet, Warning, TEXT("Client: Sent keepalive packet. "));
				}
			}
		}

		void Disconnect() {
			FBufferArchive disconnect(true);
			int32 BytesSent = 0;
			Packet::MakeDisconnect(disconnect);
			socket->Send(disconnect.GetData(), disconnect.Num(), BytesSent);
			Updated();
			UE_LOG(LogVoxelNet, Warning, TEXT("Client: Sent disconnect packet. "));
		}

		////// runnable api ///////////
		const int BUFFER_SIZE = 1024 * 1024; // todo reuse buffer

		virtual uint32 Run() override {
			int32 number = 0;

			while (running) {
				FPlatformProcess::Sleep(SOCKET_DELAY); // todo fix crash when we read too few bytes w no sleep
				//FPlatformProcess::Sleep(.2);
				uint32 size;

				KeepAlive();
				if (socket->HasPendingData(size)) {
					FArrayReader opcodeBuffer(true);
					opcodeBuffer.Init(0, BUFFER_SIZE); // todo wouldnt it be better to resuse the same buffer??

					int32 BytesRead = 0;

					if (socket->Recv(opcodeBuffer.GetData(), 1, BytesRead)) {
						if (opcodeBuffer[0] == 0x0) {
							// do nothing
							//UE_LOG(LogVoxelNet, Warning, TEXT("Client: Received keepalive. "));
						}
						else if (opcodeBuffer[0] == 0x1) {
							// handshake
							if (socket->Recv(opcodeBuffer.GetData() + 1, 9, BytesRead)) {
								int64 cookie = 0;
								uint8 version = 0;
								opcodeBuffer << version; // actually the opcode
								opcodeBuffer << version;
								opcodeBuffer << cookie;
								handshakes.Enqueue(cookie);
								UE_LOG(LogVoxelNet, Warning, TEXT("Client: Received handshake packet. Protocol version %d, Cookie: %s"), version, *FString(std::to_string(cookie).c_str()));
							}
						}
						else if (opcodeBuffer[0] == 0x3) {
							// region count - fixed size int32  = 4byte
							if (socket->Recv(opcodeBuffer.GetData() + 1, 4, BytesRead)) {
								uint8 opcode;
								int32 regions;

								opcodeBuffer << opcode;
								opcodeBuffer << regions;

								remainingRegionsToDownload += regions;
								//UE_LOG(LogVoxelNet, Warning, TEXT("Client: Received region count. Expect %d regions to follow."),  regions);

							}
						}
						else if (opcodeBuffer[0] == 0x4) {
							// region response - variable length int encoded
							if (socket->Recv(opcodeBuffer.GetData() + 1, 4, BytesRead)) {
								uint8 opcode;
								uint32 dataSize;

								FArrayReader copy = opcodeBuffer; // could i just seek to 0 instead?

								copy << opcode;
								copy << dataSize;

							//UE_LOG(LogVoxelNet, Warning, TEXT("Client: Received region response with a compressed size of %d. "), dataSize);

								uint32 availableSize = 0;

								//sleep until we can pull that much data
								while (!(socket->HasPendingData(availableSize) && availableSize >= dataSize)) {
									KeepAlive();
									if (!running)
										return 0;
									FPlatformProcess::Sleep(SOCKET_DELAY);
									UE_LOG(LogVoxelNet, Warning, TEXT("Client: Waiting for the rest of the region data... %d / %d"), availableSize, dataSize);
								}

								if (socket->Recv(opcodeBuffer.GetData() + 5, FMath::Min((int32)dataSize, BUFFER_SIZE), BytesRead)) {
									remainingRegionsToDownload--;
									//UE_LOG(LogVoxelNet, Warning, TEXT("Client:  Finished reading compressed chunk received %d bytes out of %d. Remaining regions: %d"), BytesRead, dataSize, remainingRegionsToDownload);

									Packet::RegionData data;
									Packet::ParseRegionResponse(opcodeBuffer, data);
									//UE_LOG(LogVoxelNet, Warning, TEXT("Client:  Downloaded region [%d, %d, %d] %d bytes"), data.x,data.y,data.z, BytesRead);
									downloadedRegions.Enqueue(data);
								}
							}
						}
						else if (opcodeBuffer[0] == 0x5) {
							// disconnect packet
							UE_LOG(LogVoxelNet, Warning, TEXT("Client: Received disconnect packet. "));
							//Disconnect();
							return 5;
						}
						else {
							UE_LOG(LogVoxelNet, Warning, TEXT("Client: Disconnecting due to invalid opcode: %#04x"), opcodeBuffer[0]);
							//Disconnect();
							return -opcodeBuffer[0];
						}
					}
				}
				else { FPlatformProcess::Sleep(SOCKET_DELAY); }
			}
			//Disconnect();
			return 0;
		}

		virtual void Stop() override {
			// shutdown early
			running = false;
		}


		virtual void Exit() override {
			// send disconnect packet
			Disconnect();
		}


	};


	class VoxelNetServer : public FRunnable {
	public:
		FSocket* socket;
		FDateTime lastUpdate = FDateTime::Now();
		const FIPv4Endpoint& endpoint;
		int64 cookie = 0;
		bool running;

		TQueue<TArray<TArray<uint8>>, EQueueMode::Mpsc> regionSetsToSend;

		VoxelNetServer(int64 cookie, FSocket* socket, const FIPv4Endpoint& endpoint)
			: socket(socket), endpoint(endpoint), cookie(cookie), running(true) {
		}

		void UploadRegions(TArray<TArray<uint8>> regionPackets) { regionSetsToSend.Enqueue(regionPackets); }

		void Updated() { lastUpdate = FDateTime::Now(); }

		void Disconnect() const {
			FBufferArchive disconnect;
			int32 BytesSent = 0;
			Packet::MakeDisconnect(disconnect);
			socket->Send(disconnect.GetData(), disconnect.Num(), BytesSent);
			UE_LOG(LogVoxelNet, Warning, TEXT("Server to %s: Sent disconnect packet. "), *endpoint.ToString());
		}

		void SendRegionNumber(int32 regions) const {
			FBufferArchive count;
			int32 BytesSent = 0;
			Packet::MakeRegionCount(count, regions);
			socket->Send(count.GetData(), count.Num(), BytesSent);
		}

		////// runnable api ///////////

		virtual uint32 Run() override {
			//hash the packet for equality checks
			int32 number = 0;

			// handshake
			//int64 cookie = 110111011101110112;

			FBufferArchive handshake(true);
			Packet::MakeHandshake(handshake, cookie);

			int32 sent;
			socket->Send(handshake.GetData(), handshake.Num(), sent);
			Updated();

			UE_LOG(LogVoxelNet, Warning, TEXT("Server to %s: Sent handshake. %d bytes. Protocol version: %d, cookie: %s"), *endpoint.ToString(), sent, PROTOCOL_VERSION, *FString(std::to_string(cookie).c_str()));

			// handshake accepted
			while (running && lastUpdate + FTimespan::FromSeconds(SOCKET_TIMEOUT) >= FDateTime::Now()) {
				while (!regionSetsToSend.IsEmpty()) {
					int32 BytesTotal = 0;
					TArray<TArray<uint8>> upload;
					regionSetsToSend.Dequeue(upload);
					SendRegionNumber(upload.Num());
					for (auto& elem : upload) {
						int32 BytesSent = 0;
						socket->Send(elem.GetData(), elem.Num(), BytesSent);
						//UE_LOG(LogVoxelNet, Verbose, TEXT("----> Server sent region [%s] %d bytes"), BytesSent);
						BytesTotal += BytesSent;
					}
					UE_LOG(LogVoxelNet, Verbose, TEXT("----> Server sent %f kilobytes of compressed region data, with %d regions."), BytesTotal/1024.0, upload.Num());
					//FPlatformProcess::Sleep(SOCKET_DELAY); // todo see how changing this affects client
				}
				
				FPlatformProcess::Sleep(SOCKET_DELAY); // todo see how changing this affects client
				uint32 size;

				if (socket->HasPendingData(size)) {
					FArrayReader opcodeBuffer(true);
					opcodeBuffer.Init(0, 1024);

					//int32 BytesSent = 0;
					int32 BytesRead = 0;

					if (socket->Recv(opcodeBuffer.GetData(), 1, BytesRead)) {
						Updated();
						if (opcodeBuffer[0] == 0x0) {
							// keepalive packet
							// respond with a keepalive as well, maybe rate limit this
							FBufferArchive keepalive(true);
							int32 BytesSent = 0;
							Packet::MakeKeepAlive(keepalive);
							socket->Send(keepalive.GetData(), keepalive.Num(), BytesSent);
						}
						else if (opcodeBuffer[0] == 0x5) {
							// disconnect packet
							UE_LOG(LogVoxelNet, Warning, TEXT("Server to %s: Received disconnect packet. "), *endpoint.ToString());
							return 5;
						}
						else {
							UE_LOG(LogVoxelNet, Warning, TEXT("Server to %s: Disconnecting %s due to invalid opcode: %#04x"), *endpoint.ToString(), *endpoint.ToString(), opcodeBuffer[0]);
							return -opcodeBuffer[0];
						}
					}
				}
			}
			if (lastUpdate + FTimespan::FromSeconds(SOCKET_TIMEOUT) < FDateTime::Now()) {
				UE_LOG(LogVoxelNet, Warning, TEXT("Server to %s: Disconnecting %s due to timeout. "), *endpoint.ToString(), *endpoint.ToString());
				return 1;
			}
			else {
				UE_LOG(LogVoxelNet, Warning, TEXT("Server to %s: Disconnecting %s due to request. "), *endpoint.ToString(), *endpoint.ToString());
				Disconnect();
				return 0;
			}
		}


		virtual void Stop() override {
			// shutdown early
			running = false;
		}


		virtual void Exit() override {
			// send disconnect packet
			Disconnect();
		}


	};


};
