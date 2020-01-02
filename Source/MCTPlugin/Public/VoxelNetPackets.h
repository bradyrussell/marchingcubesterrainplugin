#pragma once

#include "Config.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Serialization/ArchiveLoadCompressedProxy.h"

#define SOCKET_DELAY .001
#define SOCKET_TIMEOUT 10.0
#define PROTOCOL_VERSION 0x2

namespace Packet {
	struct RegionData {
		int32 x,y,z;
		uint8 data[2][REGION_SIZE][REGION_SIZE][REGION_SIZE];
	};


	inline void MakeKeepAlive(FArchive &packet) {
		uint8 nulbyte = 0;
		packet << nulbyte;
	}

	inline void MakeHandshake(FArchive &packet, int64 cookie) {
		uint8 op = 0x01, ver = PROTOCOL_VERSION;
		packet << op;
		packet << ver;
		packet << cookie;
	}

	inline void MakeRegionCount(FArchive &packet, int32 regions) {
		uint8 header = 3;
		packet << header;
		packet << regions;
	}

	inline uint32 MakeRegionContents(FArchive &packet, RegionData &data) { 
		TArray<uint8> compressed;
		FArchiveSaveCompressedProxy compressor(compressed, NAME_Zlib);

		uint8 header = 4;
		packet << header;

		compressor << data.x;
		compressor << data.y;
		compressor << data.z;

		for(int n=0; n < 2; n++){
			for(int x=0; x < REGION_SIZE; x++){
				for(int y=0; y < REGION_SIZE; y++){
					for(int z=0; z < REGION_SIZE; z++) {
						compressor << data.data[n][x][y][z];
					}
				}
			}
		}

		compressor.Flush();

		//uint16 size = compressed.Num()+4;

		//packet << size;
		packet << compressed; // appears to write an additional 4 byte header, prob int size
		compressor.Close();
		return compressed.Num();
	}

	inline void MakeDisconnect(FArchive &packet) {
		uint8 dcbyte = 0x5;
		packet << dcbyte;
		packet << dcbyte;
	}

	inline void ParseRegionResponse(FArchive &packet, RegionData &data) {
		TArray<uint8> compressed;
		uint8 header;
		//uint16 size;

		packet << header;
		//packet << size;
		packet << compressed;
		FArchiveLoadCompressedProxy decompressor(compressed, NAME_Zlib);

		decompressor << data.x;
		decompressor << data.y;
		decompressor << data.z;

		for(int n=0; n < 2; n++){
			for(int x=0; x < REGION_SIZE; x++){
				for(int y=0; y < REGION_SIZE; y++){
					for(int z=0; z < REGION_SIZE; z++) {
						decompressor << data.data[n][x][y][z];
					}
				}
			}
		}

		decompressor.Flush();
		decompressor.Close();

	}
};