##### Injection

首先传入三张 Texture

```cpp
Texture2D<float4> worldPosLSBuffer : register(t0); // light space
Texture2D<float4> normalLSBuffer : register(t1); // light space
Texture2D<float4> fluxLSBuffer : register(t2); // light space
```

也就是 RSM 渲染出来的三张 Texture
绘制出如此多个实例
`commandList->DrawInstanced(RSM_SIZE * RSM_SIZE, 1, 0, 0);`
**让 RSM 的每一个像素都会被遍历到**
因为我们在 RSM 里面的理论是：每一个像素都能成为次级光源，所以在进行这样操作的时候，我们是不能马虎的，必须把每一个次级光源注入进入 Volume 里面

再看看我们的 VS 代码：

```cpp
GS_IN VSMain(VS_IN input)
{
    GS_IN output = (GS_IN)0;

    uint2 RSMsize;
    worldPosLSBuffer.GetDimensions(RSMsize.x, RSMsize.y);
    uint2 rsmCoords = uint2(input.vertexID % RSMsize.x, input.vertexID / RSMsize.x);

    RSMTexel texel = GetRSMTexel(rsmCoords.xy);

    float3 pos = texel.positionWS;
    output.cellPos = float4(int3(pos * LPV_SCALE + float3(LPV_DIM_HALF, LPV_DIM_HALF, LPV_DIM_HALF) + 0.5f * texel.normalWS), 1.0f);
    output.normal = texel.normalWS;
    output.flux = texel.flux;

    return output;
}
```

假定 RSMsize 为 2048，那么绘制调用就会生成 2048\*2048 个点

```cpp
struct VS_IN
{
    uint vertexID : SV_VertexID;
};
```

正好对应了每一个 RSM 像素
这个时候的每个点，都有它对应的 ID，用

````cpp
uint2 RSMsize;
 worldPosLSBuffer.GetDimensions(RSMsize.x, RSMsize.y);
 uint2 rsmCoords = uint2(input.vertexID % RSMsize.x, input.vertexID / RSMsize.x);
 ```
这种方法，显然可以算出每一个点它对应的行，列，的整数化坐标，方便我们去索引每一个RSM像素，比如（2,3）代表第三行的第二个像素
然后调用
```cpp
RSMTexel GetRSMTexel(uint2 coords)
{
 RSMTexel texel = (RSMTexel)0;
 texel.positionWS = worldPosLSBuffer.Load(int3(coords, 0)).xyz;
 texel.normalWS = normalLSBuffer.Load(int3(coords, 0)).xyz;
 texel.flux = fluxLSBuffer.Load(int3(coords, 0)).xyz;
 return texel;
}
````

则正好可以建立对于像素索引，而索引到的 RSM 对应像素的值
于是得到了每个 RSM 像素的：

1. 世界坐标
2. Flux
3. 法线

然后我们开始进行注入

首先对坐标进行放缩，
`output.cellPos = float4(int3(pos * LPV_SCALE + float3(LPV_DIM_HALF, LPV_DIM_HALF, LPV_DIM_HALF) + 0.5f * texel.normalWS), 1.0f);`
使得现在的每一个坐标，都是基于 LPV 坐标系的，**0.5f \* texel.normalWS**仅仅是为了最终的画面效果，进行一点注入偏移

> 如果对于这里不是很理解，可以想想我们上一次对于把值域为[-1,1]转换到[0,1]是如何进行操作的

整体：我们得到了：
已经映射到 LPV 的 RSM 的每一个像素的：1. 坐标，2.光通量,3. 法线
我们把它写成顶点形式的结构体：

```cpp
struct GS_IN
{
    float4 cellPos : SV_POSITION;
    float3 normal : NORMAL;
    float3 flux : FLUX;
};
```

而后进行索引
注意，这里的 cellpos 是基于 LPV 坐标系的世界坐标，我们为什么叫它世界坐标，是为了把它和屏幕坐标区别开来，屏幕坐标是【-1,1】的
那么来看 GS

