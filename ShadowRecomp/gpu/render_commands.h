
#pragma once

#include "renderstate.h"
#include "video.h"
#include <gpu/ubershaders/shader_variant.h>



enum class RenderCommandType
{
	SetRenderState,
	DestructResource,
	UnlockTextureRect,
	UnlockBuffer16,
	UnlockBuffer32,
	DrawImGui,
	EndFrame,
	StretchRect,
	SetRenderTarget,
	SetDepthStencilSurface,
	Clear,
	SetViewport,
	SetTexture,
	//SetScissorRect,
	SetTextureStageState,
	SetBooleans,
	SetVertexShaderConstants,
	SetVertexShaderVariant,
	SetPixelShaderConstants,
	AddPipeline,
	DrawPrimitive,
	DrawIndexedPrimitive,
	DrawPrimitiveUP,
	SetVertexDeclaration,
	SetVertexShader,
	SetStreamSource,
	SetStreamSourceUP, // for dynamic variation from RenderWare
	SetIndices,
	SetPixelShader,
	StagedCopy,

};

struct RenderCommand
{
	RenderCommandType type;

	union
	{
		struct
		{
			GuestRenderState type;
			uint32_t value;
		} setRenderState;

		struct
		{
			GuestResource* resource;
		} destructResource;

		struct
		{
			bool gameThreadWaits;
		} endFrame;

		struct
		{
			GuestTexture* texture;
		} unlockTextureRect;

		struct
		{
			GuestBuffer* indexBuffer;
		} unlockBuffer;

		struct
		{
			GuestBuffer* dst;
			const RenderBuffer* srcBuffer;
			uint64_t srcOffset;
			uint32_t size;
		} stagedCopy;

		struct
		{
			GuestBuffer* indexBuffer;
		} setIndices;

		struct
		{
			GuestSurface* renderTarget;
		} setRenderTarget;

		struct
		{
			GuestSurface* depthStencil;
		} setDepthStencilSurface;


		struct
		{
			uint32_t flags;
			float color[4];
			float z;
			uint32_t stencil;
			bool isFixed;
		} clear;


		struct
		{
			float x;
			float y;
			float width;
			float height;
			float minDepth;
			float maxDepth;
			bool isFixed;
		} setViewport;

		struct
		{
			uint32_t index;
			GuestTexture* texture;
			bool isFixed;
		} setTexture;

		struct
		{
			uint32_t primitiveType;
			uint32_t startVertex;
			uint32_t primitiveCount;
			bool isFixed;
		} drawPrimitive;

		struct
		{
			uint32_t primitiveType;
			int32_t baseVertexIndex;
			uint32_t startIndex;
			uint32_t primCount;
			int16_t* pIndexData;
			GuestBuffer* indexBuffer;
			bool isFixed;
		} drawIndexedPrimitive;

		struct
		{
			uint32_t primitiveType;
			uint32_t primitiveCount;
			uint8_t* vertexStreamZeroData;
			uint32_t vertexStreamZeroSize;
			uint32_t vertexStreamZeroStride;
			bool isFixed;
			uint32_t ID;
			DrawCallCaller caller;
			uint32_t threadID;
		} drawPrimitiveUP;


		struct
		{
			GuestVertexDeclaration* vertexDeclaration;
		} setVertexDeclaration;

		struct
		{
			GuestShader* shader;
			uint32_t handle;
			VertexShaderMode type;
		} setVertexShader;

		struct
		{
			uint32_t StreamNumber;
			GuestBuffer* vertexBuffer;
			uint32_t stride;
		} setStreamSource;

		struct
		{
			uint32_t StreamNumber;
			uint8_t* vertexData;
			uint32_t size;
			uint32_t stride;
		} setStreamSourceUP;


		struct
		{
			GuestShader* shader;
			bool isFixed;
		} setPixelShader;

		struct
		{
			XXH64_hash_t hash;
			RenderPipeline* pipeline;
		} addPipeline;

		struct
		{
			X_D3DTRANSFORMSTATETYPE state;
			D3DMATRIX matrix;
		} setTransform;

		struct
		{
			uint32_t D3DTSS_ADDRESSU;
			uint32_t D3DTSS_ADDRESSV;
			uint32_t D3DTSS_ADDRESSW;
			uint32_t D3DTSS_MAGFILTER;
			uint32_t D3DTSS_MINFILTER;
			uint32_t D3DTSS_MIPFILTER;
			uint32_t D3DTSS_BORDERCOLOR;
			//shared for pixel shaders
			uint32_t D3DTSS_COLOROP;
			uint32_t D3DTSS_COLORARG0;
			uint32_t D3DTSS_COLORARG1;
			uint32_t D3DTSS_COLORARG2;
			uint32_t D3DTSS_ALPHAOP;
			uint32_t D3DTSS_ALPHAARG0;
			uint32_t D3DTSS_ALPHAARG1;
			uint32_t D3DTSS_ALPHAARG2;
			uint32_t D3DTSS_RESULTARG;
			uint32_t D3DTSS_BUMPENVMAT00;
			uint32_t D3DTSS_BUMPENVMAT01;
			uint32_t D3DTSS_BUMPENVMAT11;
			uint32_t D3DTSS_BUMPENVMAT10;
			uint32_t D3DTSS_BUMPENVLSCALE;
			uint32_t D3DTSS_BUMPENVLOFFSET;
			uint32_t D3DTSS_TEXCOORDINDEX;
			uint32_t index;
		} SetTextureStageState;

		struct
		{
			uint8_t* memory;
			uint32_t index;
			uint32_t size;
			bool isFixedFunc;
		} setVertexShaderConstants;

		struct
		{
			ShaderVariantKey key;
		} setVertexShaderVariant;

		struct
		{
			uint8_t* memory;
			uint32_t index;
			uint32_t size;
		} setPixelShaderConstants;
	};
};

extern moodycamel::BlockingConcurrentQueue<RenderCommand> g_renderQueue;


struct LocalRenderCommandQueue
{
	RenderCommand commands[20];
	uint32_t count = 0;

	void submit()
	{
		if (count == 0)
			return;

		g_renderQueue.enqueue_bulk(commands, count);
		count = 0;
	}

	RenderCommand& enqueue()
	{
		if (count == std::size(commands))
			submit();
		return commands[count++];
	}
};

struct PendingRenderCommands : LocalRenderCommandQueue
{
	~PendingRenderCommands()
	{
		submit();
	}
};

extern thread_local PendingRenderCommands g_pendingRenderCommands;

inline void SendRenderCommand(const RenderCommand& cmd)
{
	g_pendingRenderCommands.enqueue() = cmd;
	g_pendingRenderCommands.submit();
}