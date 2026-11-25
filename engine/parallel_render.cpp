#include <QThreadPool>
#include <QObject>
#include <QDebug>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include "parallel_render.h"
#include "shader_interface.h"
#include "utils.h"

using namespace std;

Tile tiles[tileBufferH][tileBufferW];
RenderTaskDispatcher taskDispatcher(threadCount);

using namespace ShaderInternal;

int tileLevelIterate(const Triangle2D &frag, int tileXlt, int tileYlt){
    int flags[4][3], innerFlag = true;
    for(int i=0;i<2;i++){
        for(int j=0;j<2;j++){
            EdgeIterator it = frag.edgeIterator;
            it.batchIterate(tileXlt + i*(tileSize-1), tileYlt + j*(tileSize-1));
            for(int k=0;k<3;k++){
                flags[i*2+j][k] = (it.e[k].val >= 0);
            }
            if(it.check() == EdgeIterator::OUTER) innerFlag = false;
        }
    }
    for(int i=0;i<3;i++){
        bool flag = true;
        for(int j=0;j<4;j++){
            if(flags[j][i] == 1){
                flag = false;
                break;
            }
        }
        if(flag) return TileLevelResult::OUTER;
    }
    if(innerFlag)
        return TileLevelResult::INNER;
    return TileLevelResult::UNKNOWN;
}

std::atomic<int> tmp1 = 0, tmp2=0, tmp3=0;

// TLS counters for OUTER-pixel statistics; merged into frameStat once per tile.
thread_local uint64_t tlPixelVisited = 0;

static const bool useSpanRaster = []{
    const char *v = std::getenv("PIG4_SPAN_RASTER");
    return !v || std::strcmp(v, "0") != 0;
}();

