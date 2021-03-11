# Marching Cubes Terrain Plugin

An Unreal Engine plugin that utilizes [Polyvox](http://www.volumesoffun.com/polyvox-about/) & [LevelDB](https://github.com/google/leveldb) and depends on the [UnrealFastNoise](https://github.com/midgen/UnrealFastNoise) & [RuntimeMeshComponent](https://github.com/Koderz/RuntimeMeshComponent) plugins.

The master branch of this repo is currently targeting UE 4.26.1.

# Purpose

This plugin allows for multiplayer runtime editable procedural voxel terrain in UE4, which can be rendered using Marching Cubes or Cubic meshes.

This means you can create procedural worlds from a numeric seed which expand "infinitely" in any direction such as in Minecraft. This plugin uses cubic regions so there is infinite height and depth as well.

Works with UE4 navmesh generation, check out NavInvokers for best performance.

# Example
[![](http://img.youtube.com/vi/GseUuAAKobw/0.jpg)](http://www.youtube.com/watch?v=GseUuAAKobw "Marching Cubes Terrain Plugin")

# Usage
## Download the plugin and dependencies to your Plugins folder
This plugin as well as:
[UnrealFastNoise](https://github.com/midgen/UnrealFastNoise)
[RuntimeMeshComponent](https://github.com/Koderz/RuntimeMeshComponent)
[LDBPlugin](https://github.com/bradyrussell/ldbplugin)

## Launch the editor and create a BP child of the PagedWorld class

This is the only class you are required to subclass.
![Paged World class](https://i.imgur.com/sKuTwON.png)

## BeginPlay script

Set up your beginplay similar to this:
![BeginPlay setup](https://i.imgur.com/0lU5wt7.png)

## Override function GetNoiseGenerators()

This is where you make your World Generation unique. You will have to customize the C++ as well to make really custom worlds.
![Noise generator](https://i.imgur.com/4EO9eAu.png)

## World Settings

Customize the rendering settings in the class defaults tab. You can leave the rest alone.
![Rendering settings](https://i.imgur.com/UXhmUkw.png)

## Now open or create your PlayerController
Add a TerrainPagingComponent to the PlayerController and set the view distance (this is a radius).

Next we need to set up multiplayer connections, here is a quick way:
![Multiplayer setup 1](https://i.imgur.com/1a5kqx3.png)

Here is a quick hack to prevent the player from falling / moving before he gets connected:
![Player movement disable](https://i.imgur.com/cpAyGxa.png)

# Result (with Run Dedicated Server checked)
![enter image description here](https://i.imgur.com/3cnfBxo.png)
