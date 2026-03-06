#include "textureatlas.h"
#include <cmath>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
// Pixel helpers
// ─────────────────────────────────────────────────────────────────────────────
struct RGBA { uint8_t r,g,b,a; };

static void setPixel(std::vector<uint8_t>& img, int x, int y, RGBA c) {
    if (x<0||x>=ATLAS_W||y<0||y>=ATLAS_H) return;
    int i = (y*ATLAS_W + x)*4;
    img[i]=c.r; img[i+1]=c.g; img[i+2]=c.b; img[i+3]=c.a;
}

static RGBA getPixel(const std::vector<uint8_t>& img, int x, int y) {
    int i = (y*ATLAS_W + x)*4;
    return {img[i], img[i+1], img[i+2], img[i+3]};
}

// Blend colour by amount [0-255]
static RGBA blend(RGBA base, RGBA over, uint8_t amt) {
    float t = amt/255.f;
    return {
        (uint8_t)(base.r*(1-t)+over.r*t),
        (uint8_t)(base.g*(1-t)+over.g*t),
        (uint8_t)(base.b*(1-t)+over.b*t),
        255
    };
}

// Simple LCG noise for pixel variation
static uint32_t lcg(uint32_t s) { return s*1664525u+1013904223u; }
static float pnoise(int x, int y, int seed=0) {
    // Use unsigned arithmetic to avoid signed-integer-overflow UB (callers
    // pass values like x*2 which can exceed INT_MAX at TILE_SIZE-1).
    uint32_t s = lcg((uint32_t)x*73856093u ^ (uint32_t)y*19349663u ^ (uint32_t)seed);
    return (float)(s&0xFF)/255.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tile painters — each draws into a 16x16 region at (ox,oy)
// ─────────────────────────────────────────────────────────────────────────────

// Fill rect with solid colour + per-pixel noise variation
static void fillNoisy(std::vector<uint8_t>& img, int ox, int oy,
                      RGBA base, float noiseAmt, int seed=0) {
    for (int y=0; y<TILE_SIZE; y++) {
        for (int x=0; x<TILE_SIZE; x++) {
            float n = pnoise(x,y,seed)*noiseAmt;
            RGBA c = {
                (uint8_t)std::min(255,(int)(base.r*(1.f+n-noiseAmt*0.5f))),
                (uint8_t)std::min(255,(int)(base.g*(1.f+n-noiseAmt*0.5f))),
                (uint8_t)std::min(255,(int)(base.b*(1.f+n-noiseAmt*0.5f))),
                255
            };
            setPixel(img, ox+x, oy+y, c);
        }
    }
}

static void paintGrassTop(std::vector<uint8_t>& img, int ox, int oy) {
    RGBA base={88,172,55,255};
    fillNoisy(img,ox,oy,base,0.25f,1);
    // Darker patches
    for (int y=0;y<TILE_SIZE;y++)
        for (int x=0;x<TILE_SIZE;x++) {
            float n=pnoise(x+3,y+7,2);
            if(n>0.72f) {
                RGBA p=getPixel(img,ox+x,oy+y);
                setPixel(img,ox+x,oy+y,{(uint8_t)(p.r*0.78f),(uint8_t)(p.g*0.78f),(uint8_t)(p.b*0.78f),255});
            }
        }
}

static void paintGrassSide(std::vector<uint8_t>& img, int ox, int oy) {
    // Bottom 12 rows = dirt, top 4 rows = grass strip
    RGBA dirt={134,96,67,255};
    RGBA grass={88,172,55,255};
    fillNoisy(img,ox,oy,dirt,0.2f,3);
    for (int y=0;y<4;y++)
        for (int x=0;x<TILE_SIZE;x++) {
            float n=pnoise(x,y,5)*0.2f;
            RGBA c={(uint8_t)(grass.r*(1.f+n)),(uint8_t)(grass.g*(1.f+n)),(uint8_t)(grass.b*(1.f+n)),255};
            setPixel(img,ox+x,oy+y,c);
        }
    // Transition row
    for (int x=0;x<TILE_SIZE;x++) {
        RGBA d=getPixel(img,ox+x,oy+4);
        setPixel(img,ox+x,oy+4,blend(d,grass,120));
    }
}

static void paintDirt(std::vector<uint8_t>& img, int ox, int oy) {
    RGBA base={134,96,67,255};
    fillNoisy(img,ox,oy,base,0.22f,7);
    // Small pebble dots
    for (int y=0;y<TILE_SIZE;y++)
        for (int x=0;x<TILE_SIZE;x++) {
            if(pnoise(x+1,y+9,8)>0.84f)
                setPixel(img,ox+x,oy+y,{100,72,50,255});
        }
}

static void paintStone(std::vector<uint8_t>& img, int ox, int oy) {
    RGBA base={128,128,128,255};
    fillNoisy(img,ox,oy,base,0.15f,11);
    // Crack lines
    for (int x=0;x<TILE_SIZE;x++) {
        if(pnoise(x,0,12)>0.88f)
            for (int y=0;y<TILE_SIZE;y++)
                if(pnoise(x,y,13)<0.3f) {
                    RGBA p=getPixel(img,ox+x,oy+y);
                    setPixel(img,ox+x,oy+y,{(uint8_t)(p.r*0.72f),(uint8_t)(p.g*0.72f),(uint8_t)(p.b*0.72f),255});
                }
    }
}

static void paintBedrock(std::vector<uint8_t>& img, int ox, int oy) {
    RGBA base={32,32,32,255};
    fillNoisy(img,ox,oy,base,0.3f,15);
    // Irregular blotches
    for (int y=0;y<TILE_SIZE;y++)
        for (int x=0;x<TILE_SIZE;x++) {
            float n=pnoise(x*2,y*2,16);
            if(n>0.65f) setPixel(img,ox+x,oy+y,{18,18,18,255});
            else if(n<0.2f) setPixel(img,ox+x,oy+y,{55,55,55,255});
        }
}

static void paintWoodTop(std::vector<uint8_t>& img, int ox, int oy) {
    RGBA base={160,120,60,255};
    fillNoisy(img,ox,oy,base,0.1f,17);
    // Concentric rings
    for (int y=0;y<TILE_SIZE;y++)
        for (int x=0;x<TILE_SIZE;x++) {
            float cx=(x-7.5f), cy=(y-7.5f);
            float r=sqrtf(cx*cx+cy*cy);
            float ring=fmodf(r,3.0f);
            if(ring<0.8f) {
                RGBA p=getPixel(img,ox+x,oy+y);
                setPixel(img,ox+x,oy+y,{(uint8_t)(p.r*0.82f),(uint8_t)(p.g*0.82f),(uint8_t)(p.b*0.78f),255});
            }
        }
}

static void paintWoodSide(std::vector<uint8_t>& img, int ox, int oy) {
    RGBA base={150,100,45,255};
    // Vertical grain stripes
    for (int y=0;y<TILE_SIZE;y++)
        for (int x=0;x<TILE_SIZE;x++) {
            float n=pnoise(x,y*3,18)*0.2f;
            float stripe=fmodf((float)x+pnoise(x,y,19)*1.5f,4.f);
            float dark = (stripe<0.6f)?0.82f:1.f;
            RGBA c={
                (uint8_t)std::min(255,(int)(base.r*dark*(1.f+n))),
                (uint8_t)std::min(255,(int)(base.g*dark*(1.f+n))),
                (uint8_t)std::min(255,(int)(base.b*dark*(1.f+n))),
                255
            };
            setPixel(img,ox+x,oy+y,c);
        }
}

static void paintLeaves(std::vector<uint8_t>& img, int ox, int oy) {
    RGBA dark ={38,110,28,255};
    RGBA mid  ={55,148,38,255};
    RGBA light={72,180,50,255};

    for (int y=0; y<TILE_SIZE; y++) {
        for (int x=0; x<TILE_SIZE; x++) {
            float n  = pnoise(x,    y,    20);
            float n2 = pnoise(x+8,  y+8,  21);
            float nv = pnoise(x*2,  y*2,  22)*0.15f;
            // Base colour — all pixels get some leaf colour
            RGBA c = n2>0.6f ? light : n2>0.35f ? mid : dark;
            // Darker variation on low-noise pixels
            if (n < 0.35f) {
                c.r = (uint8_t)(c.r * 0.7f);
                c.g = (uint8_t)(c.g * 0.7f);
                c.b = (uint8_t)(c.b * 0.7f);
            }
            c.r = (uint8_t)std::min(255,(int)(c.r*(1.f+nv)));
            c.g = (uint8_t)std::min(255,(int)(c.g*(1.f+nv)));
            c.a = 255;
            setPixel(img, ox+x, oy+y, c);
        }
    }
}

static void paintSnow(std::vector<uint8_t>& img, int ox, int oy) {
    RGBA base={235,242,250,255};
    fillNoisy(img,ox,oy,base,0.08f,23);
    for (int y=0;y<TILE_SIZE;y++)
        for (int x=0;x<TILE_SIZE;x++) {
            if(pnoise(x+2,y+4,24)>0.78f) {
                RGBA p=getPixel(img,ox+x,oy+y);
                setPixel(img,ox+x,oy+y,{(uint8_t)(p.r*0.88f),(uint8_t)(p.g*0.90f),(uint8_t)(p.b*0.97f),255});
            }
        }
}

// Snow strip on top (3px), stone body below — sides of snow-capped peaks
static void paintSnowSide(std::vector<uint8_t>& img, int ox, int oy) {
    // Stone body first
    paintStone(img, ox, oy);
    // Snow strip across the top 3 rows
    RGBA snowBase = {235,242,250,255};
    for (int y=0; y<3; y++) {
        for (int x=0; x<TILE_SIZE; x++) {
            float n = pnoise(x, y, 25) * 0.08f;
            RGBA c = {
                (uint8_t)std::min(255,(int)(snowBase.r*(1.f+n))),
                (uint8_t)std::min(255,(int)(snowBase.g*(1.f+n))),
                (uint8_t)std::min(255,(int)(snowBase.b*(1.f+n))),
                255
            };
            setPixel(img, ox+x, oy+y, c);
        }
    }
    // Soft blend on transition row
    for (int x=0; x<TILE_SIZE; x++) {
        RGBA snow  = getPixel(img, ox+x, oy+2);
        RGBA stone = getPixel(img, ox+x, oy+3);
        setPixel(img, ox+x, oy+3, blend(stone, snow, 100));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Tiles
// ─────────────────────────────────────────────────────────────────────────────
static void paintUISlot(std::vector<uint8_t>& img, int ox, int oy) {
    RGBA base={139,139,139,255}; // lighter gray
    RGBA shadow={55,55,55,255};  // dark gray
    RGBA light={255,255,255,255}; // white highlights

    // Main body
    for (int y=1; y<TILE_SIZE-1; y++)
        for (int x=1; x<TILE_SIZE-1; x++)
            setPixel(img, ox+x, oy+y, base);

    // Outer border (shadow)
    for (int i=0; i<TILE_SIZE; i++) {
        setPixel(img, ox+i, oy, shadow);
        setPixel(img, ox+i, oy+TILE_SIZE-1, shadow);
        setPixel(img, ox, oy+i, shadow);
        setPixel(img, ox+TILE_SIZE-1, oy+i, shadow);
    }

    // Inner highlights/shadows
    for (int i=1; i<TILE_SIZE-1; i++) {
        setPixel(img, ox+i, oy+1, shadow); // top shadow
        setPixel(img, ox+1, oy+i, shadow); // left shadow
        setPixel(img, ox+i, oy+TILE_SIZE-2, light); // bottom light
        setPixel(img, ox+TILE_SIZE-2, oy+i, light); // right light
    }
}

static void paintUISelector(std::vector<uint8_t>& img, int ox, int oy) {
    RGBA white={255,255,255,255};
    RGBA dark={55,55,55,255};

    // Thick white border with highlights
    for (int y=0; y<TILE_SIZE; y++) {
        for (int x=0; x<TILE_SIZE; x++) {
            bool edge = (x<2 || x>=TILE_SIZE-2 || y<2 || y>=TILE_SIZE-2);
            if (edge) {
                // outer blackish pixel
                bool outer = (x==0 || x==TILE_SIZE-1 || y==0 || y==TILE_SIZE-1);
                setPixel(img, ox+x, oy+y, outer ? dark : white);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint8_t> generateAtlas() {
    std::vector<uint8_t> img(ATLAS_W * ATLAS_H * 4, 0);

    paintGrassTop (img, (int)TileID::GrassTop  * TILE_SIZE, 0);
    paintGrassSide(img, (int)TileID::GrassSide * TILE_SIZE, 0);
    paintDirt     (img, (int)TileID::Dirt      * TILE_SIZE, 0);
    paintStone    (img, (int)TileID::Stone     * TILE_SIZE, 0);
    paintBedrock  (img, (int)TileID::Bedrock   * TILE_SIZE, 0);
    paintWoodTop  (img, (int)TileID::WoodTop   * TILE_SIZE, 0);
    paintWoodSide (img, (int)TileID::WoodSide  * TILE_SIZE, 0);
    paintLeaves   (img, (int)TileID::Leaves    * TILE_SIZE, 0);
    paintSnow     (img, (int)TileID::Snow      * TILE_SIZE, 0);
    paintSnowSide (img, (int)TileID::SnowSide  * TILE_SIZE, 0);
    paintUISlot   (img, (int)TileID::UISlot    * TILE_SIZE, 0);
    paintUISelector(img, (int)TileID::UISelector * TILE_SIZE, 0);

    return img;
}