```cpp
[maxvertexcount(1)]
void GSMain(point GS_IN input[1], inout PointStream<PS_IN> OutputStream)
{
    PS_IN output = (PS_IN)0;
    output.layerID = floor(input[0].cellPos.z);

    output.screenPos = float4((input[0].cellPos.xy + 0.5f) * LPV_DIM_INVERSE * 2.0f - 1.0f, 0.0f, 1.0f);
    output.screenPos.y = -output.screenPos.y;

    output.normal = input[0].normal;
    output.flux = input[0].flux;

    OutputStream.Append(output);
}
```

结合下面的就不难理解上面的关于 LPV 的变换了
screenspace 必须是\[-1,1]的
这里的变换过程我贴出来

> 1. output.screenPos = float4((input[0].cellPos.xy + 0.5f) _ LPV_DIM_INVERSE _ 2.0f - 1.0f, 0.0f, 1.0f);
>    这里是将 cell 在 light space 中的坐标转换到屏幕空间坐标([-1,1]范围)。
>    input[0].cellPos.xy 是 cell 在 light space 的 xy 坐标。
>    加 0.5f 是为了转换到 cell 中心。
>    然后乘以 LPV_DIM_INVERSE 是为了归一化到[0,1]范围。
>    再乘以 2 扩大到[0,2]范围。
>    最后减 1 转换到屏幕空间的[-1,1]范围。
> 2. output.screenPos.y = -output.screenPos.y;
>    这一步是对 y 坐标取反,把正交 projection 转换为 OpenGL 的 coordinate system。
>    也就是对光栅化的 cell 进行上下翻转,转换到以左下角为原点的屏幕空间,使得与视角一致。

所以，目前的所有横纵坐标，都变成了 xy∈\[-1,1]
也恰好是屏幕空间坐标我们达到了遍历的目的，但是这里仅仅只实现了一层 LPV
问题就在于
`output.layerID = floor(input[0].cellPos.z);`
我们了解一下 layerID:**SV_RenderTargetArrayIndex**
英文很好理解它是干什么的，RenderTarget 层，我们看一看 MicroSoft 文档的描述：

