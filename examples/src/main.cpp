
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _XM_NO_INTRINSICS_
#include <DirectXMath.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <tchar.h>
#include <DirectXMath.h>
#include <windows.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

using namespace DirectX;

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool                      g_SwapChainOccluded = false;
static UINT                      g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static ID3D11Texture2D* g_pOffscreenTex = nullptr;
static ID3D11RenderTargetView* g_pOffscreenRTV = nullptr;
static ID3D11ShaderResourceView* g_pOffscreenSRV = nullptr;
static ID3D11Texture2D* g_pOffscreenDepthTex = nullptr;
static ID3D11DepthStencilView* g_pOffscreenDSV = nullptr;
static UINT                      g_OffscreenW = 0;
static UINT                      g_OffscreenH = 0;

static ID3D11Buffer* g_pVertexBuffer = nullptr;
static ID3D11Buffer* g_pIndexBuffer = nullptr;
static ID3D11Buffer* g_pConstantBuffer = nullptr;
static ID3D11VertexShader* g_pVertexShader = nullptr;
static ID3D11PixelShader* g_pPixelShader = nullptr;
static ID3D11InputLayout* g_pInputLayout = nullptr;
static ID3D11RasterizerState* g_pRasterizerState = nullptr;
static ID3D11DepthStencilState* g_pDepthStencilState = nullptr;
static UINT                      g_IndexCount = 0;

static ID3D11Buffer* g_pHitboxVB = nullptr;
static ID3D11Buffer* g_pHitboxIB = nullptr;
static ID3D11VertexShader* g_pHitboxVS = nullptr;
static ID3D11PixelShader* g_pHitboxPS = nullptr;
static ID3D11InputLayout* g_pHitboxLayout = nullptr;
static ID3D11RasterizerState* g_pWireRasterizer = nullptr;
static ID3D11Buffer* g_pHitboxCB = nullptr;
static bool                      g_ShowHitbox = false;

static XMFLOAT3 g_AABBMin = { -1,-1,-1 };
static XMFLOAT3 g_AABBMax = { 1, 1, 1 };

static float  g_RotationX = 0.0f;
static float  g_RotationY = 0.0f;

struct CBMatrices { XMMATRIX World, View, Projection; };
struct Vertex { XMFLOAT3 Position, Normal; };
struct PosVertex { XMFLOAT3 Position; };

static const char* g_ShaderSrc = R"HLSL(
cbuffer CBMatrices : register(b0) { matrix World; matrix View; matrix Projection; };
struct VSIn { float3 Pos : POSITION; float3 Nor : NORMAL; };
struct PSIn { float4 Pos : SV_POSITION; float3 Nor : TEXCOORD0; };
PSIn VSMain(VSIn v) {
    PSIn o;
    float4 wp = mul(float4(v.Pos,1), World);
    o.Pos = mul(mul(wp, View), Projection);
    o.Nor = mul(float4(v.Nor,0), World).xyz;
    return o;
}
float4 PSMain(PSIn i) : SV_TARGET {
    float3 n  = normalize(i.Nor);
    float3 l1 = normalize(float3(0.5f,1.0f,-0.8f));
    float3 l2 = normalize(float3(-0.5f,-0.3f,0.6f));
    float3 base = float3(0.85f,0.87f,0.92f);
    float3 col  = base*(0.15f+saturate(dot(n,l1))+saturate(dot(n,l2))*0.3f);
    return float4(col,1);
}
)HLSL";

static const char* g_HitboxShaderSrc = R"HLSL(
cbuffer CBMatrices : register(b0) { matrix World; matrix View; matrix Projection; };
struct VSIn { float3 Pos : POSITION; };
struct PSIn { float4 Pos : SV_POSITION; };
PSIn VSMain(VSIn v) {
    PSIn o;
    float4 wp = mul(float4(v.Pos,1), World);
    o.Pos = mul(mul(wp, View), Projection);
    return o;
}
float4 PSMain(PSIn i) : SV_TARGET {
    return float4(1,1,1,1);
}
)HLSL";

static float g_PadLeftRight = 0.05f;
static float g_PadFrontBack = 0.05f;
static float g_PadUpDown = 0.05f;
static float g_CamDist = -4.0f;

struct OBJFaceVtx { int vi, ni; };

