#ifndef SHADER_INTERFACE_H
#define SHADER_INTERFACE_H

#include "shaders.h"
#include "parallel_render.h"

extern thread_local uint64_t tlPixelVisited;

template<typename T> concept IsShader = requires(const EdgeIterator &edgeIt, const Iterator2D &zInv, const Iterator2D &u_z, const Iterator2D &v_z){
    {T::alphaTest(uint(), edgeIt, zInv, u_z, v_z)} -> std::same_as<bool>;
} && requires(float u, float v, float d, uint triangleID, Vec3 viewDirection){
    {T::colorSample(u, v, triangleID, viewDirection, d)} -> std::same_as<uint>;
};

// 所有涉及部分透明面片的逻辑在这里实现，包括线框渲染，单片草之类的
template<typename FragmentShader>
    requires IsShader<FragmentShader> void segmentRasterization(const Triangle2D &frag, int pixelW, int pixelH){

    EdgeIterator edgeIt = frag.edgeIterator;
    Iterator2D zInv = frag.zInv;
    Iterator2D u_z = frag.u_z;
    Iterator2D v_z = frag.v_z;

    int xlt = frag.xlt;
    int xrb = frag.xrb;
    int ylt = frag.ylt;
    int yrb = frag.yrb;

    edgeIt.batchIterate(xlt, ylt);
    zInv.batchIterate(xlt, ylt);
    u_z.batchIterate(xlt, ylt);
    v_z.batchIterate(xlt, ylt);

    for(int y = ylt; y <= yrb; y++){
        EdgeIterator tempEdgeIt = edgeIt;
        Iterator2D tempZInv = zInv;
        Iterator2D tempUZ = u_z;
        Iterator2D tempVZ = v_z;
        bool passFlag = false;

        for(int x = xlt; x <= xrb; x++){
            bool result = FragmentShader::alphaTest(frag.triangleID, tempEdgeIt, tempZInv, tempUZ, tempVZ);

            if(result){
                passFlag = true;
                if(shadingBuffer.zInv[y][x] < tempZInv.val){
                    shadingBuffer.triangleID[y][x] = frag.triangleID;
                    shadingBuffer.zInv[y][x] = tempZInv.val;
                    shadingBuffer.u_z[y][x] = tempUZ.val;
                    shadingBuffer.v_z[y][x] = tempVZ.val;
                    shadingBuffer.materialID[y][x] = ShaderInternal::triangles[frag.triangleID].materialID;
                }
            }else if(passFlag && tempEdgeIt.check() != EdgeIterator::INNER)break;

            tempEdgeIt.xIterate();
            tempZInv.xIterate();
            tempUZ.xIterate();
            tempVZ.xIterate();
        }
        edgeIt.yIterate();
        zInv.yIterate();
        u_z.yIterate();
        v_z.yIterate();
    }
}

struct TileLevelResult{
    const static int
        OUTER   = 0,
        UNKNOWN = 1,
        INNER   = 2;
};

std::tuple<bool, float, float> inline xTest(Vec3 p0, Vec3 p1, float x){
    if(p0.x > p1.x) std::swap(p0, p1);
    if(abs(p0.x-p1.x) < 1e-5) return {false, 0, 0};
    if(p0.x > x || p1.x < x) return {false, 0, 0};
    float k = (x-p0.x) / (p1.x-p0.x);
    return {true, x, p0.y + k *(p1.y-p0.y)};
}
std::tuple<bool, float, float> inline yTest(Vec3 p0, Vec3 p1, float y){
    if(p0.y > p1.y) std::swap(p0, p1);
    if(abs(p0.y-p1.y) < 1e-5) return {false, 0, 0};
    if(p0.y > y || p1.y < y) return {false, 0, 0};
    float k = (y-p0.y) / (p1.y-p0.y);
    return {true, p0.x + k *(p1.x-p0.x), y};
}

