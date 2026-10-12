"""Original CC0 pixel geometry for LapisCube's tool and block texture family.

No external image inputs. Keep these deterministic so atlas builds are repeatable.
"""
from PIL import Image, ImageDraw
import random


def grain(color, seed, spread=9):
    rng = random.Random(seed)
    im = Image.new('RGBA', (16, 16))
    for y in range(16):
        for x in range(16):
            n = rng.randrange(-spread, spread+1)
            im.putpixel((x, y), tuple(max(0, min(255, c+n)) for c in color)+(255,))
    return im


def textures():
    out = {}
    materials = {'wooden': ('#9c6b3f', '#e2b97b'), 'stone': ('#777e85', '#c4cbd0'),
                 'iron': ('#b4bdc4', '#f0f4ed'), 'golden': ('#d79925', '#fff0a0'),
                 'diamond': ('#279eae', '#a1f4e6'), 'netherite': ('#534652', '#a89096')}
    for material, (body, edge) in materials.items():
        for tool in ('pickaxe', 'axe', 'shovel', 'hoe'):
            im = Image.new('RGBA', (16, 16)); d = ImageDraw.Draw(im)
            d.line((3, 13, 10, 6), fill='#342c2b', width=3)
            d.line((3, 13, 10, 6), fill='#a47a47', width=1)
            points = {'pickaxe': [(3,3),(6,1),(10,2),(14,6),(14,10),(12,8),(11,5),(7,4)],
                      'axe': [(7,1),(11,2),(14,5),(12,8),(8,7),(6,5)],
                      'shovel': [(10,1),(14,1),(15,5),(12,8),(8,4)],
                      'hoe': [(4,2),(8,1),(12,3),(13,6),(10,6),(9,4),(4,4)]}[tool]
            d.polygon(points, fill=body, outline='#282a32')
            d.line(points[:3], fill=edge, width=1)
            out[material+'_'+tool] = im
    for name, color in [('stone',(112,119,126)), ('cobblestone',(112,119,126)),
                        ('obsidian',(43,32,62)), ('oak_planks',(155,112,65)),
                        ('oak_log',(100,71,43)), ('log_top',(171,129,77)),
                        ('bricks',(150,76,58)), ('furnace_side',(100,108,113)),
                        ('furnace_front',(100,108,113)), ('crafting_side',(137,92,49)),
                        ('crafting_top',(167,123,70)), ('chest_side',(155,109,49)),
                        ('chest_front',(155,109,49)), ('bookshelf',(155,112,65)),
                        ('sandstone',(201,180,125)), ('lapis_block',(38,76,157))]:
        im = grain(color, name); d = ImageDraw.Draw(im)
        if name in ('cobblestone','bricks','sandstone'):
            for y in range(0,16,4):
                d.line((0,y,15,y), fill='#514e4a' if name!='sandstone' else '#9e8a59')
                for x in range((y//4%2)*4,16,8):
                    d.line((x,y,x,y+3), fill='#514e4a' if name!='sandstone' else '#9e8a59')
                    d.line((x+1,y+1,min(x+7,15),y+1), fill=tuple(min(255,c+25) for c in color))
        if name=='oak_planks':
            for y in (3,7,11,15): d.line((0,y,15,y),fill='#6a482f')
            for x,y in ((4,0),(11,4),(6,8),(13,12)): d.line((x,y,x,y+3),fill='#795235')
        if name=='oak_log':
            for x in (1,4,8,12,15): d.line((x,0,x,15),fill='#513b2b');d.line((x-1,0,x-1,15),fill='#987044')
        if name=='log_top':
            for n in (1,4,6): d.rectangle((n,n,15-n,15-n),outline='#765332')
        if name.startswith('furnace'):
            d.rectangle((0,0,15,15),outline='#42494f')
            if name.endswith('front'):
                d.rectangle((3,3,12,6),fill='#252d33'); d.rectangle((2,9,13,13),fill='#242a30')
                d.line((3,14,12,14),fill='#b0b8b9')
        if name.startswith(('chest','crafting')) or name=='bookshelf':
            d.rectangle((0,0,15,15),outline='#503b2d',width=2)
            if name=='crafting_top':
                for n in (5,10): d.line((n,2,n,13),fill='#67492f');d.line((2,n,13,n),fill='#67492f')
            elif name=='crafting_side':
                d.rectangle((3,3,12,10),fill='#bf995d');d.line((4,4,10,9),fill='#544037',width=2)
            elif name=='bookshelf':
                for y in (2,9):
                    for x,c in zip((2,5,8,11),('#547d91','#a34e45','#6b8e51','#d2b66e')):
                        d.rectangle((x,y,x+1,y+4),fill=c)
            else:
                d.line((1,5,14,5),fill='#503b2d')
                if name=='chest_front':d.rectangle((7,4,8,8),fill='#e3d398')
        out[name] = im
    for ore,color in [('coal','#34383b'),('iron','#d0a489'),('gold','#eed05c'),
                      ('diamond','#66e1dd'),('redstone','#d7504b'),('lapis','#487dc9')]:
        im=out['stone'].copy();d=ImageDraw.Draw(im)
        for x,y in ((2,3),(10,2),(6,8),(12,11),(2,13)):
            d.rectangle((x,y,x+2,y+1),fill=color); d.point((x,y),fill='#e3e1d4')
        out[ore+'_ore']=im
    return out
