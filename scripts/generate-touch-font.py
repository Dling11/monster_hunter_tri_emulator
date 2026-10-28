#!/usr/bin/env python3
"""Bake the licensed Inter font and small line icons into one 512 KiB atlas.

Requires Pillow only when regenerating. The application uses the generated
header and does not load TTFs, download fonts or rasterize text at runtime.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
WIDTH, HEIGHT, SIZE = 1024, 512, 44
CHARS = sorted(set(chr(c) for c in range(32, 127)) |
               set('áéíóúñÁÉÍÓÚÑ¿¡äöüÄÖÜßàèêçìòù•‹›–—−×…°'))


def icon(number):
    image = Image.new('L', (176, 176))
    d = ImageDraw.Draw(image)
    def line(points, width=9):
        d.line(points, fill=255, width=width, joint='curve')
        r = width / 2
        for x, y in (points[0], points[-1]): d.ellipse((x-r, y-r, x+r, y+r), fill=255)
    def rect(bounds, radius=10): d.rounded_rectangle(bounds, radius, outline=255, width=9)
    if number < 4:
        vertices = [(88, 27), (145, 141), (31, 141)]
        angle = number * math.pi / 2
        points = [(88+(x-88)*math.cos(angle)-(y-88)*math.sin(angle),
                   88+(x-88)*math.sin(angle)+(y-88)*math.cos(angle)) for x,y in vertices]
        d.polygon(points, fill=255)
    elif number == 4: # Home
        line([(25, 80), (88, 25), (151, 80)])
        line([(42, 70), (42, 145), (74, 145), (74, 106), (102, 106), (102, 145), (134, 145), (134, 70)])
    elif number == 5: # Gear
        d.ellipse((45,45,131,131),outline=255,width=9)
        d.ellipse((72,72,104,104),outline=255,width=8)
        for i in range(8):
            a=i*math.pi/4
            line([(88+44*math.cos(a),88+44*math.sin(a)),(88+59*math.cos(a),88+59*math.sin(a))])
    elif number == 6: # Display
        rect((24,33,152,119));line([(88,120),(88,144)]);line([(59,145),(117,145)])
    elif number == 7: # System sliders
        for x,cy in ((43,65),(88,113),(133,74)):
            line([(x,30),(x,cy-15)]);line([(x,cy+15),(x,148)])
            d.ellipse((x-15,cy-15,x+15,cy+15),outline=255,width=8)
    elif number == 8: # Save
        line([(33,27),(125,27),(148,50),(148,148),(28,148),(28,27),(33,27)])
        rect((53,27,112,69),4);rect((53,102,123,148),4)
    elif number == 9: # Gamepad
        line([(53,49),(123,49),(143,68),(155,127),(145,140),(126,137),(108,116),(68,116),(50,137),(31,140),(21,127),(33,68),(53,49)])
        line([(42,82),(72,82)]);line([(57,67),(57,97)])
        d.ellipse((119,68,131,80),fill=255);d.ellipse((131,83,143,95),fill=255)
    elif number == 10: # Network
        for box in ((21,34,155,153),(42,64,134,153),(62,94,114,153)):
            d.arc(box,215,325,fill=255,width=9)
        d.ellipse((82,133,94,145),fill=255)
    elif number == 11: # Combo layers
        line([(27,64),(88,29),(149,64),(88,99),(27,64)])
        line([(27,89),(88,124),(149,89)]);line([(27,115),(88,150),(149,115)])
    elif number == 12: # Resume arrow
        line([(38,88),(141,88)]);line([(103,49),(143,88),(103,127)])
    return image.resize((SIZE,SIZE),Image.Resampling.LANCZOS)


def generate(output, preview=None):
    font_path=ROOT/'third_party/inter/InterVariable.ttf'
    source=json.loads((font_path.parent/'source.json').read_text(encoding='utf-8'))
    if hashlib.sha256(font_path.read_bytes()).hexdigest()!=source['sha256']:
        raise SystemExit('Inter font differs from its pinned official source.')
    atlas=Image.new('L',(WIDTH,HEIGHT));x=y=2;row_h=0
    def place(image):
        nonlocal x,y,row_h
        w,h=image.size
        if x+w+2>WIDTH: x=2;y+=row_h+2;row_h=0
        if y+h+2>HEIGHT: raise SystemExit('Font atlas overflow; keep the fixed texture budget.')
        atlas.paste(image,(x,y));pos=(x/WIDTH,y/HEIGHT,(x+w)/WIDTH,(y+h)/HEIGHT)
        x+=w+2;row_h=max(row_h,h)
        return pos
    faces=[]
    for weight in (400,600):
        font=ImageFont.truetype(str(font_path),SIZE)
        font.set_variation_by_axes([14,weight])
        cap_top=font.getbbox('H')[1]
        glyphs=[]
        for ch in CHARS:
            left,top,right,bottom=font.getbbox(ch)
            w,h=max(1,right-left+4),max(1,bottom-top+4)
            image=Image.new('L',(w,h));ImageDraw.Draw(image).text((2-left,2-top),ch,font=font,fill=255)
            uv=place(image)
            blank=image.getbbox() is None
            glyphs.append((ord(ch),*uv,0 if blank else w,0 if blank else h,
                           0 if blank else left-2,0 if blank else top-cap_top-2,font.getlength(ch)))
        faces.append(glyphs)
    for cp,number in [(0x25B2,0),(0x25B6,1),(0x25BC,2),(0x25C0,3),(0x2302,4),(0x2699,5)]+[(0xE100+i,6+i) for i in range(7)]:
        image=Image.new('L',(SIZE+4,SIZE+4));image.paste(icon(number),(2,2));uv=place(image)
        metric=(cp,*uv,SIZE+4,SIZE+4,-2,-2,SIZE)
        for face in faces:face.append(metric)
    raw=atlas.tobytes();rle=[];i=0
    while i<len(raw):
        value=raw[i];count=1
        while i+count<len(raw) and raw[i+count]==value and count<255:count+=1
        rle.extend((count,value));i+=count
    lines=['// Generated by scripts/generate-touch-font.py. Do not edit.',
           '// Inter copyright: The Inter Project Authors; SIL OFL 1.1.',
           '// Full license: LICENSES/Inter-OFL.txt (APK: assets/licenses/Inter-OFL.txt).',
           '#pragma once','#include <cstdint>','#include <cstddef>',
           f'constexpr size_t FONT_ATLAS_WIDTH={WIDTH};',f'constexpr size_t FONT_ATLAS_HEIGHT={HEIGHT};',
           f'constexpr float BASE_FONT_SIZE={SIZE}.0f;',f'constexpr size_t FONT_RAW_SIZE={len(raw)};',
           'struct FontGlyphMetric { uint32_t codepoint; float u0,v0,u1,v1,w,h,offX,offY,advance; };']
    for name,face in zip(('Regular','Semibold'),faces):
        lines.append(f'inline constexpr FontGlyphMetric g_font{name}[]={{')
        for g in sorted(face):lines.append(' {'+str(g[0])+','+','.join(f'{v:.7f}f' for v in g[1:])+'},')
        lines.append('};')
    lines+=['inline constexpr uint8_t g_fontAtlasRLE[]={']
    for i in range(0,len(rle),32):lines.append(' '+','.join(str(v) for v in rle[i:i+32])+',')
    lines+=['};','inline const FontGlyphMetric* FindFontGlyph(uint32_t cp,bool strong=false) {',
            ' const auto* table=strong?g_fontSemibold:g_fontRegular;',
            ' size_t first=0,last=sizeof(g_fontRegular)/sizeof(g_fontRegular[0]);',
            ' while(first<last){const size_t mid=(first+last)/2;if(table[mid].codepoint<cp)first=mid+1;else last=mid;}',
            ' if(first<sizeof(g_fontRegular)/sizeof(g_fontRegular[0]) && table[first].codepoint==cp)return table+first;',
            ' return cp==63?nullptr:FindFontGlyph(63,strong);','}',
            'inline void DecompressFontAtlas(uint8_t* dst) {',
            ' size_t out=0;for(size_t i=0;i<sizeof(g_fontAtlasRLE);i+=2)',
            ' for(unsigned j=0;j<g_fontAtlasRLE[i] && out<FONT_RAW_SIZE;++j)dst[out++]=g_fontAtlasRLE[i+1];','}']
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
    if preview:atlas.save(preview)
    print(f'Inter Regular/Semibold: {len(faces[0])} glyphs per face, {len(rle)} compressed bytes, {WIDTH}x{HEIGHT} single-channel texture ({len(raw)//1024} KiB).')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'android-source/runtime/include/rowell_ui_atlas.h')
    parser.add_argument('--preview',type=Path)
    args=parser.parse_args();generate(args.output,args.preview)
