#include <stdio.h>
#include <windows.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <D3Dcompiler.h>

#include <map>
#include <vector>
#include <string>
#include <vector>
#include <algorithm>
#include <DirectXMath.h>


#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "d3d11.lib")

#pragma comment(lib, "advapi32.lib")

#include <fbxsdk.h>
#pragma comment(lib, "libfbxsdk-mt.lib")
#pragma comment(lib, "libxml2-mt.lib")
#pragma comment(lib, "zlib-mt.lib")

#include "FreeImage.h"
#pragma comment(lib, "FreeImage.lib")



enum {
	CMD_NOP,
	CMD_SET_BARRIER,
	CMD_SET_RENDER_TARGET,
	CMD_SET_DEPTH_RENDER_TARGET,
	CMD_SET_TEXTURE,
	CMD_SET_VERTEX,
	CMD_SET_INDEX,
	CMD_SET_CONSTANT,
	CMD_SET_SHADER,
	CMD_CLEAR,
	CMD_CLEAR_DEPTH,
	CMD_DRAW_INDEX,
	CMD_QUIT,
	CMD_MAX,
};

struct matrix4x4 {
	float data[16];
};

struct vector4 {
	union {
		struct {
			float x, y, z, w;
		};
		float data[4];
	};
	void print() {
		printf("%.5f, %.5f, %.5f, %.5f\n", x, y, z, w);
	}
};

struct vector3 {
	float x, y, z;
	void print() {
		printf("%.5f, %.5f\n", x, y, z);
	}
};

struct vector2 {
	float x, y;
	void print() {
		printf("%.5f, %.5f\n", x, y);
	}
};

struct vertex_format {
	vector4 pos;
	vector3 nor;
	vector2 uv;
};

struct cmd {
	int type;
	std::string name;
	struct rect_t {
		int x, y, w, h;
	};
	union {
		struct set_barrier_t {
			bool to_present;
			bool to_rendertarget;
			bool to_texture;
		} set_barrier;

		struct set_render_target_t {
			int fmt;
			rect_t rect;
		} set_render_target;

		struct set_depth_render_target_t {
			int fmt;
			rect_t rect;
		} set_depth_render_target;

		struct set_texture_t {
			int fmt;
			int slot;
			void *data;
			size_t size;
			size_t stride;
			rect_t rect;
		} set_texture;

		struct set_vertex_t {
			void *data;
			size_t size;
			size_t stride_size;
		} set_vertex;

		struct set_index_t {
			void *data;
			size_t size;
		} set_index;

		struct set_constant_t {
			int slot;
			void *data;
			size_t size;
		} set_constant;

		struct set_shader_t {
			bool is_update;
		} set_shader;

		struct clear_t {
			vector4 color;
		} clear;

		struct clear_depth_t {
			float value;
		} clear_depth;

		struct draw_index_t {
			int start;
			int count;
		} draw_index;
	};

	void print() {
		printf("cmd:name=%s:\t\t\t", name.c_str());
		switch(type) {
			case CMD_CLEAR:
				printf("CMD_CLEAR :%f %f %f %f\n", clear.color.x, clear.color.y, clear.color.z, clear.color.w);
				break;
			case CMD_SET_BARRIER:
				printf("CMD_SET_BARRIER :");
				printf("%d %d %d\n",
						set_barrier.to_present, set_barrier.to_rendertarget, set_barrier.to_texture);
				break;
			case CMD_SET_RENDER_TARGET:
				printf("CMD_SET_RENDER_TARGET :");
				printf("rect.x=%d rect.x=%d rect.x=%d rect.x=%d : fmt=%d\n",
						set_render_target.rect.x, set_render_target.rect.y,
						set_render_target.rect.w, set_render_target.rect.h, set_render_target.fmt);
				break;
			case CMD_SET_TEXTURE:
				printf("CMD_SET_TEXTURE :");
				printf("%d %d %d %d : slot=%d, fmt=%d, data=%p, size=%p\n",
						set_texture.rect.x, set_texture.rect.y,
						set_texture.rect.w, set_texture.rect.h,
						set_texture.slot, set_texture.fmt, set_texture.data, set_texture.size);
				break;
			case CMD_SET_VERTEX:
				printf("CMD_SET_VERTEX :");
				printf("data=%p, size=%zu\n", set_vertex.data, set_vertex.size);
				break;
			case CMD_SET_INDEX:
				printf("CMD_SET_INDEX :");
				printf("data=%p, size=%zu\n", set_index.data, set_index.size);
				break;
			case CMD_SET_CONSTANT:
				printf("CMD_SET_CONSTANT :");
				printf("slot=%d, data=%p, size=%zu\n",
						set_constant.slot, set_constant.data, set_constant.size);
				break;
			case CMD_SET_SHADER:
				printf("CMD_SET_SHADER :");
				printf("is_update=%d\n", set_shader.is_update);
				break;
			case CMD_DRAW_INDEX:
				printf("CMD_DRAW_INDEX :");
				printf("start=%d, count=%d\n", draw_index.start, draw_index.count);
				break;
			case CMD_MAX:
				break;
		}
	}
};

HRESULT 
CompileShaderFromFile(std::string name, LPCSTR szEntryPoint, LPCSTR szShaderModel, ID3DBlob** ppBlobOut )
{
	HRESULT hr = S_OK;
	ID3DBlob* pErrorBlob = NULL;
	std::vector<WCHAR> wfname;
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
	for (int i = 0; i < name.length(); i++)
		wfname.push_back(name[i]);
	wfname.push_back(0);
	hr = D3DCompileFromFile(&wfname[0], NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		szEntryPoint, szShaderModel, flags, 0, ppBlobOut, &pErrorBlob);
	if(FAILED(hr)) {
		printf("ERROR:%s : %s\n", __FUNCTION__, pErrorBlob ? pErrorBlob->GetBufferPointer() : "UNKNOWN");
	}
	if(pErrorBlob)
		pErrorBlob->Release();
	return hr;
}