static bool LoadOBJ(const char* path, std::vector<Vertex>& outV, std::vector<UINT>& outI)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::vector<XMFLOAT3> pos, nor;
    outV.clear(); outI.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line); std::string tok; ss >> tok;
        if (tok == "v") { XMFLOAT3 p; ss >> p.x >> p.y >> p.z; pos.push_back(p); }
        else if (tok == "vn") { XMFLOAT3 n; ss >> n.x >> n.y >> n.z; nor.push_back(n); }
        else if (tok == "f") {
            std::vector<OBJFaceVtx> fv; std::string part;
            while (ss >> part) {
                OBJFaceVtx v = { 0,0 };
                for (char& c : part) if (c == '/') c = ' ';
                std::istringstream ps(part); std::string num; int slot = 0;
                while (ps >> num) { int val = std::stoi(num); if (slot == 0)v.vi = val; if (slot == 2)v.ni = val; slot++; }
                fv.push_back(v);
            }
            for (int k = 1; k + 1 < (int)fv.size(); k++) {
                auto add = [&](const OBJFaceVtx& f) {
                    Vertex vtx = {};
                    if (f.vi > 0 && f.vi <= (int)pos.size()) vtx.Position = pos[f.vi - 1];
                    if (f.ni > 0 && f.ni <= (int)nor.size()) vtx.Normal = nor[f.ni - 1];
                    outI.push_back((UINT)outV.size()); outV.push_back(vtx);
                    };
                add(fv[0]); add(fv[k]); add(fv[k + 1]);
            }
        }
    }
    if (nor.empty()) {
        for (size_t k = 0; k + 2 < outV.size(); k += 3) {
            XMVECTOR p0 = XMLoadFloat3(&outV[k].Position);
            XMVECTOR p1 = XMLoadFloat3(&outV[k + 1].Position);
            XMVECTOR p2 = XMLoadFloat3(&outV[k + 2].Position);
            XMVECTOR n = XMVector3Normalize(XMVector3Cross(p1 - p0, p2 - p0));
            XMFLOAT3 nf; XMStoreFloat3(&nf, n);
            outV[k].Normal = outV[k + 1].Normal = outV[k + 2].Normal = nf;
        }
    }
    return !outV.empty();
}

static void NormalizeModel(std::vector<Vertex>& verts)
{
    if (verts.empty()) return;
    XMFLOAT3 mn = verts[0].Position, mx = verts[0].Position;
    for (auto& v : verts) {
        mn.x = std::min(mn.x, v.Position.x); mn.y = std::min(mn.y, v.Position.y); mn.z = std::min(mn.z, v.Position.z);
        mx.x = std::max(mx.x, v.Position.x); mx.y = std::max(mx.y, v.Position.y); mx.z = std::max(mx.z, v.Position.z);
    }
    XMFLOAT3 c = { (mn.x + mx.x) * .5f,(mn.y + mx.y) * .5f,(mn.z + mx.z) * .5f };
    float ex = mx.x - mn.x, ey = mx.y - mn.y, ez = mx.z - mn.z;
    float ext = (ex > ey ? (ex > ez ? ex : ez) : (ey > ez ? ey : ez)) * .5f;
    if (ext == 0.f) ext = 1.f;
    for (auto& v : verts) {
        v.Position.x = (v.Position.x - c.x) / ext;
        v.Position.y = (v.Position.y - c.y) / ext;
        v.Position.z = (v.Position.z - c.z) / ext;
    }
    // calc AABB
    g_AABBMin = g_AABBMax = verts[0].Position;
    for (auto& v : verts) {
        g_AABBMin.x = std::min(g_AABBMin.x, v.Position.x); g_AABBMin.y = std::min(g_AABBMin.y, v.Position.y); g_AABBMin.z = std::min(g_AABBMin.z, v.Position.z);
        g_AABBMax.x = std::max(g_AABBMax.x, v.Position.x); g_AABBMax.y = std::max(g_AABBMax.y, v.Position.y); g_AABBMax.z = std::max(g_AABBMax.z, v.Position.z);
    }
}

