# Fragmented

Fragmented is a merge hell for spike modules that are too simple to have their own toolset project, or common modules across multiple projects.

This project runs on Spike foundation.

Head to this **[Wiki](https://github.com/PredatorCZ/Spike/wiki/Spike)** for more information on how to effectively use it.
<h2>Module list</h2>
<table>
<tr><td><a href="#The-Thing-BST-to-GLTF">The Thing BST to GLTF</a></td><td>Convert The Thing bst animset to gltf</td></tr>
<tr><td><a href="#Chaos-Legion-CPC/ITM-to-GLTF">Chaos Legion CPC/ITM to GLTF</a></td><td>Convert Chaos Legion CPC/item to GLTF</td></tr>
<tr><td><a href="#Extract-PSARC">Extract PSARC</a></td><td>Extract PlayStation archive</td></tr>
<tr><td><a href="#Extract-Moorhuhn-2-WTN">Extract Moorhuhn 2 WTN</a></td><td>Extract moorhuhn2.wtn</td></tr>
<tr><td><a href="#Extract-Moorhuhn-3-DAT">Extract Moorhuhn 3 DAT</a></td><td>Extract moorhuhn3.dat</td></tr>
<tr><td><a href="#Trapt-SAI-to-GLTF">Trapt SAI to GLTF</a></td><td>Convert trapt sai to GLTF</td></tr>
</table>

## The Thing BST to GLTF

### Module command: bst_to_gltf

Convert BST+SGH+AN collections to GLTF

> [!NOTE]
> The following file patterns apply to `batch.json` which is described [HERE](https://github.com/PredatorCZ/Spike/wiki/Spike---Batching)

### Main file patterns: `.bst$`

### Secondary file patterns: `.an$`, `.sgh$`

## Chaos Legion CPC/ITM to GLTF

### Module command: cpc_to_gltf



### Input file patterns: `.cpc$`, `.CPC$`, `^ITM*.BIN$`

## Extract PSARC

### Module command: extract_psarc



## Extract Moorhuhn 2 WTN

### Module command: mh2_extract



### Input file patterns: `^MoorHuhn2.wtn$`

## Extract Moorhuhn 3 DAT

### Module command: mh3_extract



### Input file patterns: `^moorhuhn3.dat$`

## Trapt SAI to GLTF

### Module command: sai_to_gltf



> [!NOTE]
> The following file patterns apply to `batch.json` which is described [HERE](https://github.com/PredatorCZ/Spike/wiki/Spike---Batching)

### Main file patterns: `.glb$`, `.gltf$`

### Secondary file patterns: `.sai$`



## [Latest Release](https://github.com/PredatorCZ/Fragmented/releases)

## License

This toolset is available under GPL v3 license. (See LICENSE)\
This toolset uses following libraries:

- Spike, Copyright (c) 2016-2025 Lukas Cone (Apache 2)
