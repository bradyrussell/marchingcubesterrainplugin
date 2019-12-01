#pragma once

// voxel config
#define REGION_SIZE 32 //voxels
#define VOXEL_SIZE 100 // cm
#define MAX_MATERIALS 6
#define MARCHING_CUBES 1
#define ASYNC_COLLISION true//!WITH_EDITOR//false
//#define NEW_REGIONS_PER_TICK 5

#define VOXELNET_PORT 9292//9797

//#define REGEN_NULL_REGIONS
//#define DONT_SAVE
#define WORLD_TICK_TRACKING
#define DATABASE_OPTIMIZATIONS

// db config
#define DB_NAME "WorldDatabase" // directory name of db
#define DB_GLOBAL_TAG "MapGlobalData_" // 14 bytes or more so we dont conflict with region keys. // because region data can encompass all 13 byte strings, we will always interrupt ordering somehow
#define DB_VERSION_TAG "DB_VERSION" //
#define DB_VERSION 2 // changes when non backwards compatible changes to the structure occur

// regional data offsets, max of 255 - REGION_SIZE 
#define REGIONAL_DATA_BUILDING 0 // store a TArray<TArray<uint8>> where each element of top level array is a serialized building
#define REGIONAL_DATA_ENTITY 1 // store an array of actor archives with spawn information
#define REGIONAL_DATA_CONTAINER 2 // store all item containers in the chunk
#define REGIONAL_DATA_RESOURCES 3 // store dropped items

#define REGIONAL_DATA_MAX 255-REGION_SIZE // 223 for VOXEL_SIZE of 32