static void CreateOffscreenRT(UINT w, UINT h)
{
    if (g_pOffscreenSRV) { g_pOffscreenSRV->Release(); g_pOffscreenSRV = nullptr; }
    if (g_pOffscreenRTV) { g_pOffscreenRTV->Release(); g_pOffscreenRTV = nullptr; }
    if (g_pOffscreenTex) { g_pOffscreenTex->Release(); g_pOffscreenTex = nullptr; }
    if (g_pOffscreenDSV) { g_pOffscreenDSV->Release(); g_pOffscreenDSV = nullptr; }
    if (g_pOffscreenDepthTex) { g_pOffscreenDepthTex->Release(); g_pOffscreenDepthTex = nullptr; }
    if (w == 0 || h == 0) return;
    g_OffscreenW = w; g_OffscreenH = h;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    g_pd3dDevice->CreateTexture2D(&td, nullptr, &g_pOffscreenTex);
    g_pd3dDevice->CreateRenderTargetView(g_pOffscreenTex, nullptr, &g_pOffscreenRTV);
    g_pd3dDevice->CreateShaderResourceView(g_pOffscreenTex, nullptr, &g_pOffscreenSRV);
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = w; dd.Height = h; dd.MipLevels = 1; dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; dd.SampleDesc.Count = 1;
    dd.Usage = D3D11_USAGE_DEFAULT; dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    g_pd3dDevice->CreateTexture2D(&dd, nullptr, &g_pOffscreenDepthTex);
    g_pd3dDevice->CreateDepthStencilView(g_pOffscreenDepthTex, nullptr, &g_pOffscreenDSV);
}

static void CreateHitboxBuffers()
{
    float x0 = g_AABBMin.x, y0 = g_AABBMin.y, z0 = g_AABBMin.z;
    float x1 = g_AABBMax.x, y1 = g_AABBMax.y, z1 = g_AABBMax.z;

    float sx = x1 - x0;
    float sy = y1 - y0;
    float sz = z1 - z0;

    float px = sx * g_PadLeftRight;
    float py = sy * g_PadUpDown;
    float pz = sz * g_PadFrontBack;

    x0 -= px;
    x1 += px;

    y0 -= py;
    y1 += py;

    z0 -= pz;
    z1 += pz;

    PosVertex verts[8] = {
        {{x0,y0,z0}}, {{x1,y0,z0}}, {{x1,y1,z0}}, {{x0,y1,z0}},
        {{x0,y0,z1}}, {{x1,y0,z1}}, {{x1,y1,z1}}, {{x0,y1,z1}},
    };

    UINT idx[24] = {
        0,1, 1,2, 2,3, 3,0,
        4,5, 5,6, 6,7, 7,4,
        0,4, 1,5, 2,6, 3,7
    };

    D3D11_BUFFER_DESC bd = {};
    D3D11_SUBRESOURCE_DATA sd = {};

    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = sizeof(verts);
    sd.pSysMem = verts;
    g_pd3dDevice->CreateBuffer(&bd, &sd, &g_pHitboxVB);

    bd.ByteWidth = sizeof(idx);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd.pSysMem = idx;
    g_pd3dDevice->CreateBuffer(&bd, &sd, &g_pHitboxIB);
}