void PresentGraphics(
	const char * appname, std::vector<cmd> & vcmd,
	HWND hwnd, UINT w, UINT h, UINT num, UINT heapcount, UINT slotmax)
{
	static ID3D11Device *dev  = NULL;
	static ID3D11DeviceContext *ctx = NULL;
	static IDXGISwapChain *swapchain = NULL;

	struct PipelineState {
		ID3D11VertexShader *vs = NULL;
		ID3D11GeometryShader *gs = NULL;
		ID3D11PixelShader *ps = NULL;
		ID3D11InputLayout *layout = NULL;
	};
	static std::map<std::string, ID3D11RenderTargetView *> mrtv;
	static std::map<std::string, ID3D11ShaderResourceView *> msrv;
	static std::map<std::string, ID3D11DepthStencilView *> mdsv;
	
	static std::map<std::string, ID3D11Texture2D *> mtex;
	static std::map<std::string, ID3D11Buffer *> mbuf;
	static std::map<std::string, PipelineState> mpstate;
	static ID3D11SamplerState * sampler_state_point = NULL;
	static ID3D11SamplerState * sampler_state_linear = NULL;
	static ID3D11RasterizerState * rsstate = NULL;
	static uint64_t device_index = 0;
	static uint64_t frame_count = 0;

	if(dev == nullptr) {
		DXGI_SWAP_CHAIN_DESC d3dsddesc = {
			{ w, h, { 60, 1 }, DXGI_FORMAT_R8G8B8A8_UNORM, 
				DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
				DXGI_MODE_SCALING_UNSPECIFIED,
			}, {1, 0},
			DXGI_USAGE_RENDER_TARGET_OUTPUT,
			num, hwnd, TRUE, DXGI_SWAP_EFFECT_DISCARD,
			DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH,
		};

		D3D11CreateDeviceAndSwapChain(
			NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
			&d3dsddesc, &swapchain, &dev, NULL, &ctx);
		ID3D11Texture2D *backtex = nullptr;
		ID3D11RenderTargetView *backrtv = nullptr;
		swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID *)&backtex);
		dev->CreateRenderTargetView(backtex, NULL, &backrtv);
		auto backbuffername = "backbuffer";
		for(int i = 0; i < num; i++) {
			auto name = backbuffername + std::to_string(i);
			mtex[name] = backtex;
			mrtv[name] = backrtv;
		}

		D3D11_SAMPLER_DESC sampler_desc = {
			D3D11_FILTER_MIN_MAG_MIP_POINT,
			D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP,
			0, 0, D3D11_COMPARISON_NEVER, {0.0f, 0.0f, 0.0f, 0.0f}, 0, D3D11_FLOAT32_MAX
		};
		dev->CreateSamplerState(&sampler_desc, &sampler_state_point);
		sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		dev->CreateSamplerState(&sampler_desc, &sampler_state_linear);

		D3D11_RASTERIZER_DESC rsstate_desc = {};
		rsstate_desc.FillMode = D3D11_FILL_SOLID;
		rsstate_desc.CullMode = D3D11_CULL_NONE;
		rsstate_desc.FrontCounterClockwise = TRUE;
		rsstate_desc.DepthClipEnable = TRUE;
		rsstate_desc.ScissorEnable = TRUE;
		dev->CreateRasterizerState(&rsstate_desc, &rsstate);
	};

	if(hwnd == nullptr) {
		auto release = [](auto &a, const char * name = nullptr) {
			if(a) {
				printf("release : %p : name=%s\n", a, name ? name : "noname");
				a->Release();
			}
			a = nullptr;
		};
		auto mrelease = [&](auto &m) {
			for(auto & p : m)
				release(p.second, p.first.c_str());
		};

		mrelease(msrv);
		mrelease(mtex);
		mrelease(mbuf);
		for(auto & p : mpstate) {
			release(p.second.vs, (p.first + " : VS").c_str());
			release(p.second.gs, (p.first + " : GS").c_str());
			release(p.second.ps, (p.first + " : PS").c_str());
			release(p.second.layout, (p.first + " : LAYOUT").c_str());
		}
		release(sampler_state_point);
		release(sampler_state_linear);
		release(rsstate);
		release(swapchain);
		release(ctx);
		release(dev);
		return;
	}

	ctx->IASetInputLayout(NULL);
	ctx->VSSetShader(NULL, NULL, 0);
	ctx->GSSetShader(NULL, NULL, 0);
	ctx->PSSetShader(NULL, NULL, 0);
	ctx->VSSetSamplers(0, 1, &sampler_state_point);
	ctx->PSSetSamplers(0, 1, &sampler_state_point);
	ctx->VSSetSamplers(1, 1, &sampler_state_linear);
	ctx->PSSetSamplers(1, 1, &sampler_state_linear);
	ctx->RSSetState(rsstate);

	for(auto & c : vcmd) {
		auto type = c.type;
		auto name = c.name;

		//CMD_SET_RENDER_TARGET
		if(type == CMD_SET_RENDER_TARGET) {
			auto name_depth = name + "_depth";
			auto fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
			auto fmt_depth = DXGI_FORMAT_D32_FLOAT;
			auto tex = mtex[name];
			if(tex == nullptr) {
				D3D11_TEXTURE2D_DESC desc = {
					c.set_render_target.rect.w, c.set_render_target.rect.h, 1, 1, fmt, {1, 0},
					D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0,  0,
				};
				dev->CreateTexture2D(&desc, NULL, &tex);
				if(tex)
					mtex[name] = tex;
				else
					printf("error CMD_SET_RENDER_TARGET name=%s, tex=%p\n", name.c_str(), tex);
				printf("name=%s, tex=%p\n", name.c_str(), tex);
			}
			auto tex_depth = mtex[name_depth];
			if(tex_depth == nullptr) {
				D3D11_TEXTURE2D_DESC desc = {
					c.set_render_target.rect.w, c.set_render_target.rect.h, 1, 1, fmt_depth, {1, 0},
					D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0,  0,
				};
				dev->CreateTexture2D(&desc, NULL, &tex_depth);
				if(tex)
					mtex[name_depth] = tex;
				else
					printf("error CMD_SET_RENDER_TARGET name_depth=%s, tex=%p\n", name_depth.c_str(), tex);
				printf("name=%s, tex=%p\n", name_depth.c_str(), tex);
			}
			auto rtv = mrtv[name];
			if(rtv == nullptr) {
				dev->CreateRenderTargetView(tex, nullptr, &rtv);
				mrtv[name] = rtv;
				printf("name=%s, rtv=%p\n", name.c_str(), rtv);
			}

			auto dsv = mdsv[name];
			if(dsv == nullptr) {
				dev->CreateDepthStencilView(tex_depth, nullptr, &dsv);
				mdsv[name] = dsv;
				printf("name(dsv)=%s, dsv=%p\n", name.c_str(), dsv);
			}
			
			D3D11_RECT rc = { 0, 0, w, h, };
			D3D11_VIEWPORT vp = { 0.0f, 0.0f, (FLOAT)w, (FLOAT)h, 0.0f, 1.0f, };
			ctx->RSSetScissorRects(1, &rc);
			ctx->RSSetViewports(1, &vp);
			ctx->OMSetRenderTargets(1, &rtv, dsv);
		}

		//CMD_SET_TEXTURE
		if(type == CMD_SET_TEXTURE) {
			auto slot = c.set_texture.slot;
			auto fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
			auto tex = mtex[name];
			if(tex == nullptr) {
				D3D11_TEXTURE2D_DESC desc = {
					c.set_texture.rect.w, c.set_texture.rect.h, 1, 1, fmt, {1, 0},
					D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,  0,
				};
				D3D11_SUBRESOURCE_DATA initdata = {};
				initdata.pSysMem = c.set_texture.data;
				initdata.SysMemPitch = c.set_texture.stride;
				initdata.SysMemSlicePitch = c.set_texture.size;
				dev->CreateTexture2D(&desc, &initdata, &tex);
				if(tex)
					mtex[name] = tex;
				else
					printf("error CMD_SET_TEXTURE name=%s, tex=%p\n", name.c_str(), tex);
				printf("name=%s, tex=%p\n", name.c_str(), tex);
			}

			auto srv = msrv[name];
			if(srv == nullptr) {
				D3D11_SHADER_RESOURCE_VIEW_DESC desc = { fmt, D3D11_SRV_DIMENSION_TEXTURE2D, {0, 1}, };
				dev->CreateShaderResourceView(tex, &desc, &srv);
				msrv[name] = srv;
				printf("name=%s, srv=%p\n", name.c_str(), srv);
			}
			ctx->VSSetShaderResources(slot, 1, &srv);
			ctx->PSSetShaderResources(slot, 1, &srv);
		}

		//CMD_SET_CONSTANT
		if(type == CMD_SET_CONSTANT) {
			auto slot = c.set_constant.slot;
			auto cb = mbuf[name];
			if(cb == nullptr) {
				D3D11_BUFFER_DESC bd = {
					c.set_constant.size, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0
				};
				auto hr = dev->CreateBuffer(&bd, nullptr, &cb);
				mbuf[name] = cb;
				printf("name=%s, cb=%p\n", name.c_str(), cb);
			}
			ctx->UpdateSubresource(cb, 0, NULL, c.set_constant.data, 0, 0);
			ctx->VSSetConstantBuffers(slot, 1, &cb);
			ctx->PSSetConstantBuffers(slot, 1, &cb);
		}

		//CMD_SET_VERTEX
		if(type == CMD_SET_VERTEX) {
			auto vb = mbuf[name];
			if(vb == nullptr) {
				D3D11_BUFFER_DESC bd = {
					c.set_vertex.size, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0
				};
				bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				auto hr = dev->CreateBuffer(&bd, nullptr, &vb);
				mbuf[name] = vb;
				printf("name=%s, vb=%p\n", name.c_str(), vb);

				D3D11_MAPPED_SUBRESOURCE msr = {};
				ctx->Map(vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
				if(msr.pData) {
					memcpy(msr.pData, c.set_vertex.data, c.set_vertex.size);
					ctx->Unmap(vb, 0);
				} else {
					printf("error CMD_SET_VERTEX name=%s Can't map\n", name.c_str());
				}
			}

			UINT stride = c.set_vertex.stride_size;
			UINT offset = 0;
			ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
			ctx->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			//ctx->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
			//ctx->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
		}


		//CMD_SET_SHADER
		if(type == CMD_SET_SHADER) {
			auto is_update = c.set_shader.is_update;
			auto pstate = mpstate[name];
			if(is_update) {
				if(pstate.layout) pstate.layout->Release();
				if(pstate.vs) pstate.vs->Release();
				if(pstate.gs) pstate.gs->Release();
				if(pstate.ps) pstate.ps->Release();
				mpstate.erase(name);
				pstate = mpstate[name];
			}
			if(pstate.vs == nullptr) {
				ID3DBlob *pBlobVS = NULL;
				ID3DBlob *pBlobGS = NULL;
				ID3DBlob *pBlobPS = NULL;
				CompileShaderFromFile(name, "VSMain", "vs_4_0", &pBlobVS);
				CompileShaderFromFile(name, "GSMain", "gs_4_0", &pBlobGS);
				CompileShaderFromFile(name, "PSMain", "ps_4_0", &pBlobPS);

				if(pBlobVS)
					dev->CreateVertexShader(
						pBlobVS->GetBufferPointer(), pBlobVS->GetBufferSize(), NULL, &pstate.vs);
				if(pBlobGS)
					dev->CreateGeometryShader(
						pBlobGS->GetBufferPointer(), pBlobGS->GetBufferSize(), NULL, &pstate.gs);
				if(pBlobPS)
					dev->CreatePixelShader(
						pBlobPS->GetBufferPointer(), pBlobPS->GetBufferSize(), NULL, &pstate.ps);
				if(pstate.vs) {
					D3D11_INPUT_ELEMENT_DESC layout[] = {
						{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
						{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
						{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
					};
					dev->CreateInputLayout(
						layout, _countof(layout),
						pBlobVS->GetBufferPointer(), pBlobVS->GetBufferSize(), &pstate.layout);
				}
				if(pstate.layout && pstate.vs && pstate.ps) {
					mpstate[name] = pstate;
				} else {
					if(pstate.layout) pstate.layout->Release();
					if(pstate.vs) pstate.vs->Release();
					if(pstate.gs) pstate.gs->Release();
					if(pstate.ps) pstate.ps->Release();
					mpstate.erase(name);
				}

				if(pBlobVS) pBlobVS->Release();
				if(pBlobGS) pBlobGS->Release();
				if(pBlobPS) pBlobPS->Release();
			}
			if(pstate.vs) {
				ctx->IASetInputLayout(pstate.layout);
				ctx->VSSetShader(pstate.vs, NULL, 0);
				ctx->GSSetShader(pstate.gs, NULL, 0);
				ctx->PSSetShader(pstate.ps, NULL, 0);
			} else {
				printf("Error SET_SHADER name=%s\npstate.vs=%p\npstate.gs=%p\npstate.ps=%p\n",
					name.c_str(), pstate.vs, pstate.gs, pstate.ps);
				Sleep(1000);
			}
		}

		//CMD_CLEAR
		if(type == CMD_CLEAR) {
			auto rtv = mrtv[name];
			if(rtv)
				ctx->ClearRenderTargetView(rtv, c.clear.color.data);
			else
				printf("Error CMD_CLEAR name=%s not found\n", name.c_str());
		}

		//CMD_CLEAR_DEPTH
		if(type == CMD_CLEAR_DEPTH) {
			auto dsv = mdsv[name];
			if(dsv)
				ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, c.clear_depth.value, 0);
			else
				printf("Error CMD_CLEAR name=%s not found\n", name.c_str());
		}
		

		//CMD_SET_INDEX
		if(type == CMD_SET_INDEX) {
			auto ib = mbuf[name];
			if(ib == nullptr) {
				D3D11_BUFFER_DESC bd = {
					c.set_index.size, D3D11_USAGE_DYNAMIC, D3D11_BIND_INDEX_BUFFER, 0, 0, 0
				};
				bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
				auto hr = dev->CreateBuffer(&bd, nullptr, &ib);
				mbuf[name] = ib;
				printf("name=%s, ib=%p size=%d\n", name.c_str(), ib, c.set_index.size);

				D3D11_MAPPED_SUBRESOURCE msr = {};
				ctx->Map(ib, 0, D3D11_MAP_WRITE_DISCARD, 0, &msr);
				if(msr.pData) {
					memcpy(msr.pData, c.set_index.data, c.set_index.size);
					ctx->Unmap(ib, 0);
				} else {
					printf("error CMD_SET_INDEX name=%s Can't map\n", name.c_str());
				}
			}
			//ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT, 0 );
		}
		
		//CMD_DRAW_INDEX
		if(type == CMD_DRAW_INDEX) {
			auto count = c.draw_index.count;
			//ctx->DrawIndexed(count, 0, 0);
			ctx->DrawInstanced(count, 1, 0, 0);
/*
void DrawInstanced(
UINT VertexCountPerInstance,
UINT InstanceCount,
UINT StartVertexLocation,
UINT StartInstanceLocation
);
			*/
		}
	}
	swapchain->Present(1, 0);
}

static LRESULT WINAPI
MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
		case WM_SYSCOMMAND:
			switch ((wParam & 0xFFF0)) {
				case SC_MONITORPOWER:
				case SC_SCREENSAVE:
					return 0;
				default:
					break;
			}
			break;
		case WM_CLOSE:
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		case WM_IME_SETCONTEXT:
			lParam &= ~ISC_SHOWUIALL;
			break;
		case WM_KEYDOWN:
			if (wParam == VK_ESCAPE)
				PostQuitMessage(0);
			break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

HWND
InitWindow(const char *name, int w, int h)
{
	HINSTANCE instance = GetModuleHandle(NULL);
	auto style = WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME;
	auto ex_style = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

	RECT rc = {0, 0, w, h};
	WNDCLASSEX twc = {
		sizeof(WNDCLASSEX), CS_CLASSDC, MsgProc, 0L, 0L, instance,
		LoadIcon(NULL, IDI_APPLICATION), LoadCursor(NULL, IDC_ARROW),
		(HBRUSH)GetStockObject(BLACK_BRUSH), NULL, name, NULL
	};

	RegisterClassEx(&twc);
	AdjustWindowRectEx(&rc, style, FALSE, ex_style);
	rc.right -= rc.left;
	rc.bottom -= rc.top;
	HWND hwnd = CreateWindowEx(
		ex_style, name, name, style,
		(GetSystemMetrics(SM_CXSCREEN) - rc.right) / 2,
		(GetSystemMetrics(SM_CYSCREEN) - rc.bottom) / 2,
		rc.right, rc.bottom, NULL, NULL, instance, NULL);
	ShowWindow(hwnd, SW_SHOW);
	SetFocus(hwnd);
	return hwnd;
};

int
Update()
{
	MSG msg;
	int is_active = 1;

	while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			is_active = 0;
			break;
		} else {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return is_active;
}


void SetBarrierToPresent(std::vector<cmd> & vcmd, std::string name)
{
	cmd c;
	c.type = CMD_SET_BARRIER;
	c.name = name;
	c.set_barrier.to_present = true;
	c.set_barrier.to_rendertarget = false;
	c.set_barrier.to_texture = false;
	vcmd.push_back(c);
}

void SetBarrierToRenderTarget(std::vector<cmd> & vcmd, std::string name)
{
	cmd c;
	c.type = CMD_SET_BARRIER;
	c.name = name;
	c.set_barrier.to_present = false;
	c.set_barrier.to_rendertarget = true;
	c.set_barrier.to_texture = false;
	vcmd.push_back(c);
}

void SetBarrierToTexture(std::vector<cmd> & vcmd, std::string name)
{
	cmd c;
	c.type = CMD_SET_BARRIER;
	c.name = name;
	c.set_barrier.to_present = false;
	c.set_barrier.to_rendertarget = false;
	c.set_barrier.to_texture = true;
	vcmd.push_back(c);
}

void SetRenderTarget(std::vector<cmd> & vcmd, std::string name, int w, int h)
{
	SetBarrierToRenderTarget(vcmd, name);

	cmd c;
	c.type = CMD_SET_RENDER_TARGET;
	c.name = name;
	c.set_render_target.fmt = 0;
	c.set_render_target.rect.x = 0;
	c.set_render_target.rect.y = 0;
	c.set_render_target.rect.w = w;
	c.set_render_target.rect.h = h;
	vcmd.push_back(c);
}

void
SetTexture(std::vector<cmd> & vcmd, std::string name, int slot,
		int w = 0, int h = 0, void *data = nullptr, size_t size = 0, size_t stride = 0)
{
	SetBarrierToTexture(vcmd, name);

	cmd c;
	c.type = CMD_SET_TEXTURE;
	c.name = name;
	c.set_texture.fmt = 0;
	c.set_texture.slot = slot;
	c.set_texture.data = data;
	c.set_texture.size = size;
	c.set_texture.stride = stride;
	c.set_texture.rect.x = 0;
	c.set_texture.rect.y = 0;
	c.set_texture.rect.w = w;
	c.set_texture.rect.h = h;
	vcmd.push_back(c);
}

void SetVertex(std::vector<cmd> & vcmd, std::string name, void *data, size_t size, size_t stride_size)
{
	cmd c;
	c.type = CMD_SET_VERTEX;
	c.name = name;
	c.set_vertex.data = data;
	c.set_vertex.size = size;
	c.set_vertex.stride_size = stride_size;
	vcmd.push_back(c);
}

void SetIndex(std::vector<cmd> & vcmd, std::string name, void *data, size_t size)
{
	cmd c;
	c.type = CMD_SET_INDEX;
	c.name = name;
	c.set_index.data = data;
	c.set_index.size = size;
	vcmd.push_back(c);
}

void SetConstant(std::vector<cmd> & vcmd, std::string name, int slot, void *data, size_t size)
{
	cmd c;
	c.type = CMD_SET_CONSTANT;
	c.name = name;
	c.set_constant.slot = slot;
	c.set_constant.data = data;
	c.set_constant.size = size;
	vcmd.push_back(c);
}

void SetShader(std::vector<cmd> & vcmd, std::string name, bool is_update)
{
	cmd c;
	c.type = CMD_SET_SHADER;
	c.name = name;
	c.set_shader.is_update = is_update;
	vcmd.push_back(c);
}

void ClearRenderTarget(std::vector<cmd> & vcmd, std::string name, vector4 col)
{
	cmd c;
	c.type = CMD_CLEAR;
	c.name = name;
	c.clear.color = col;
	vcmd.push_back(c);
}

void ClearDepthRenderTarget(std::vector<cmd> & vcmd, std::string name, float value)
{
	cmd c;
	c.type = CMD_CLEAR_DEPTH;
	c.name = name;
	c.clear_depth.value = value;
	vcmd.push_back(c);
}


void DrawIndex(std::vector<cmd> & vcmd, std::string name, int start, int count)
{
	cmd c;
	c.type = CMD_DRAW_INDEX;
	c.name = name;
	c.draw_index.start = start;
	c.draw_index.count = count;
	vcmd.push_back(c);
}

void DebugPrint(std::vector<cmd> & vcmd)
{
	for(auto & c : vcmd)
		c.print();
}

struct MatrixStack
{
	enum {
		Max = 32,
	};
	unsigned index = 0;
	DirectX::XMMATRIX data[Max];

	MatrixStack()
	{
		Reset();
	}

	auto & Get(int i)
	{
		return data[i];
	}

	auto & GetTop()
	{
		return Get(index);
	}

	void GetTop(float *a)
	{
		XMStoreFloat4x4((DirectX::XMFLOAT4X4 *)a, XMMatrixTranspose(GetTop()) );
	}

	void Reset()
	{
		index = 0;
		for(int i = 0 ; i < Max; i++) {
			auto & m = Get(i);
			m = DirectX::XMMatrixIdentity();
		}
	}

	void Push()
	{
		auto & mb = GetTop();
		index = (index + 1) % Max;
		auto & m = GetTop();
		m = mb;
		printf("%s:index=%d\n", __FUNCTION__, index);
	}

	void Pop()
	{
		index = (index - 1) % Max;
		if(index < 0)
			printf("%s : under flow\n", __FUNCTION__);
		printf("%s:index=%d\n", __FUNCTION__, index);
	}

	void Load(DirectX::XMMATRIX a)
	{
		auto & m = GetTop();
		m = a;
	}

	void Load(float *a)
	{
		Load(*(DirectX::XMMATRIX *)a);
	}

	void Mult(DirectX::XMMATRIX a)
	{
		auto & m = GetTop();
		m *= a;
	}

	void Mult(float *a)
	{
		Mult(*(DirectX::XMMATRIX *)a);
	}

	void LoadIdentity()
	{
		Load(DirectX::XMMatrixIdentity());
	}

	void LoadLookAt(
			float px, float py, float pz,
			float ax, float ay, float az,
			float ux, float uy, float uz)
	{
		Load(DirectX::XMMatrixLookAtLH({px, py, pz}, {ax, ay, az}, {ux, uy, uz}));
	}

	void LoadPerspective(float ffov, float faspect, float fnear, float ffar)
	{
		Load(DirectX::XMMatrixPerspectiveFovLH(ffov, faspect, fnear, ffar));
	}

	void Translation(float x, float y, float z)
	{
		Mult(DirectX::XMMatrixTranslation(x, y, z));
	}

	void RotateAxis(float x, float y, float z, float angle)
	{
		Mult(DirectX::XMMatrixRotationAxis({x, y, z}, angle));
	}

	void RotateX(float angle)
	{
		Mult(DirectX::XMMatrixRotationX(angle));
	}

	void RotateY(float angle)
	{
		Mult(DirectX::XMMatrixRotationY(angle));
	}

	void RotateZ(float angle)
	{
		Mult(DirectX::XMMatrixRotationZ(angle));
	}

	void Scaling(float x, float y, float z, float angle)
	{
		Mult(DirectX::XMMatrixScaling(x, y, z));
	}

	void Transpose()
	{
		Load(DirectX::XMMatrixTranspose(GetTop()));
	}

	void Print(DirectX::XMMATRIX m)
	{
		int i = 0;
		for(auto & v : m.r) {
			for(auto & e : v.m128_f32) {
				if((i % 4) == 0) printf("\n");
				printf("[%02d]%.4f, ", i++, e);
			}
		}
		printf("\n");
	}

	void Print()
	{
		Print(GetTop());
	}

	void PrintAll()
	{
		for(int i = 0; i < Max; i++) {
			Print(Get(i));
		}
	}
};

struct Image {
	int Width;
	int Height;
	std::vector<unsigned char> vdata;
	Image() {
		Width = 0;
		Height = 0;
	}

	void *GetData() {
		if(!vdata.empty()) return &vdata[0];
		return 0;
	}

	int Save(const char *filename, int width, int height, int bit, void *data) {
		FreeImage_Initialise();
		FIBITMAP *fbmp = FreeImage_Allocate(width, height, bit);
		if(!fbmp) {
			return -1;
		}
		BYTE *bits = FreeImage_GetBits(fbmp);
		if(bits) {
			memcpy(bits, data, width * height * (bit / 8));
		}
		if (!FreeImage_Save(FreeImage_GetFIFFromFilename(filename), fbmp, filename, 0)) {
			printf("Error: FreeImage_Save()\n");
		}
		FreeImage_Unload(fbmp);
		FreeImage_DeInitialise();
	}

	int Load(const char *filename) {
		int ret = -1;
		FreeImage_Initialise();
		FREE_IMAGE_FORMAT format = FreeImage_GetFileType(filename);
		FIBITMAP *image = FreeImage_Load(format, filename);
		FIBITMAP *temp  = 0;
		if(image) {
			temp = FreeImage_ConvertTo32Bits(image);
			if(temp) {
				Width  = FreeImage_GetWidth(temp);
				Height = FreeImage_GetHeight(temp);
				vdata.resize(Width * Height * 4);
				memcpy(&vdata[0], (void *)FreeImage_GetBits(temp), vdata.size());
				ret = 0;
			}
		}
		if(temp) FreeImage_Unload(temp);
		if(image) FreeImage_Unload(image);
		FreeImage_DeInitialise();
		
		return ret;
	}
	
};


struct GeometryData {
	std::map<std::string, std::vector<vertex_format>> mvtx;
	std::map<std::string, std::vector<uint32_t>> mib;
	std::map<std::string, std::string> mmaterial;
};



void
LoadFbxFromFile(std::string name,
	GeometryData & geo)
{
	auto fbxManager = FbxManager::Create();
	auto fbxScene = FbxScene::Create(fbxManager, "fbxscene");
	FbxString FileName(name.c_str());
	auto fbxImporter = FbxImporter::Create(fbxManager, "imp");
	if(fbxImporter->Initialize(FileName.Buffer(), -1, fbxManager->GetIOSettings())) {
		fbxImporter->Import(fbxScene);
		fbxImporter->Destroy();
		FbxGeometryConverter geometryConverter(fbxManager);
		geometryConverter.Triangulate(fbxScene, true);

		//マテリアルの情報を読み込んでしまう
		auto materialCount = fbxScene->GetMaterialCount();
		std::vector<std::string> vmatname;
		for (int i = 0; i < materialCount; ++i)
		{
			auto fbxMaterial = fbxScene->GetMaterial(i);
			
			printf("matname=%s\n", fbxMaterial->GetName());
			auto implementation = GetImplementation(fbxMaterial, FBXSDK_IMPLEMENTATION_CGFX);
			if(!implementation)
			{
				printf("cont\n");
				continue;
			}
			auto rootTable = implementation->GetRootTable();
			auto entryCount = rootTable->GetEntryCount();

			for (int i = 0; i < entryCount; ++i) {
				auto entry = rootTable->GetEntry(i);

				auto fbxProperty = fbxMaterial->FindPropertyHierarchical(entry.GetSource());
				if (!fbxProperty.IsValid())
				{
					fbxProperty = fbxMaterial->RootProperty.FindHierarchical(entry.GetSource());
				}

				auto textureCount = fbxProperty.GetSrcObjectCount<FbxTexture>();
				if (textureCount > 0)
				{
					std::string src = entry.GetSource();

					for (int j = 0; j < fbxProperty.GetSrcObjectCount<FbxFileTexture>(); ++j)
					{
						auto tex = fbxProperty.GetSrcObject<FbxFileTexture>(j);
						std::string texName = tex->GetFileName();
						texName = texName.substr(texName.find_last_of('/') + 1);

						if (src == "Maya|DiffuseTexture")
						{
							printf("texName=%s\n", texName.c_str());
							vmatname.push_back(texName);
						}
						/*
						else if (src == "Maya|NormalTexture")
						{
						  modelMaterial.normalTextureName = texName;
						}
						else if (src == "Maya|SpecularTexture")
						{
						  modelMaterial.specularTextureName = texName;
						}
						else if (src == "Maya|FalloffTexture")
						{
						  modelMaterial.falloffTextureName = texName;
						}
						else if (src == "Maya|ReflectionMapTexture")
						{
						  modelMaterial.reflectionMapTextureName = texName;
						}
						*/
					}
				}
			}
		}

		//シーンメッシュごとに読み込み
		auto meshCount = fbxScene->GetMemberCount<FbxMesh>();
		for (int mi = 0; mi < meshCount; mi++) {
			auto mesh = fbxScene->GetMember<FbxMesh>(mi);
			auto name = mesh->GetNode()->GetName();
			printf("##############################################################################\n");
			printf("Mesh : %s\n", name);
			printf("##############################################################################\n");
			std::vector<vertex_format> vvtx;
			
			std::vector<vector4>  vpos;
			std::vector<vector2>  vuv;
			std::vector<vector3>  vnor;
			std::vector<uint32_t> vib;
			
			//頂点読み込み
			auto uvelem = mesh->GetElementUV(0);
			auto norelem = mesh->GetElementNormal(0);
			{
				auto cp = mesh->GetControlPoints();
				printf("uvelem->GetIndexArray().GetCount()=%d\n", uvelem->GetIndexArray().GetCount());
				printf("norelem->GetIndexArray().GetCount()=%d\n", norelem->GetIndexArray().GetCount());
				for (int i = 0 ; i < mesh->GetControlPointsCount(); i++) {
					auto curcp = cp[i];
					vpos.push_back({float(curcp[0]), float(curcp[1]), float(curcp[2]), 1.0f});
					
					int normal_idx = i;
					/*
					if(norelem->GetReferenceMode() ==  FbxLayerElement::eIndexToDirect)
                    				normal_idx = norelem->GetIndexArray().GetAt(i);
					*/
					auto curnor = norelem->GetDirectArray().GetAt(normal_idx);
					vnor.push_back({float(curnor[0]), float(curnor[1]), float(curnor[2])});

					int uv_idx = i;
					/*
					if(uvelem->GetReferenceMode() ==  FbxLayerElement::eIndexToDirect)
                    				uv_idx = uvelem->GetIndexArray().GetAt(i);
					*/
					auto curuv = uvelem->GetDirectArray().GetAt(uv_idx);
					printf("UV : uv_idx=%d : %.6f %.6f\n", uv_idx, float(curuv[0]), float(curuv[1]));
					vuv.push_back({float(curuv[0]), float(curuv[1])});
				}
			}

			//マテリアル情報表示
			struct matinfo {
				int offset;
				int count;
			};
			std::map<int, matinfo> mmatinfo;
			{
				auto lMaterialIndice = &mesh->GetElementMaterial()->GetIndexArray();
				auto lMaterialMappingMode = mesh->GetElementMaterial()->GetMappingMode();
				if (lMaterialIndice && lMaterialMappingMode == FbxGeometryElement::eByPolygon)
				{
					printf("MAT : FbxGeometryElement::eByPolygon\n");
					auto polygon_count = mesh->GetPolygonCount();
					for ( int poly_index = 0; poly_index < polygon_count; poly_index++) {
						auto lMaterialIndex = lMaterialIndice->GetAt(polygon_count);
						mmatinfo[lMaterialIndex].count++;
					}
				} else {
					printf("MAT : FbxGeometryElement::OTHER\n");
				}
				auto matcount = mmatinfo.size();
				int off = 0;
				for(int i = 0 ; i < matcount; i++) {
					mmatinfo[i].offset = off;
					off += mmatinfo[i].count * 3;
				}
				printf("mmatinfo.size() = %d\n", mmatinfo.size());
			}

			auto idxbuf = mesh->GetPolygonVertices();
			for (int i = 0; i < mesh->GetPolygonVertexCount(); i++)
				vib.push_back(idxbuf[i]);
			/*
			auto polygon_count = mesh->GetPolygonCount();
			int count = 0;
			for ( int poly_index = 0; poly_index < polygon_count; poly_index++)
			
			{
				int lMaterialIndex = 0;
				auto lMaterialIndice = &mesh->GetElementMaterial()->GetIndexArray();
				auto lMaterialMappingMode = mesh->GetElementMaterial()->GetMappingMode();
				if (lMaterialIndice && lMaterialMappingMode == FbxGeometryElement::eByPolygon)
				{
					lMaterialIndex = lMaterialIndice->GetAt(poly_index);
				}
				
				for (int vertex_index = 0; vertex_index < 3; vertex_index++) {
					const int lControlPointIndex = mesh->GetPolygonVertex(poly_index, vertex_index);

					vib.push_back(lControlPointIndex);
					printf("poly_index=%d, vertex_index=%d : idx=%d\n", poly_index, vertex_index, lControlPointIndex);
				}
				printf("\n");
			}
			
			*/
			
			{
				printf("-------------------------------------------------\n");
				auto norelem = mesh->GetElementNormal(0);
				if(norelem->GetMappingMode() == FbxLayerElement::eByControlPoint) {
					printf("NORMAL : eByControlPoint\n");
				}
				if(norelem->GetMappingMode() == FbxLayerElement::eByPolygonVertex) {
					printf("NORMAL : eByPolygonVertex\n");
				}
				auto uvelem = mesh->GetElementUV(0);
				if(uvelem->GetMappingMode() == FbxLayerElement::eByControlPoint) {
					printf("UV : eByControlPoint\n");
				}
				if(uvelem->GetMappingMode() == FbxLayerElement::eByPolygonVertex) {
					printf("UV : eByPolygonVertex\n");
				}
				printf("GetUVLayerCount=%d\n", mesh->GetUVLayerCount());
				printf("IsTriangleMesh=%d\n", mesh->IsTriangleMesh());
				printf("vpos.size()=%d\n", vpos.size());
				printf("vnor.size()=%d\n", vnor.size());
				printf("vib.size()=%d\n", vib.size());
				printf("vuv.size()=%d\n", vuv.size());
				printf("-------------------------------------------------\n");
			}
			
			for ( int i = 0; i < vib.size(); i++ ) {
				vertex_format vfmt;
				auto idx = vib[i];
				vfmt.pos = vpos[idx];
				
				//vfmt.nor = vnor[idx];
				//vfmt.uv = vuv[idx];
				auto nor = norelem->GetDirectArray().GetAt(i);//norelem->GetIndexArray().GetAt(i));
				auto uv = uvelem->GetDirectArray().GetAt(uvelem->GetIndexArray().GetAt(i));
				
				vfmt.nor.x = float(nor[0]);
				vfmt.nor.y = float(nor[1]);
				vfmt.nor.z = float(nor[2]);
				
				
				vfmt.uv.x = float(uv[0]);
				vfmt.uv.y = float(uv[1]);
				printf("uv : %.6f %.6f\n", vfmt.uv.x, vfmt.uv.y);
				vvtx.push_back(vfmt);
			}

			geo.mvtx[name] = vvtx;
			geo.mib[name] = vib;
		}
		

		for(auto & m : geo.mvtx) {
			auto & name = m.first;
			if(vmatname.empty()) {
				geo.mmaterial[name] = "DEFAULT_NORMAL.tga";
			} else {
				geo.mmaterial[name] = vmatname[0];
			}
		}
	}
	fbxManager->Destroy();
}

static GeometryData fbxgeo;
int main() {
	enum {
		Width = 1280,
		Height = 720,
		BufferMax = 2,
		ShaderSlotMax = 8,
		ResourceMax = 1024,
	};

	vertex_format vtx[] = {
		{{-1, 1, 0, 1}, {0,  1,  1}, {-1, 1}},
		{{-1,-1, 0, 1}, {0,  1,  1}, {-1,-1}},
		{{ 1, 1, 0, 1}, {0,  1,  1}, { 1, 1}},
		{{ 1,-1, 0, 1}, {0,  1,  1}, { 1,-1}},
	};
	uint32_t idx[] = {
		0, 1, 2,
		2, 1, 3
	};
	auto appname = "testapp";
	auto hwnd = InitWindow(appname, Width, Height);
	int index = 0;
	static std::vector<uint32_t> vtex;
	for(int y = 0  ; y < 256; y++) {
		for(int x = 0  ; x < 256; x++) {
			vtex.push_back((x ^ y) * 1110);
		}
	}

	struct constdata {
		vector4 time;
		vector4 color;
		matrix4x4 proj;
		matrix4x4 view;
	};
	
	LoadFbxFromFile("Yuko_win_humanoid.fbx", fbxgeo);
	//LoadFbxFromFile("humanoid.fbx", fbxgeo);
	//LoadFbxFromFile("Utc_sum_humanoid.fbx", fbxgeo);
	//LoadFbxFromFile("Alicia_solid_Unity.FBX", fbxgeo);
	printf("Done\n");
	std::map<std::string, Image> mimage;
	for(auto m : fbxgeo.mmaterial) {
		Image img;
		mimage[m.first] = img;
		auto & ref = mimage[m.first];
		ref.Load(m.second.c_str());
	}
	
	constdata cdata;
	MatrixStack stack;
	std::vector<cmd> vcmd;

	auto texname = "testtex";
	auto vbname = "vtx";
	auto ibname = "idx";
	SetTexture(
			vcmd, texname, 0, 256, 256, vtex.data(), vtex.size() * sizeof(uint32_t), 256 * sizeof(uint32_t));
	uint64_t frame = 0;
	auto beforeoffscreenname = "offscreen" + std::to_string(1);
	vector3 pos = {0, 65.999992, 71.999992};
	vector3 dpos = {0, 0, 0};
	while(Update()) {
		auto buffer_index = frame % BufferMax;
		auto indexname = std::to_string(buffer_index);
		auto backbuffername = "backbuffer" + indexname;
		auto offscreenname = "offscreen" + indexname;
		auto constantname = "testconstant" + indexname;

		bool is_update = false;
		dpos.x *= 0.5;
		dpos.y *= 0.5;
		dpos.z *= 0.5;
		if(GetAsyncKeyState(VK_F5) & 0x0001) {
			is_update = true;
		}
		if(GetAsyncKeyState('W') & 0x8000) {
			dpos.z -= 1.0f;
		}
		if(GetAsyncKeyState('S') & 0x8000) {
			dpos.z += 1.0f;
		}
		if(GetAsyncKeyState('A') & 0x8000) {
			dpos.x += 1.0f;
		}
		if(GetAsyncKeyState('D') & 0x8000) {
			dpos.x -= 1.0f;
		}
		if(GetAsyncKeyState('R') & 0x8000) {
			dpos.y += 1.0f;
		}
		if(GetAsyncKeyState('F') & 0x8000) {
			dpos.y -= 1.0f;
		}
		if(GetAsyncKeyState('I') & 0x0001) {
			printf("pos : %f %f %f\n", pos.x, pos.y, pos.z);
		}

		pos.x += dpos.x;
		pos.y += dpos.y;
		pos.z += dpos.z;
		
		
		cdata.time.data[0] = float(frame) / 1000.0f;
		cdata.time.data[1] = 0.0;
		cdata.time.data[2] = 1.0;
		cdata.time.data[3] = 1.0;

		cdata.color.data[0] = 1.0;
		cdata.color.data[1] = 0.0;
		cdata.color.data[2] = 1.0;
		cdata.color.data[3] = 1.0;

		stack.Reset();
		stack.LoadPerspective((3.141592653f / 180.0f) * 90.0f, float(Width) / float(Height), 0.125f, 1024.0f);
		stack.GetTop(cdata.proj.data);
		stack.Reset();
		float tm = float(frame) * 0.01;
		float rad = 1.5;
		stack.LoadLookAt(
				pos.x, pos.y, pos.z,
				pos.x, pos.y, pos.z - 1,
				0, 1, 0);
		stack.GetTop(cdata.view.data);

		
		SetRenderTarget(vcmd, backbuffername, Width, Height);
		ClearRenderTarget(vcmd, backbuffername, {0, 1, 1, 1});
		ClearDepthRenderTarget(vcmd, backbuffername, 1.0f);
		SetShader(vcmd, "test.hlsl", is_update);
		SetConstant(vcmd, constantname, 0, &cdata, sizeof(cdata));
		for(auto & x : fbxgeo.mvtx) {
			auto & vb = fbxgeo.mvtx[x.first];
			auto & ib = fbxgeo.mib[x.first];
			auto & img = mimage[x.first];
			SetTexture(vcmd, x.first + "_mat", 0, img.Width, img.Height, img.GetData(),
				img.Width * img.Height * sizeof(uint32_t), img.Width * sizeof(uint32_t));
			SetTexture(vcmd, x.first + "_mat", 1, img.Width, img.Height, img.GetData(),
				img.Width * img.Height * sizeof(uint32_t), img.Width * sizeof(uint32_t));
			
			SetVertex(vcmd, x.first + "_vb", vb.data(), vb.size() * sizeof(vertex_format), sizeof(vertex_format));
			SetIndex(vcmd, x.first + "_ib", ib.data(), ib.size() * sizeof(uint32_t));
			DrawIndex(vcmd, x.first, 0, ib.size());
		}

		/*
		SetRenderTarget(vcmd, backbuffername, Width, Height);
		ClearRenderTarget(vcmd, backbuffername, {0, 0, 1, 1});
		ClearDepthRenderTarget(vcmd, backbuffername, 1.0f);
		SetShader(vcmd, "present.hlsl", is_update);
		SetTexture(vcmd, offscreenname, 0, 0, 0, nullptr, 0);
		SetVertex(vcmd, vbname, vtx, sizeof(vtx), sizeof(vertex_format));
		SetIndex(vcmd, ibname, idx, sizeof(idx));
		SetConstant(vcmd, constantname, 0, &cdata, sizeof(cdata));
		DrawIndex(vcmd, "presentdraw", 0, _countof(idx));
		*/

		SetBarrierToPresent(vcmd, backbuffername);
		PresentGraphics(appname, vcmd, hwnd, Width, Height, BufferMax, ResourceMax, ShaderSlotMax);
		beforeoffscreenname = offscreenname;
		//DebugPrint(vcmd);
		vcmd.clear();
		frame++;
	}
	PresentGraphics(appname, vcmd, nullptr, Width, Height, BufferMax, ResourceMax, ShaderSlotMax);
	return 0;
}