> Render-target 数组索引。 应用于几何着色器输出，并指示像素着色器将绘制基元的呈现目标数组切片。 仅当呈现器目标是数组资源时，SV_RenderTargetArrayIndex 才有效。 此语义仅适用于基元;如果基元具有多个顶点，则使用前导顶点中的值。 此值还指示深度/模具视图的哪个数组切片用于读取/写入目的。  
> 可以从几何着色器写入，并由像素着色器读取。  
> 如果  [D3D11_FEATURE_DATA_D3D11_OPTIONS3：：VPAndRTArrayIndexFromAnyShaderFeedingRasterizer](https://learn.microsoft.com/zh-cn/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_d3d11_options3)  为  `true`，则 SV_RenderTargetArrayIndex 应用于为光栅器馈送的任何着色器。

从第 0 层开始，所以是向下取整这一点也很好理解。
xy 负责屏幕的光栅化，而 layerID 这里负责写入到第几层，由于是 3D 纹理，所以很好理解。
而后 GS 标准化输出，非常容易理解

```cpp
PS_OUT PSMain(PS_IN input)
{
    PS_OUT output = (PS_OUT) 0;

    float4 SH_coef = DirCosLobeToSH(input.normal) / PI;
    output.redSH = SH_coef * input.flux.r;
    output.greenSH = SH_coef * input.flux.g;
    output.blueSH = SH_coef * input.flux.b;

    return output;
}
```

这里输入值决定了体素化给哪个体素，甚至是哪一层的哪个体素

```cpp
struct PS_IN
{
    float4 screenPos : SV_POSITION;
    float3 normal : NORMAL;
    float3 flux : FLUX;
    uint layerID : SV_RenderTargetArrayIndex;
};
```

而后尽心 PS 的写入就可以了，先提一点，这里的是纯点的体素化，和我们的 VXGI 的三角形体素化还是有较大差异的。那时候我们要考虑哪个视角去体素化最好
当然都是后话。

然后开始

##### LightPropagation 阶段

首先看看在 DX12 端我们干了什么：

```cpp
	for (int step = 0; step < mLPVPropagationSteps; step++) {
					commandListPropagation->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
commandListPropagation->DrawInstanced(3, LPV_DIM, 0, 0);
				}
```

对，`commandListPropagation->DrawInstanced(3, LPV_DIM, 0, 0);`，这里我们的 3，是绘制一个巨大的三角形，它可以被剔除为一个四边形，具体链接：
https://stackoverflow.com/questions/2588875/whats-the-best-way-to-draw-a-fullscreen-quad-in-opengl-3-2
作者的回复如是说：

> Basically, it is a trick to render a quad (which is a slice of our propagation volume) only with 3 vertices by drawing a bigger triangle which is then "clipped" to a quad. This is efficient and common in full screen passes (we kinda have a fullscreen pass if we think from the "volume" perspective because its our render target)

每个三角形针对每一层，总共针对 32 层
为什么是 32(LPV_DIM)层，因为  
`output.cellPos = float4(int3(pos * LPV_SCALE + float3(LPV_DIM_HALF, LPV_DIM_HALF, LPV_DIM_HALF) + 0.5f * texel.normalWS), 1.0f);`
正好就是\[32,32,32]的中心：\[16,16,16]

```cpp

struct VS_IN
{
    uint vertex : SV_VertexID;
    uint depthIndex : SV_InstanceID;
};
//rendering a "quad" but with 3 vertices only
VS_OUT VSMain(VS_IN input)
{
    VS_OUT output = (VS_OUT) 0;

    if (input.vertex == 0)
        output.screenPos.xy = float2(-1.0, 1.0);
    else if (input.vertex == 1)
        output.screenPos.xy = float2(3.0, 1.0);
    else
        output.screenPos.xy = float2(-1.0, -3.0);

    output.screenPos.zw = float2(0.0, 1.0);
    output.depthIndex = input.depthIndex;

    return output;
}
```

InstanceID 表示层数，VertexID 用于生成三角形，我们每三个顶点进行一次光栅化
也就是说每三个顶点进行一次遍历本层的所有 Volume，因为每次传入有三个顶点，以及一个层标
这里的 GS 主要用作流送光栅化，遍历每一层。

```cpp
struct GS_OUT
{
    float4 screenPos : SV_Position;
    uint depthIndex : SV_RenderTargetArrayIndex;
};
[maxvertexcount(3)]
void GSMain(triangle VS_OUT input[3], inout TriangleStream<GS_OUT> OutputStream)
{
    for (int i = 0; i < 3; i++)
    {
        GS_OUT output = (GS_OUT)0;
        output.depthIndex = input[i].depthIndex;
        output.screenPos = input[i].screenPos;

        OutputStream.Append(output);
    }
}
```

由于是 SV_Position，所以这里是进行了一次遍历，获得的是当前像素的坐标，可以理解为类似于 GBuffer 最后渲染 quad 的那个操作，有异曲同工之处

那么，传入 pixelShader 的是什么呢

```cpp
PS_OUT PSMain(GS_OUT input)
{
    PS_OUT output = (PS_OUT) 0;

    int4 cellIndex = int4(input.screenPos.xy - 0.5f, input.depthIndex, 0);
    SHContribution resultContribution = GetSHGatheringContribution(cellIndex);

    output.redSH = resultContribution.red;
    output.greenSH = resultContribution.green;
    output.blueSH = resultContribution.blue;

    output.acc_redSH = resultContribution.red;
    output.acc_greenSH = resultContribution.green;
    output.acc_blueSH = resultContribution.blue;

    return output;
}
```

作者给出的原文：

> SV_Position semantic which stores our screenPos in shader has to be in normalized coordinates; the pixel shader then receives screenPos which was automatically prepared/converted to pixel coordinates (thus [0,32] range which is the dimension of our volume).

同样 MicroSoft 文档给出的是：

> 声明 SV_Position 以输入到着色器时，它可以指定两种内插模式之一：linearNoPerspective 或 linearNoPerspectiveCentroid，后者会导致在多重采样抗锯齿时提供质心贴靠的 xyzw 值。 在着色器中使用时，SV_Position 描述像素位置。 可用于所有着色器，以获取偏移量为 0.5 的像素中心。

所以不难理解就是遍历的每一个体素！！！
现在我们终于可以遍历每一个体素了，通过 CellIndex，那么最终的 Propagation 也慢慢揭开了面纱

```cpp
float3 getEvalSideDirection(int index, int3 orientation)
{
    const float smallComponent = 0.4472135; // 1 / sqrt(5)
    const float bigComponent = 0.894427; // 2 / sqrt(5)

    const int2 side = cellsides[index];
    float3 tmp = float3(side.x * smallComponent, side.y * smallComponent, bigComponent);
    return float3(orientation.x * tmp.x, orientation.y * tmp.y, orientation.z * tmp.z);
}

float3 getReprojSideDirection(int index, int3 orientation)
{
    const int2 side = cellsides[index];
    return float3(orientation.x * side.x, orientation.y * side.y, 0);
}

SHContribution GetSHGatheringContribution(int4 cellIndex)
{
    SHContribution result = (SHContribution) 0;

    for (int neighbourCell = 0; neighbourCell < 6; neighbourCell++)
    {
        int4 neighbourPos = cellIndex - int4(cellDirections[neighbourCell], 0);

        SHContribution neighbourContribution = (SHContribution) 0;
        neighbourContribution.red = redSH.Load(neighbourPos);
        neighbourContribution.green = greenSH.Load(neighbourPos);
        neighbourContribution.blue = blueSH.Load(neighbourPos);

        // add contribution from main direction
        float4 directionCosLobeSH = DirCosLobeToSH(cellDirections[neighbourCell]);
        float4 directionSH = DirToSH(cellDirections[neighbourCell]);
        result.red += directFaceSubtendedSolidAngle * dot(neighbourContribution.red, directionSH) * directionCosLobeSH;
        result.green += directFaceSubtendedSolidAngle * dot(neighbourContribution.green, directionSH) * directionCosLobeSH;
        result.blue += directFaceSubtendedSolidAngle * dot(neighbourContribution.blue, directionSH) * directionCosLobeSH;

        // contributions from side direction
        for (int face = 0; face < 4; face++)
        {
            float3 evaluatedSideDir = getEvalSideDirection(face, cellDirections[face]);
            float3 reproSideDir = getReprojSideDirection(face, cellDirections[face]);

            float4 evalSideDirSH = DirToSH(evaluatedSideDir);
            float4 reproSideDirCosLobeSH = DirCosLobeToSH(reproSideDir);

            result.red += sideFaceSubtendedSolidAngle * dot(neighbourContribution.red, evalSideDirSH) * reproSideDirCosLobeSH;
            result.green += sideFaceSubtendedSolidAngle * dot(neighbourContribution.green, evalSideDirSH) * reproSideDirCosLobeSH;
            result.blue += sideFaceSubtendedSolidAngle * dot(neighbourContribution.blue, evalSideDirSH) * reproSideDirCosLobeSH;
        }
    }

    return result;
}
```

首先每个 Volume 都是对六个方向的 RGB 的积分
通过 directFaceSubtendedSolidAngle 立体角的占比来实现

```cpp
float4 directionCosLobeSH=DirCosLobeToSH(cellDirections[neighbourCell]);
float4
directionSH = DirToSH(cellDirections[neighbourCell]);
        result.red += directFaceSubtendedSolidAngle * dot(neighbourContribution.red, directionSH) * directionCosLobeSH;
```

而后是 directionCosLobeSH 进行低通滤波
directionSH 让光照对方向进行投影，下面，插入来自知乎窃贼手套的！
灵魂图片！非常感谢！！！
![picture](20230724230057.png)
那么现在逐渐清晰，一个主方向，另外四个副方向，副方向可以约为同一个立体角
每一个 Volume 贡献五个方向，主方向+四个侧面，相邻面不进行计算
还是异曲同工，一个用来 SH，一个用来低通滤波
至此，我们的 Propagation HLSL 部分正式结束！
那么看看 DX12 端干了什么！！！
等等，别忘了我们的 PS

```cpp
PS_OUT PSMain(GS_OUT input)
{
    PS_OUT output = (PS_OUT) 0;

    int4 cellIndex = int4(input.screenPos.xy - 0.5f, input.depthIndex, 0);
    SHContribution resultContribution = GetSHGatheringContribution(cellIndex);

    output.redSH = resultContribution.red;
    output.greenSH = resultContribution.green;
    output.blueSH = resultContribution.blue;

    output.acc_redSH = resultContribution.red;
    output.acc_greenSH = resultContribution.green;
    output.acc_blueSH = resultContribution.blue;

    return output;
}
```

那这个 acc_SH 是干什么吃的，为什么要原封不动的复制我们的东西
且看 DX12：

```cpp
mBlendStateLPVPropagation.IndependentBlendEnable = TRUE;
	for (size_t i = 0; i < 6; i++)
	{
		mBlendStateLPVPropagation.RenderTarget[i].BlendEnable = (i < 3) ? FALSE : TRUE;
		mBlendStateLPVPropagation.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
		mBlendStateLPVPropagation.RenderTarget[i].DestBlend = D3D12_BLEND_ONE;
		mBlendStateLPVPropagation.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
		mBlendStateLPVPropagation.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
		mBlendStateLPVPropagation.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ONE;
		mBlendStateLPVPropagation.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		mBlendStateLPVPropagation.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	}
```

前三个 RT 禁止 Blend，后三个 RT 直接用资源原本值加上当前值
也就是说前三个直接覆盖。那么我们这里就要清楚，我们的 SRV 并没有改变
所以光照只可能传播的更亮，因为是累计光照，而不可能传播的更远！
最后贴上循环代码！！！可见，没有改变过 srv

```cpp
for (int step = 0; step < mLPVPropagationSteps; step++) {
commandListPropagation
->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
commandListPropagation->DrawInstanced(3, LPV_DIM, 0, 0);
				}
```

##### 使用 LPV 的数据

```cpp
// LPV
    if (useLPV)
    {

        float3 lpv = float3(0.0f, 0.0f, 0.0f);
        float4 SHintensity = DirToSH(normal.rgb);
        float3 lpvCellCoords = (worldPos.rgb * LPV_SCALE + float3(LPV_DIM_HALF, LPV_DIM_HALF, LPV_DIM_HALF)) * LPV_DIM_INVERSE;
        float4 lpvIntensity =
        float4(
	    	max(0.0f, dot(SHintensity, redSH.Sample(samplerLPV, lpvCellCoords))),
	    	max(0.0f, dot(SHintensity, greenSH.Sample(samplerLPV, lpvCellCoords))),
	    	max(0.0f, dot(SHintensity, blueSH.Sample(samplerLPV, lpvCellCoords))),
	    1.0f) / PI;

        lpv = LPVAttenuation * min(lpvIntensity.rgb * LPVPower, float3(LPVCutoff, LPVCutoff, LPVCutoff)) * albedo.rgb;

        indirectLighting += lpv * lpvGIPower;
    }
```

别急，sampler 最擅长的是什么，插值！
而且现在的纹理空间的坐标为【0-1】的三次方
可以等价于深度值/层数
最后，用索引到的纹理的 RGB 也就是光照 XAlbedo
那么 LPV 结束！