static bool Init3D()
{
    ID3DBlob* vsB = nullptr, * psB = nullptr, * err = nullptr;
    HRESULT hr = D3DCompile(g_ShaderSrc, strlen(g_ShaderSrc), nullptr, nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &vsB, &err);
    if (FAILED(hr)) { if (err) { OutputDebugStringA((char*)err->GetBufferPointer()); err->Release(); }return false; }
    hr = D3DCompile(g_ShaderSrc, strlen(g_ShaderSrc), nullptr, nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &psB, &err);
    if (FAILED(hr)) { if (err) { OutputDebugStringA((char*)err->GetBufferPointer()); err->Release(); }vsB->Release(); return false; }
    g_pd3dDevice->CreateVertexShader(vsB->GetBufferPointer(), vsB->GetBufferSize(), nullptr, &g_pVertexShader);
    g_pd3dDevice->CreatePixelShader(psB->GetBufferPointer(), psB->GetBufferSize(), nullptr, &g_pPixelShader);
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(Vertex,Position),D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",  0,DXGI_FORMAT_R32G32B32_FLOAT,0,offsetof(Vertex,Normal),  D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    g_pd3dDevice->CreateInputLayout(layout, 2, vsB->GetBufferPointer(), vsB->GetBufferSize(), &g_pInputLayout);
    vsB->Release(); psB->Release();

    ID3DBlob* hvsB = nullptr, * hpsB = nullptr;
    hr = D3DCompile(g_HitboxShaderSrc, strlen(g_HitboxShaderSrc), nullptr, nullptr, nullptr, "VSMain", "vs_4_0", 0, 0, &hvsB, &err);
    if (FAILED(hr)) { if (err) { OutputDebugStringA((char*)err->GetBufferPointer()); err->Release(); }return false; }
    hr = D3DCompile(g_HitboxShaderSrc, strlen(g_HitboxShaderSrc), nullptr, nullptr, nullptr, "PSMain", "ps_4_0", 0, 0, &hpsB, &err);
    if (FAILED(hr)) { if (err) { OutputDebugStringA((char*)err->GetBufferPointer()); err->Release(); }hvsB->Release(); return false; }
    g_pd3dDevice->CreateVertexShader(hvsB->GetBufferPointer(), hvsB->GetBufferSize(), nullptr, &g_pHitboxVS);
    g_pd3dDevice->CreatePixelShader(hpsB->GetBufferPointer(), hpsB->GetBufferSize(), nullptr, &g_pHitboxPS);
    D3D11_INPUT_ELEMENT_DESC hlayout[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    g_pd3dDevice->CreateInputLayout(hlayout, 1, hvsB->GetBufferPointer(), hvsB->GetBufferSize(), &g_pHitboxLayout);
    hvsB->Release(); hpsB->Release();

    // load obj
    std::vector<Vertex> verts; std::vector<UINT> idxs;
    if (!LoadOBJ("D:\\ImGUI 3D\\roblox.obj", verts, idxs)) {
        MessageBoxA(nullptr, "Impossible de charger D:\\ImGUI 3D\\roblox.obj", "Erreur OBJ", MB_OK | MB_ICONERROR);
        return false;
    }
    NormalizeModel(verts);
    g_IndexCount = (UINT)idxs.size();

    D3D11_BUFFER_DESC bd = {}; D3D11_SUBRESOURCE_DATA sd = {};
    bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.ByteWidth = (UINT)(sizeof(Vertex) * verts.size()); sd.pSysMem = verts.data();
    g_pd3dDevice->CreateBuffer(&bd, &sd, &g_pVertexBuffer);
    bd.ByteWidth = (UINT)(sizeof(UINT) * idxs.size()); bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sd.pSysMem = idxs.data();
    g_pd3dDevice->CreateBuffer(&bd, &sd, &g_pIndexBuffer);

    bd.Usage = D3D11_USAGE_DYNAMIC; bd.ByteWidth = sizeof(CBMatrices);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_pd3dDevice->CreateBuffer(&bd, nullptr, &g_pConstantBuffer);

    g_pd3dDevice->CreateBuffer(&bd, nullptr, &g_pHitboxCB);

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_BACK; rd.DepthClipEnable = TRUE;
    g_pd3dDevice->CreateRasterizerState(&rd, &g_pRasterizerState);

    D3D11_RASTERIZER_DESC wrd = {};
    wrd.FillMode = D3D11_FILL_WIREFRAME; wrd.CullMode = D3D11_CULL_NONE; wrd.DepthClipEnable = TRUE;
    g_pd3dDevice->CreateRasterizerState(&wrd, &g_pWireRasterizer);

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE; dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc = D3D11_COMPARISON_LESS;
    g_pd3dDevice->CreateDepthStencilState(&dsd, &g_pDepthStencilState);

    CreateHitboxBuffers();

    return true;
}

static void Render3DToTexture()
{
    if (!g_pOffscreenRTV || !g_pVertexBuffer) return;

    const float bg[4] = { 0.12f,0.12f,0.15f,1.f };
    g_pd3dDeviceContext->ClearRenderTargetView(g_pOffscreenRTV, bg);
    g_pd3dDeviceContext->ClearDepthStencilView(g_pOffscreenDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);

    XMMATRIX world = XMMatrixRotationX(g_RotationX) * XMMatrixRotationY(g_RotationY);
    XMMATRIX view = XMMatrixLookAtLH(
        XMVectorSet(0, 0, g_CamDist, 1),
        XMVectorSet(0, 0, 0, 1),
        XMVectorSet(0, 1, 0, 0)
    );
    float asp = (g_OffscreenH > 0) ? (float)g_OffscreenW / (float)g_OffscreenH : 1.f;
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(45.f), asp, 0.1f, 100.f);

    auto UploadMVP = [&](ID3D11Buffer* cb) {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        g_pd3dDeviceContext->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        CBMatrices* c = (CBMatrices*)mapped.pData;
        c->World = XMMatrixTranspose(world);
        c->View = XMMatrixTranspose(view);
        c->Projection = XMMatrixTranspose(proj);
        g_pd3dDeviceContext->Unmap(cb, 0);
        };
    UploadMVP(g_pConstantBuffer);

    D3D11_VIEWPORT vp = { 0,0,(float)g_OffscreenW,(float)g_OffscreenH,0,1 };
    g_pd3dDeviceContext->RSSetViewports(1, &vp);
    g_pd3dDeviceContext->OMSetDepthStencilState(g_pDepthStencilState, 0);
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pOffscreenRTV, g_pOffscreenDSV);

    g_pd3dDeviceContext->RSSetState(g_pRasterizerState);
    g_pd3dDeviceContext->IASetInputLayout(g_pInputLayout);
    g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(Vertex), offset = 0;
    g_pd3dDeviceContext->IASetVertexBuffers(0, 1, &g_pVertexBuffer, &stride, &offset);
    g_pd3dDeviceContext->IASetIndexBuffer(g_pIndexBuffer, DXGI_FORMAT_R32_UINT, 0);
    g_pd3dDeviceContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pd3dDeviceContext->PSSetShader(g_pPixelShader, nullptr, 0);
    g_pd3dDeviceContext->VSSetConstantBuffers(0, 1, &g_pConstantBuffer);
    g_pd3dDeviceContext->DrawIndexed(g_IndexCount, 0, 0);

    if (g_ShowHitbox && g_pHitboxVB)
    {
        UploadMVP(g_pHitboxCB);
        g_pd3dDeviceContext->RSSetState(g_pWireRasterizer);
        g_pd3dDeviceContext->IASetInputLayout(g_pHitboxLayout);
        g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        UINT hstride = sizeof(PosVertex), hoffset = 0;
        g_pd3dDeviceContext->IASetVertexBuffers(0, 1, &g_pHitboxVB, &hstride, &hoffset);
        g_pd3dDeviceContext->IASetIndexBuffer(g_pHitboxIB, DXGI_FORMAT_R32_UINT, 0);
        g_pd3dDeviceContext->VSSetShader(g_pHitboxVS, nullptr, 0);
        g_pd3dDeviceContext->PSSetShader(g_pHitboxPS, nullptr, 0);
        g_pd3dDeviceContext->VSSetConstantBuffers(0, 1, &g_pHitboxCB);
        g_pd3dDeviceContext->DrawIndexed(24, 0, 0);
    }
}

