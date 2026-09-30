const fs = require("fs/promises");

// Optional maintainer tool. Normal builds compile the checked-in C snapshot.
const snapshot = require("./generated/registry_snapshot.json");
if (snapshot.protocol !== 772 || snapshot.version !== "1.21.8") throw Error("Protocol snapshot mismatch");
const biomes = ["plains", "mangrove_swamp", "desert", "snowy_plains", "beach"];
async function extractItemsAndBlocks () {
  return {palette: snapshot.palette, items: snapshot.items,
    mapping: snapshot.mappingToBlock, mappingWithOverrides: snapshot.mapping,
    blockRegistry: snapshot.blockRegistry};
}

// Write an integer as a VarInt
function writeVarInt (value) {
  const bytes = [];
  while (true) {
    if ((value & ~0x7F) === 0) {
      bytes.push(value);
      return Buffer.from(bytes);
    }
    bytes.push((value & 0x7F) | 0x80);
    value >>>= 7;
  }
}

// Serialize a single registry
function serializeRegistry (name, entries) {
  const parts = [];

  // Packet ID for Registry Data
  parts.push(Buffer.from([0x07]));

  // Registry name
  const nameBuf = Buffer.from(name, "utf8");
  parts.push(writeVarInt(nameBuf.length));
  parts.push(nameBuf);

  // Entry count
  parts.push(writeVarInt(entries.length));

  // Serialize entries
  for (const entryName of entries) {
    const entryBuf = Buffer.from(entryName, "utf8");
    parts.push(writeVarInt(entryBuf.length));
    parts.push(entryBuf);
    parts.push(Buffer.from([0x00]));
  }

  // Combine all parts
  const fullData = Buffer.concat(parts);

  // Prepend packet length
  const lengthBuf = writeVarInt(fullData.length);

  return Buffer.concat([lengthBuf, fullData]);
}

// Serialize a tag update
function serializeTags (tags) {
  const parts = [];

  // Packet ID for Update Tags
  parts.push(Buffer.from([0x0D]));

  // Tag type count
  parts.push(writeVarInt(Object.keys(tags).length));

  // Tag registry entry
  for (const type in tags) {

    // Tag registry identifier
    const identifier = Buffer.from(type, "utf8");
    parts.push(writeVarInt(identifier.length));
    parts.push(identifier);

    // Tag count
    parts.push(writeVarInt(Object.keys(tags[type]).length));

    // Write tag data
    for (const tag in tags[type]) {
      // Tag identifier
      const identifier = Buffer.from(tag, "utf8");
      parts.push(writeVarInt(identifier.length));
      parts.push(identifier);
      // Array of IDs
      parts.push(writeVarInt(Object.keys(tags[type][tag]).length));
      for (const id of tags[type][tag]) {
        parts.push(writeVarInt(id));
      }
    }

  }

  // Combine all parts
  const fullData = Buffer.concat(parts);

  // Prepend packet length
  const lengthBuf = writeVarInt(fullData.length);

  return Buffer.concat([lengthBuf, fullData]);
}

function toVarIntBuffer (array) {
  const parts = [];
  for (const num of array) {
    parts.push(writeVarInt(num));
  }
  return Buffer.concat(parts);
}

// Convert to C-style hex byte array string
function toCArray (buffer) {
  const hexBytes = [...buffer].map(b => `0x${b.toString(16).padStart(2, "0")}`);
  const lines = [];
  for (let i = 0; i < hexBytes.length; i += 12) {
    lines.push("  " + hexBytes.slice(i, i + 12).join(", "));
  }
  return lines.join(",\n");
}

const requiredRegistries = [
  "cat_variant",
  "chicken_variant",
  "cow_variant",
  "frog_variant",
  "painting_variant",
  "pig_variant",
  "wolf_sound_variant",
  "wolf_variant",
  "damage_type"
];

