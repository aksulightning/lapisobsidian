#!/usr/bin/env python3
"""Pack verified Kenney CC0 tiles, original geometric UI. Recorded audio is built separately.
Requires Pillow only at build time. Never downloads Minecraft resources.
"""
import hashlib
import io
import json
import pathlib
import re
import zipfile
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[1]
SERVER = ROOT.parents[1]
URL = 'https://kenney.nl/media/pages/assets/voxel-pack/a3a73d0ff7-1677662501/kenney_voxel-pack.zip'
LICENSE = 'https://creativecommons.org/publicdomain/zero/1.0/'


def main():
    source = ROOT / 'build/kenney-voxel.zip'
    if not source.exists():
        raise SystemExit(f'Download {URL} to {source} first')
    if hashlib.sha256(source.read_bytes()).hexdigest() != '667c05e3f6d95718aaef888c7fc06f7137ba5dede95f4574deb17d4436257958':
        raise SystemExit('Unexpected Kenney source archive hash')
    archive = zipfile.ZipFile(source)
    license_text = archive.read('License.txt').decode()
    if 'Creative Commons Zero, CC0' not in license_text:
        raise SystemExit('Unexpected asset license')
    dest = ROOT / 'assets'
    (dest / 'licenses').mkdir(exist_ok=True)
    (dest / 'licenses/Kenney-Voxel.txt').write_text(license_text)
    paths = sorted(n for n in archive.namelist() if n.endswith('.png') and n.startswith(('PNG/Tiles/', 'PNG/Items/')))
    # Prefix names to distinguish item and block images such as wheat.
    # Reserve the first 256 cells for the existing Classic/CPE texture layout.
    tile = {path.split('/')[-1][:-4]: i+256 for i, path in enumerate(paths) if path.startswith('PNG/Tiles/')}
    item = {path.split('/')[-1][:-4]: i+256 for i, path in enumerate(paths) if path.startswith('PNG/Items/')}
    atlas = Image.new('RGBA', (512, 1024))
    images = {}
    for i, path in enumerate(paths):
        im = Image.open(io.BytesIO(archive.read(path))).convert('RGBA').resize((32, 32), Image.Resampling.LANCZOS)
        name = path.split('/')[-1][:-4]
        if name in ('water', 'ice'):
            im.putalpha(150 if name == 'water' else 185)
        images[i+256] = im
        atlas.paste(im, ((i+256) % 16 * 32, (i+256) // 16 * 32))
    classic = {0:'grass_top',1:'stone',2:'dirt',3:'dirt_grass',4:'wood',5:'stone',6:'stone',7:'brick_red',
               8:'wood_red',9:'wood_red',10:'wood_red',11:'wood',12:'grass4',13:'grass3',14:'water',15:'grass2',
               16:'rock',17:'greystone',18:'sand',19:'gravel_stone',20:'trunk_side',21:'trunk_top',22:'leaves_transparent',
               23:'stone_silver',24:'stone_gold',25:'sand',28:'mushroom_brown',29:'mushroom_red',30:'lava',
               32:'stone_gold',33:'stone_iron',34:'stone_coal',35:'wood',36:'rock_moss',37:'greystone',38:'lava',
               39:'stone_silver',40:'stone_gold',41:'sand',49:'glass',50:'snow',51:'ice',54:'brick_red',
               55:'stone_silver',56:'stone_gold',57:'sand'}
    for i in range(256):
        im = images[tile[classic.get(i,'stone')]]
        if 64 <= i <= 84:
            # Original flat colors over Kenney's cotton weave for Classic cloth blocks.
            colors=['#cb4747','#dd8b41','#e7d552','#8ac64f','#469855','#53b4a8','#52bfc5','#5595c8',
                    '#4f64b9','#6a53b0','#944fae','#c468ae','#5c646b','#939a9e','#d6dcdb','#e6c8af',
                    '#62824d','#40775f','#77532f','#243551','#15231e']
            from PIL import ImageChops
            im=ImageChops.multiply(images[tile['cotton_tan']],Image.new('RGBA',(32,32),colors[i-64]))
        atlas.paste(im,(i%16*32,i//16*32))
    atlas.save(dest / 'terrain.png')
    (dest / 'atlas.json').write_text(json.dumps({'tile_size':32, 'columns':16, 'classic_slots':classic,
        'tiles':dict(zip(paths,range(256,256+len(paths))))}, indent=2)+'\n')
    # Exact simple geometry, authored in code; deliberately independent UI.
    gui = Image.new('RGBA', (256,256)); d = ImageDraw.Draw(gui)
    d.rectangle((0,0,181,21), fill='#182738', outline='#617d8c', width=1)
    d.rectangle((0,22,23,45), outline='#f5d281', width=2)
    gui.save(dest/'gui.png'); gui.save(dest/'gui_classic.png')
    icons = Image.new('RGBA',(256,256)); d=ImageDraw.Draw(icons)
    d.line((7,3,7,11),fill='white'); d.line((3,7,11,7),fill='white')
    icons.save(dest/'icons.png')
    # One neutral texel for original cuboid entity models, tinted by the renderer.
    Image.new('RGBA',(64,64),'white').save(dest/'lapis-entities.png')
    facts = json.loads((SERVER/'generated/registry_snapshot.json').read_text())
    palette = list(facts['palette'].items())
    # Snapshot palette order is authoritative; generate checked numeric facts only.
    states = [int(x) for x in re.search(r'block_palette\[\] = \{([^}]+)', (SERVER/'src/registries.c').read_text())[1].split(',')]
    names = {int(v):k for k,v in re.findall(r'#define B_(\w+) (\d+)',(SERVER/'include/registries.h').read_text())}
    def texture(name):
        exact = {'grass_block':'dirt_grass','snowy_grass_block':'dirt_snow','dirt':'dirt','coarse_dirt':'gravel_dirt',
                 'bedrock':'greystone','cobblestone':'rock','mossy_cobblestone':'rock_moss','oak_planks':'wood',
                 'oak_log':'trunk_side','oak_leaves':'leaves_transparent','glass':'glass','furnace':'oven',
                 'crafting_table':'table','note_block':'wood_red','jukebox':'wood_red','water':'water','lava':'lava',
                 'wheat':'wheat_stage1','farmland':'dirt','short_grass':'grass1','fern':'grass2','oak_sapling':'grass3',
                 'dead_bush':'grass_brown','sugar_cane':'wheat_stage4','sand':'sand','gravel':'gravel_stone',
                 'coal_ore':'stone_coal','iron_ore':'stone_iron','gold_ore':'stone_gold','diamond_ore':'stone_diamond',
                 'redstone_ore':'greystone_ruby','lapis_ore':'stone_silver','bricks':'brick_red','cactus':'cactus_side',
                 'torch':'wood_red','redstone_torch':'wood_red','red_mushroom':'mushroom_red','brown_mushroom':'mushroom_brown'}
        if name in exact:return exact[name]
        for token, fallback in [('water','water'),('lava','lava'),('leaves','leaves_transparent'),('grass','grass1'),('flower','grass4'),
                                ('wool','cotton_tan'),('snow','snow'),('ice','ice'),('glass','glass'),('rail','track_straight'),
                                ('oak','wood'),('wood','wood'),('sign','wood'),('chest','wood'),('door','wood'),('sand','sand')]:
            if token in name:return fallback
        return 'stone'
    h = ['/* Generated numeric compatibility facts. See tools/build_assets.py. */', '#ifndef LC_FACTS_H', '#define LC_FACTS_H',
         'struct LapisBlockFact { int state, item, side, top, bottom; const char* name; };',
         'static const struct LapisBlockFact Lapis_BlockFacts[256] = {']
    for i,state in enumerate(states):
        name=names[i]; tex=texture(name); top=bottom=tex
        if name=='grass_block':top='grass_top';bottom='dirt'
        if name=='snowy_grass_block':top='snow';bottom='dirt'
        if name=='oak_log':top=bottom='trunk_top'
        if name=='cactus':top='cactus_top';bottom='cactus_inside'
        h.append(f'    {{{state}, {facts["items"].get(name,0)}, {tile[tex]}, {tile[top]}, {tile[bottom]}, "{name}"}},')
    h += ['};','struct LapisItemFact { int id, icon; const char* name; };','static const struct LapisItemFact Lapis_ItemFacts[] = {']
    for name,id in facts['items'].items():
        icon=item.get(name,-1)
        for material, substitute in [('wooden','bronze'),('stone','silver'),('golden','gold'),('iron','iron'),('diamond','diamond'),('netherite','diamond')]:
            for tool, asset in [('sword','sword'),('shovel','shovel'),('pickaxe','pick'),('axe','axe'),('hoe','hoe')]:
                if name==material+'_'+tool:icon=item[asset+'_'+substitute]
        if icon<0:icon=tile[texture(name)]
        h.append(f'    {{{id}, {icon}, "{name}"}},')
    h += ['};',f'#define LAPIS_ITEM_FACT_COUNT {len(facts["items"])}',f'#define LAPIS_TILE_WHEAT {tile["wheat_stage1"]}',
          'static const int Lapis_WheatTiles[4] = {'+','.join(str(tile[f'wheat_stage{i}']) for i in range(1,5))+'};','#endif']
    (ROOT/'src/LapisFacts.h').write_text('\n'.join(h)+'\n')
    # Keep recording provenance intact; use build_audio.py to regenerate WAVs.
    old_manifest = json.loads((dest/'manifest.json').read_text())
    entries=[]
    for path in sorted(dest.glob('*.png'))+[dest/'web-icon.svg']:
        kenney=path.name=='terrain.png'
        entries.append(dict(source_url='https://github.com/aksulightning/lapisobsidian/tree/testing-cube/clients/lapiscube/assets/web-icon.svg' if path.suffix=='.svg' else URL if kenney else 'https://github.com/aksulightning/lapisobsidian/tree/testing-cube/clients/lapiscube/tools/build_assets.py',
                            author='Kenney Vleugels' if kenney else 'LapisCube contributors',license='CC0-1.0',license_url=LICENSE,
                            modifications='Tiles resized 128 to 32, water/ice alpha adjusted, Classic cloth tinted, packed with Classic/CPE compatibility cells; see atlas.json' if kenney else 'Original code-authored geometry or PCM synthesis',
                            destination_filename=str(path.relative_to(ROOT)),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                            attribution='Kenney (kenney.nl)' if kenney else 'LapisCube',redistribution_notes='CC0; attribution appreciated, not required'))
    entries += [a for a in old_manifest['assets'] if a['destination_filename'].startswith('assets/audio/')]
    (dest/'manifest.json').write_text(json.dumps(dict(schema_version=1,project='LapisCube',default_asset_license='CC0-1.0',status=old_manifest['status'],assets=entries),indent=2)+'\n')
    (dest/'source.json').write_text(json.dumps(dict(url=URL,sha256=hashlib.sha256(source.read_bytes()).hexdigest(),license_file='licenses/Kenney-Voxel.txt'),indent=2)+'\n')
    pack=ROOT/'build/engine/texpacks';pack.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(pack/'default.zip','w',zipfile.ZIP_DEFLATED) as out:
        for path in sorted(dest.glob('*.png')):out.write(path,path.name)
    print(f'Packed {len(paths)} CC0 tiles; wrote compatibility facts and {len(entries)} manifest entries')


if __name__=='__main__':main()