void RenderTask::operator()(){
    const auto cpuStart = chrono::steady_clock::now();

    tile->zInvMin = 1e9f;
    tile->cpCount = 0;

    for(int y=0;y<tileSize;y++)
        for(int x=0;x<tileSize;x++){
            tile->triangleID[y][x] = 0x80000000;
        }
    // memset(tile->triangleID, 0x80, sizeof(tile->triangleID));
    memset(tile->zInv, 0, sizeof(tile->zInv));
    memset(tile->vis, 0, sizeof(tile->vis));


    sort(fragments.begin(), fragments.end(), [](const Triangle2D *a, const Triangle2D *b){
        return maxZInv[a->triangleID] > maxZInv[b->triangleID];
    });

    for(const Triangle2D *ptr:fragments){
        int tileLevelResult = TileLevelResult::UNKNOWN;

        if(ptr->xlt <= tile->tileX*tileSize
            && ptr->ylt <= tile->tileY*tileSize
            && ptr->xrb > (tile->tileX+1)*tileSize
            && ptr->yrb > (tile->tileY+1)*tileSize)
            tileLevelResult = tileLevelIterate(*ptr, tile->tileX*tileSize, tile->tileY*tileSize);
        uint shaderConfig = ShaderInternal::triangles[ptr->triangleID].shaderConfig;

        if(shaderConfig & ShaderConfig::WireframeOnly)
            tileRasterization<WireframeShader>(*ptr, *tile, tileLevelResult);
        else if(shaderConfig & ShaderConfig::Highlight)
            tileRasterization<HighlightShader>(*ptr, *tile, tileLevelResult);
        else if(shaderConfig & ShaderConfig::UseAlphaBitmap && !useSpanRaster)
            tileRasterization<AlphaTestShader>(*ptr, *tile, tileLevelResult);
        else if(shaderConfig & ShaderConfig::UseAlphaBitmap){
            if(useSpanRaster) spanTileRasterization<AlphaTestShader>(*ptr, *tile, tileLevelResult);
            else tileRasterization<AlphaTestShader>(*ptr, *tile, tileLevelResult);
        }else{
            if(useSpanRaster) spanTileRasterization<BaseShader>(*ptr, *tile, tileLevelResult);
            else tileRasterization<BaseShader>(*ptr, *tile, tileLevelResult);
        }

    }

    const auto rasterEnd = chrono::steady_clock::now();

    int tileXlt = tile->tileX * tileSize;
    int tileYlt = tile->tileY * tileSize;

    // 调试：在归一化之前抄一份 tile 中实际被 alpha-test 接受的像素。
    // 用来和独立调试 pass 的预期结果对比。
    if(ShaderInternal::debugStage1Capture
        && ShaderInternal::debugActualUV.size() == size_t(ShaderInternal::pixelW) * ShaderInternal::pixelH){
        for(int y=0;y<tileSize;y++){
            int globalY = tileYlt + y;
            for(int x=0;x<tileSize;x++){
                uint triangleID = tile->triangleID[y][x];
                if(triangleID >= 0x80000000u || tile->zInv[y][x] <= 0.0f) continue;
                if(!(ShaderInternal::triangles[triangleID].shaderConfig & ShaderConfig::UseAlphaBitmap)) continue;

                const Material &material = assetManager.getMaterials()[ShaderInternal::triangles[triangleID].materialID];
                if(material.mipmap2.empty()) continue;
                const TextureMap &texture = material.mipmap2[0];
                const int w = texture.w, h = texture.h;

                const float invZ = 1.0f / tile->zInv[y][x];
                const float u = tile->u_z[y][x] * invZ;
                const float v = tile->v_z[y][x] * invZ;
                const uint tx = uint(int(u * w)) & uint(w - 1);
                const uint ty = uint(int(v * h)) & uint(h - 1);
                const uint c00 = texture.pixel(tx, ty);
                const size_t idx = size_t(globalY) * size_t(ShaderInternal::pixelW)
                                 + size_t(tileXlt + x);

                const uint ru = uint(std::clamp(u, 0.0f, 1.0f) * 255.0f);
                const uint gv = uint(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                ShaderInternal::debugActualUV[idx] = 0xff000000u | (ru << 16) | (gv << 8);
                const uint alpha = c00 >> 24;
                ShaderInternal::debugActualAlpha[idx] = 0xff000000u
                                                      | (alpha << 16) | (alpha << 8) | alpha;
                ShaderInternal::debugActualTex[idx] = 0xff000000u | (c00 & 0x00ffffffu);
            }
        }
    }

    for(int y=0;y<tileSize;y++){
        for(int x=0;x<tileSize;x++){
            if(tile->triangleID[y][x] < 0x80000000u){
                tile->u_z[y][x] /= tile->zInv[y][x];
                tile->v_z[y][x] /= tile->zInv[y][x];
            }
        }
    }
    const auto normalizeEnd = chrono::steady_clock::now();

    for(int y=0;y<tileSize;y++){
        int globalY = tileYlt+y;
        int bufferBase = globalY * ShaderInternal::pixelW;
        for(int x=0;x<tileSize;x++){
            uint colorRef = 0xff000000;
            if(tile->triangleID[y][x] < 0x80000000u){

                float u = tile->u_z[y][x];
                float v = tile->v_z[y][x];

                uint shaderConfig = ShaderInternal::triangles[tile->triangleID[y][x]].shaderConfig;

                float d = 0.0f;
                if(!(shaderConfig & ShaderConfig::DisableMipmap)){
                    float ux = 0.0f, vx = 0.0f, uy = 0.0f, vy = 0.0f;

                    if(x+1 < tileSize){
                        ux = tile->u_z[y][x+1] - u;
                        vx = tile->v_z[y][x+1] - v;
                    }else if(x > 0){
                        ux = tile->u_z[y][x-1] - u;
                        vx = tile->v_z[y][x-1] - v;
                    }
                    if(y+1 < tileSize){
                        uy = tile->u_z[y+1][x] - u;
                        vy = tile->v_z[y+1][x] - v;
                    }else if(y > 0){
                        uy = tile->u_z[y-1][x] - u;
                        vy = tile->v_z[y-1][x] - v;
                    }
                    d = sqrt(max(ux*ux+vx*vx, uy*uy+vy*vy));
                    // tile->derivative[y][x] = d;

                }

                if(shaderConfig & ShaderConfig::WireframeOnly)
                    colorRef = colorDetermination<WireframeShader>(u, v, tile->triangleID[y][x], {1,0,0}, d);
                else if(shaderConfig & ShaderConfig::Highlight)
                    colorRef = colorDetermination<HighlightShader>(u, v, tile->triangleID[y][x], {1,0,0}, d);
                else if(shaderConfig & ShaderConfig::UseAlphaBitmap)
                    colorRef = colorDetermination<AlphaTestShader>(u, v, tile->triangleID[y][x], {1,0,0}, d);
                else
                    colorRef = colorDetermination<BaseShader>(u, v, tile->triangleID[y][x], {1,0,0}, d);
            }
            int globalX = tileXlt+x;
            ShaderInternal::buffer[bufferBase + globalX] = colorRef;
        }
    }

    const auto shadeEnd = chrono::steady_clock::now();
    const auto us = [](auto a, auto b){
        return uint64_t(chrono::duration_cast<chrono::microseconds>(b - a).count());
    };
    frameStat.rasterUs.fetch_add(us(cpuStart, rasterEnd), memory_order_relaxed);
    frameStat.normalizeUs.fetch_add(us(rasterEnd, normalizeEnd), memory_order_relaxed);
    frameStat.shadeUs.fetch_add(us(normalizeEnd, shadeEnd), memory_order_relaxed);

    frameStat.pixelIterated.fetch_add(uint(tlPixelVisited), memory_order_relaxed);
    tlPixelVisited = 0;
}

void RenderTaskDispatcher::init(){
    tileH = camera.height;
    tileW = camera.width;

    for(int i=0;i<tileH;i++){
        for(int j=0;j<tileW;j++){
            tiles[i][j].tileX = j;
            tiles[i][j].tileY = i;
            taskBuffer[i][j].tile = &tiles[i][j];
            taskBuffer[i][j].fragments.clear();
        }
    }
}

void RenderTaskDispatcher::submitFragment(const Triangle2D &frag, int tileX, int tileY){
    RenderTask &task = taskBuffer[tileY][tileX];
    task.fragments.push_back(&frag);
}



void RenderTaskDispatcher::finish(){

    static vector<RenderTask> dogTasks;
    dogTasks.clear();

    tmp1=0;
    for(int y=0;y<tileH;y++){
        for(int x=0;x<tileW;x++){
            RenderTask &task = taskBuffer[y][x];
            dogTasks.push_back(std::move(task));
            // task.fragments.clear();
        }
    }

    static vector<vector<RenderTask>> threadTasks(threadCount);
    for(int i=0;i<threadCount;i++)
        threadTasks[i].clear();

    // 排个序随便分一下
    // sort(dogTasks.begin(), dogTasks.end(), [](const pair<int, RenderTask> &a, const pair<int, RenderTask> &b){return a.first > b.first;});

    int ttfa = (dogTasks.size()-1) / threadCount+1;
    for(auto [id, task]:enumerate(dogTasks)){
        threadTasks[id%threadCount].push_back(std::move(task));
        // loads[id%threadCount] += pair.first;
    }

    disp.runBatch(std::move(threadTasks));
    tmp1=tmp2=tmp3=0;
}