async function convert () {

  const outputPath = __dirname + "/src/registries.c";
  const headerPath = __dirname + "/include/registries.h";

  const registries = snapshot.registries;
  const registryBuffers = [];

  for (const registry of requiredRegistries) {
    if (!(registry in registries)) {
      console.error(`Missing required registry "${registry}"!`);
      return;
    }
    if (registry.endsWith("variant")) {
      // The mob "variants" only require one valid variant to be accepted
      // Send "temperate" if available, otherwise shortest string to save memory
      if (registries[registry].includes("temperate")) {
        registryBuffers.push(serializeRegistry(registry, ["temperate"]));
      } else {
        const shortest = registries[registry].sort((a, b) => a.length - b.length)[0];
        registryBuffers.push(serializeRegistry(registry, [shortest]));
      }
    } else {
      registryBuffers.push(serializeRegistry(registry, registries[registry]));
    }
  }
  // Send biomes separately - only "plains" is actually required
  registryBuffers.push(serializeRegistry("worldgen/biome", biomes));
  // Send dimensions separately - we only use "overworld"
  registryBuffers.push(serializeRegistry("dimension_type", ["overworld"]));
  const fullRegistryBuffer = Buffer.concat(registryBuffers);

  const itemsAndBlocks = await extractItemsAndBlocks();

  const tagBuffer = serializeTags({
    "fluid": {
      // Water and lava, both flowing and still states
      "water": [ 1, 2 ],
      "lava": [ 3, 4 ]
    },
    "block": {
      "mineable/pickaxe": [
        itemsAndBlocks.blockRegistry["stone"],
        itemsAndBlocks.blockRegistry["stone_slab"],
        itemsAndBlocks.blockRegistry["cobblestone"],
        itemsAndBlocks.blockRegistry["cobblestone_slab"],
        itemsAndBlocks.blockRegistry["sandstone"],
        itemsAndBlocks.blockRegistry["sandstone_slab"],
        itemsAndBlocks.blockRegistry["ice"],
        itemsAndBlocks.blockRegistry["diamond_ore"],
        itemsAndBlocks.blockRegistry["gold_ore"],
        itemsAndBlocks.blockRegistry["redstone_ore"],
        itemsAndBlocks.blockRegistry["iron_ore"],
        itemsAndBlocks.blockRegistry["coal_ore"],
        itemsAndBlocks.blockRegistry["copper_ore"],
        itemsAndBlocks.blockRegistry["furnace"],
        itemsAndBlocks.blockRegistry["iron_block"],
        itemsAndBlocks.blockRegistry["gold_block"],
        itemsAndBlocks.blockRegistry["diamond_block"],
        itemsAndBlocks.blockRegistry["redstone_block"],
        itemsAndBlocks.blockRegistry["coal_block"],
        itemsAndBlocks.blockRegistry["copper_block"]
      ],
      "mineable/axe": [
        itemsAndBlocks.blockRegistry["oak_log"],
        itemsAndBlocks.blockRegistry["oak_planks"],
        itemsAndBlocks.blockRegistry["oak_wood"],
        itemsAndBlocks.blockRegistry["oak_slab"],
        itemsAndBlocks.blockRegistry["crafting_table"],
        itemsAndBlocks.blockRegistry["chest"]
      ],
      "mineable/shovel": [
        itemsAndBlocks.blockRegistry["grass_block"],
        itemsAndBlocks.blockRegistry["dirt"],
        itemsAndBlocks.blockRegistry["sand"],
        itemsAndBlocks.blockRegistry["snow"],
        itemsAndBlocks.blockRegistry["snow_block"],
        itemsAndBlocks.blockRegistry["mud"]
      ],
      "leaves": [
        itemsAndBlocks.blockRegistry["oak_leaves"]
      ]
    },
    "item": {
      "planks": [
        itemsAndBlocks.items["oak_planks"]
      ]
    }
  });

  const networkBlockPalette = toVarIntBuffer(Object.values(itemsAndBlocks.palette));

  const sourceCode = `\
#include <stdint.h>
#include "registries.h"
#include "protocol.h"
_Static_assert(LAPIS_PROTOCOL_VERSION == REGISTRY_PROTOCOL_VERSION, "Registry protocol mismatch");

// Binary contents of required "Registry Data" packets
const uint8_t registries_bin[] = {
${toCArray(fullRegistryBuffer)}
};
// Binary contents of "Update Tags" packets
const uint8_t tags_bin[] = {
${toCArray(tagBuffer)}
};

// Block palette
const uint16_t block_palette[] = { ${Object.values(itemsAndBlocks.palette).join(", ")} };
// Block palette as VarInt buffer
const uint8_t network_block_palette[] = {
${toCArray(networkBlockPalette)}
};

// Block-to-item mapping
static const uint16_t B_to_I[] = { ${itemsAndBlocks.mappingWithOverrides.join(", ")} };
uint16_t registry_block_item (uint32_t id) {
  return id < 256 ? B_to_I[id] : 0;
}
// Item-to-block mapping
uint8_t I_to_B (uint32_t item) {
  switch (item) {
    ${itemsAndBlocks.mapping.map((c, i) => c ? `case ${c}: return ${i};\n    ` : "").join("")}
    default: break;
  }
  return 0;
}
`;

  const headerCode = `\
#ifndef H_REGISTRIES
#define H_REGISTRIES

#define REGISTRY_PROTOCOL_VERSION ${snapshot.protocol}
#define REGISTRY_ITEM_MAX_ID 1415

#include <stdint.h>

// Binary packet data (${fullRegistryBuffer.length + tagBuffer.length} bytes total)
extern const uint8_t registries_bin[${fullRegistryBuffer.length}];
extern const uint8_t tags_bin[${tagBuffer.length}];

extern const uint16_t block_palette[256]; // Block palette
extern const uint8_t network_block_palette[${networkBlockPalette.length}]; // Block palette as VarInt buffer
uint16_t registry_block_item (uint32_t id); // Checked block-to-item mapping
uint8_t I_to_B (uint32_t item); // Item-to-block mapping

// Block identifiers
${Object.keys(itemsAndBlocks.palette).map((c, i) => `#define B_${c} ${i}`).join("\n")}

// Item identifiers
${Object.entries(itemsAndBlocks.items).map(c => `#define I_${c[0]} ${c[1]}`).join("\n")}

// Biome identifiers
${biomes.map((c, i) => `#define W_${c} ${i}`).join("\n")}

// Damage type identifiers
${registries["damage_type"].map((c, i) => `#define D_${c} ${i}`).join("\n")}

#endif
`;

  await fs.writeFile(outputPath, sourceCode);
  await fs.writeFile(headerPath, headerCode);
  console.log("Done. Wrote to `registries.c` and `registries.h`");

}

convert().catch(error => { console.error(error); process.exitCode = 1; });