std::tuple<bool, int, int, int, int> inline getTiledBBox(const Triangle2D &frag, int xmin, int xmax, int ymin, int ymax){
    Vec3 p0 = ShaderInternal::projectedVertices[ShaderInternal::triangles[frag.triangleID].vid[0]].pos;
    Vec3 p1 = ShaderInternal::projectedVertices[ShaderInternal::triangles[frag.triangleID].vid[1]].pos;
    Vec3 p2 = ShaderInternal::projectedVertices[ShaderInternal::triangles[frag.triangleID].vid[2]].pos;
    p0.z = 0;
    p1.z = 0;
    p2.z = 0;

    thread_local static std::vector<std::tuple<bool, float, float>> intersections;
    intersections.clear();

    for(const auto [v0, v1]: std::vector<std::pair<Vec3, Vec3>>({{p0, p1}, {p1, p2}, {p2, p0}})){
        intersections.push_back(xTest(v0, v1, xmin));
        intersections.push_back(xTest(v0, v1, xmax));
        intersections.push_back(yTest(v0, v1, ymin));
        intersections.push_back(yTest(v0, v1, ymax));
    }

    for(const Vec3 &v:{p0, p1, p2})
        if(v.x >= xmin && v.x <= xmax && v.y >= ymin && v.y <= ymax)
            intersections.push_back({true, v.x, v.y});


    float xlt = 1e9, ylt = 1e9, xrb = 0, yrb = 0;
    int cnt = 0;
    for(const auto &[flag, x, y]: intersections){
        if(!flag) continue;
        xlt = std::min(xlt, x);
        ylt = std::min(ylt, y);
        xrb = std::max(xrb, x);
        yrb = std::max(yrb, y);
        cnt ++;
    }
    xlt = std::max(xlt, (float)xmin);
    ylt = std::max(ylt, (float)ymin);
    xrb = std::min(xrb, (float)xmax);
    yrb = std::min(yrb, (float)ymax);

    return {cnt>0, xlt, ylt, ceil(xrb), ceil(yrb)};

}
template<typename FragmentShader>
    requires IsShader<FragmentShader> void tileRasterization(const Triangle2D &frag,Tile& __restrict tile, int tileLevelResult){

    if(tileLevelResult == TileLevelResult::OUTER) return;

    EdgeIterator edgeIt = frag.edgeIterator;
    Iterator2D zInv = frag.zInv;
    Iterator2D u_z = frag.u_z;
    Iterator2D v_z = frag.v_z;

    int tileXmin = tile.tileX * tileSize;
    int tileXmax = tile.tileX * tileSize + tileSize-1;
    int tileYmin = tile.tileY * tileSize;
    int tileYmax = tile.tileY * tileSize + tileSize-1;

    int xlt = std::max(frag.xlt, tileXmin);
    int xrb = std::min(frag.xrb, tileXmax);
    int ylt = std::max(frag.ylt, tileYmin);
    int yrb = std::min(frag.yrb, tileYmax);

    if((frag.xrb-frag.xlt+1)*(frag.yrb-frag.ylt+1) > 4096){
        auto [flag, precXlt, precYlt, precXrb, precYrb] = getTiledBBox(frag, tileXmin, tileXmax, tileYmin, tileYmax);
        if(flag){
            xlt = std::max(xlt, precXlt);
            ylt = std::max(ylt, precYlt);
            xrb = std::min(xrb, precXrb);
            yrb = std::min(yrb, precYrb);
        }
    }
    float zMax = zInv.val + std::max({xlt*zInv.dv_dx+ylt*zInv.dv_dy,
                                      xrb*zInv.dv_dx+ylt*zInv.dv_dy,
                                      xlt*zInv.dv_dx+yrb*zInv.dv_dy,
                                      xrb*zInv.dv_dx+yrb*zInv.dv_dy});
    if(zMax < tile.zInvMin && tile.cpCount == tileSize*tileSize) return;

    tlPixelVisited += uint64_t((xrb-xlt+1)*(yrb-ylt+1));

    edgeIt.batchIterate(xlt, ylt);
    zInv.batchIterate(xlt, ylt);
    u_z.batchIterate(xlt, ylt);
    v_z.batchIterate(xlt, ylt);

    xlt -= tile.tileX * tileSize;
    xrb -= tile.tileX * tileSize;
    ylt -= tile.tileY * tileSize;
    yrb -= tile.tileY * tileSize;

    for(int y = ylt; y <= yrb; y++){

        EdgeIterator tempEdgeIt = edgeIt;
        Iterator2D tempZInv = zInv;
        Iterator2D tempUZ = u_z;
        Iterator2D tempVZ = v_z;
        bool passFlag = false;

        for(int x = xlt; x <= xrb; x++){
            if(tile.zInv[y][x] < tempZInv.val){
                bool result = FragmentShader::alphaTest(frag.triangleID, tempEdgeIt, tempZInv, tempUZ, tempVZ);
                if(result){
                    passFlag = true;
                    if(!tile.vis[y][x]){
                        tile.vis[y][x] = true;
                        tile.cpCount ++;
                        tile.zInvMin = std::min(tile.zInvMin, tempZInv.val);
                    }
                    // tile.zInvMin = min(tile.zInvMin, tempZInv.val);
                    tile.triangleID[y][x] = frag.triangleID;
                    tile.zInv[y][x] = tempZInv.val;
                    tile.u_z[y][x] = tempUZ.val;
                    tile.v_z[y][x] = tempVZ.val;
                }else if(passFlag && tempEdgeIt.check() != EdgeIterator::INNER){
                    // 只有走出三角形当前扫描线区间才能提前退出。
                    // 不能因为贴图 alpha=0 就 break：透明区域后面可能还有有效像素。
                    break;
                }
            }

            if(tileLevelResult == TileLevelResult::UNKNOWN)
                tempEdgeIt.xIterate();
            tempZInv.xIterate();
            tempUZ.xIterate();
            tempVZ.xIterate();
        }
        if(tileLevelResult == TileLevelResult::UNKNOWN)
            edgeIt.yIterate();
        zInv.yIterate();
        u_z.yIterate();
        v_z.yIterate();

    }
}