static void Cleanup3D()
{
    if (g_pOffscreenSRV) { g_pOffscreenSRV->Release();     g_pOffscreenSRV = nullptr; }
    if (g_pOffscreenRTV) { g_pOffscreenRTV->Release();     g_pOffscreenRTV = nullptr; }
    if (g_pOffscreenTex) { g_pOffscreenTex->Release();     g_pOffscreenTex = nullptr; }
    if (g_pOffscreenDSV) { g_pOffscreenDSV->Release();     g_pOffscreenDSV = nullptr; }
    if (g_pOffscreenDepthTex) { g_pOffscreenDepthTex->Release(); g_pOffscreenDepthTex = nullptr; }
    if (g_pVertexBuffer) { g_pVertexBuffer->Release();     g_pVertexBuffer = nullptr; }
    if (g_pIndexBuffer) { g_pIndexBuffer->Release();      g_pIndexBuffer = nullptr; }
    if (g_pConstantBuffer) { g_pConstantBuffer->Release();   g_pConstantBuffer = nullptr; }
    if (g_pVertexShader) { g_pVertexShader->Release();     g_pVertexShader = nullptr; }
    if (g_pPixelShader) { g_pPixelShader->Release();      g_pPixelShader = nullptr; }
    if (g_pInputLayout) { g_pInputLayout->Release();      g_pInputLayout = nullptr; }
    if (g_pRasterizerState) { g_pRasterizerState->Release();  g_pRasterizerState = nullptr; }
    if (g_pDepthStencilState) { g_pDepthStencilState->Release(); g_pDepthStencilState = nullptr; }
    if (g_pHitboxVB) { g_pHitboxVB->Release();         g_pHitboxVB = nullptr; }
    if (g_pHitboxIB) { g_pHitboxIB->Release();         g_pHitboxIB = nullptr; }
    if (g_pHitboxVS) { g_pHitboxVS->Release();         g_pHitboxVS = nullptr; }
    if (g_pHitboxPS) { g_pHitboxPS->Release();         g_pHitboxPS = nullptr; }
    if (g_pHitboxLayout) { g_pHitboxLayout->Release();     g_pHitboxLayout = nullptr; }
    if (g_pWireRasterizer) { g_pWireRasterizer->Release();   g_pWireRasterizer = nullptr; }
    if (g_pHitboxCB) { g_pHitboxCB->Release();         g_pHitboxCB = nullptr; }
}

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int main(int, char**)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        ::MonitorFromPoint(POINT{ 0,0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = { sizeof(wc),CS_CLASSDC,WndProc,0L,0L,
        GetModuleHandle(nullptr),nullptr,nullptr,nullptr,nullptr,L"ImGui3DViewer",nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"ImGui - 3D OBJ Viewer", WS_OVERLAPPEDWINDOW,
        100, 100, (int)(1280 * main_scale), (int)(800 * main_scale),
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); ::UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT); ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale); style.FontScaleDpi = main_scale;
    ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    if (!Init3D()) {
        ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
        CleanupDeviceD3D(); ::DestroyWindow(hwnd); ::UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1;
    }
    CreateOffscreenRT(800, 600);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    bool done = false;

    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg); ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) { ::Sleep(10); continue; }
        g_SwapChainOccluded = false;
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0; CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();

        if (io.KeyCtrl && io.MouseWheel != 0.0f)
        {
            g_CamDist += io.MouseWheel * 0.5f;

            if (g_CamDist > -1.0f) g_CamDist = -1.0f;
            if (g_CamDist < -20.0f) g_CamDist = -20.0f;
        }

        // control window
        ImGui::SetNextWindowPos(ImVec2(722, 110), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(379, 126), ImGuiCond_FirstUseEver);

        ImGui::Begin("Control");
        ImGui::Checkbox("Hitbox", &g_ShowHitbox);
        ImGui::SliderFloat("Pad Left/Right", &g_PadLeftRight, 0.0f, 0.5f);
        ImGui::SliderFloat("Pad Front/Back", &g_PadFrontBack, 0.0f, 0.5f);
        ImGui::SliderFloat("Pad Up/Down", &g_PadUpDown, 0.0f, 0.5f);

        CreateHitboxBuffers();
        ImGui::End();

        // model window
        ImGui::SetNextWindowPos(ImVec2(148, 108), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(545, 581), ImGuiCond_FirstUseEver);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        ImVec2 winSize = ImGui::GetContentRegionAvail();
        UINT newW = (UINT)std::max(1.f, winSize.x);
        UINT newH = (UINT)std::max(1.f, winSize.y);
        if (newW != g_OffscreenW || newH != g_OffscreenH) CreateOffscreenRT(newW, newH);

        Render3DToTexture();

        ImVec2 imageSize = ImVec2((float)g_OffscreenW, (float)g_OffscreenH);
        ImVec2 imagePos = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)g_pOffscreenSRV, imageSize);
        ImGui::SetCursorScreenPos(imagePos);
        ImGui::InvisibleButton("##3d_viewport", imageSize, ImGuiButtonFlags_MouseButtonLeft);
        if (ImGui::IsItemActive()) {
            g_RotationY += io.MouseDelta.x * 0.005f;
            g_RotationX += io.MouseDelta.y * 0.005f;
        }
        ImGui::End();

        ImGui::Render();
        const float cc[4] = { clear_color.x * clear_color.w,clear_color.y * clear_color.w,
                            clear_color.z * clear_color.w,clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        HRESULT hr = g_pSwapChain->Present(1, 0);
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    Cleanup3D();
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    CleanupDeviceD3D(); ::DestroyWindow(hwnd); ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT flags = 0; D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL fla[] = { D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        flags, fla, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            flags, fla, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl, &g_pd3dDeviceContext);
    if (res != S_OK) return false;
    CreateRenderTarget(); return true;
}
void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
void CreateRenderTarget() {
    ID3D11Texture2D* bb = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&bb));
    g_pd3dDevice->CreateRenderTargetView(bb, nullptr, &g_mainRenderTargetView);
    bb->Release();
}
void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); g_ResizeHeight = (UINT)HIWORD(lParam); return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0; break;
    case WM_DESTROY:
        ::PostQuitMessage(0); return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}