template<typename FragmentShader>
    requires IsShader<FragmentShader> void spanTileRasterization(const Triangle2D &frag, Tile &tile, int tileLevelResult){
    if(tileLevelResult == TileLevelResult::OUTER || !frag.spanReady) return;
    const int tx0 = tile.tileX * tileSize;
    const int ty0 = tile.tileY * tileSize;
    const int y0 = std::max(ty0, frag.ylt);
    const int y1 = std::min(ty0 + tileSize - 1, frag.yrb);
    if(y0 > y1) return;

    SpanCursor cz, cu, cv;
    int prevL = 0;
    int prevSeg = -1;
    bool anchored = false;
    for(int y = y0; y <= y1; ++y){
        const int seg = (y + 0.5f >= frag.splitY) ? 1 : 0;
        const SpanEdge &le = frag.leftEdge[seg], &re = frag.rightEdge[seg];
        const float yy = y + 0.5f;
        // 边方程用 (x + 0.5, y + 0.5) 作采样点，SpanEdge 的 x0 是顶点 x + 0.5。
        const float xl = le.x0 + le.m * (yy - le.y0);
        const float xr = re.x0 + re.m * (yy - re.y0);
        int l = int(std::floor(xl));
        int r = int(std::ceil(xr)) - 1;
        l = std::max(l, tx0);
        r = std::min(r, tx0 + tileSize - 1);
        if(l > r) continue;

        if(!anchored || seg != prevSeg){
            cz.it = frag.zInv; cu.it = frag.u_z; cv.it = frag.v_z;
            cz.anchor(frag.zInv, l, y);
            cu.anchor(frag.u_z, l, y);
            cv.anchor(frag.v_z, l, y);
        }else{
            const int dx = l - prevL;
            cz.it.val += cz.it.dv_dy;
            cu.it.val += cu.it.dv_dy;
            cv.it.val += cv.it.dv_dy;
            if(dx){
                cz.it.val += float(dx) * cz.it.dv_dx;
                cu.it.val += float(dx) * cu.it.dv_dx;
                cv.it.val += float(dx) * cv.it.dv_dx;
            }
        }
        prevL = l;
        prevSeg = seg;
        anchored = true;
        tlPixelVisited += uint64_t(r - l + 1);

        EdgeIterator edge = frag.edgeIterator;
        edge.batchIterate(l, y);
        Iterator2D z = cz.it, u = cu.it, v = cv.it;
        for(int x = l; x <= r; ++x){
            if(edge.check() == EdgeIterator::INNER && tile.zInv[y-ty0][x-tx0] < z.val){
                if(FragmentShader::alphaTest(frag.triangleID, edge, z, u, v)){
                    const int ly = y - ty0, lx = x - tx0;
                    tile.triangleID[ly][lx] = frag.triangleID;
                    tile.zInv[ly][lx] = z.val;
                    tile.u_z[ly][lx] = u.val;
                    tile.v_z[ly][lx] = v.val;
                    if(!tile.vis[ly][lx]){
                        tile.vis[ly][lx] = true;
                        tile.cpCount++;
                        tile.zInvMin = std::min(tile.zInvMin, z.val);
                    }
                }
            }
            edge.xIterate(); z.val += z.dv_dx; u.val += u.dv_dx; v.val += v.dv_dx;
        }
    }
}

template<typename FragmentShader>
    requires IsShader<FragmentShader> uint colorDetermination(float u, float v, uint triangleID, const Vec3 &view, float d=0.0f){
    return FragmentShader::colorSample(u, v, triangleID, view, d);
}

#endif // SHADER_INTERFACE_H
