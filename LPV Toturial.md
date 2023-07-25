##### Injection
首先传入三张Texture
```cpp
Texture2D<float4> worldPosLSBuffer : register(t0); // light space
Texture2D<float4> normalLSBuffer : register(t1); // light space
Texture2D<float4> fluxLSBuffer : register(t2); // light space
```
也就是RSM渲染出来的三张Texture
绘制出如此多个实例
`commandList->DrawInstanced(RSM_SIZE * RSM_SIZE, 1, 0, 0);`
**让RSM的每一个像素都会被遍历到**
因为我们在RSM里面的理论是：每一个像素都能成为次级光源，所以在进行这样操作的时候，我们是不能马虎的，必须把每一个次级光源注入进入Volume里面

再看看我们的VS代码：
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
假定RSMsize为2048，那么绘制调用就会生成2048\*2048个点
```cpp
struct VS_IN
{
    uint vertexID : SV_VertexID;
};
```
正好对应了每一个RSM像素
这个时候的每个点，都有它对应的ID，用
   ```cpp
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
```
则正好可以建立对于像素索引，而索引到的RSM对应像素的值
于是得到了每个RSM像素的：
1. 世界坐标
2. Flux
3. 法线

然后我们开始进行注入

首先对坐标进行放缩，
`output.cellPos = float4(int3(pos * LPV_SCALE + float3(LPV_DIM_HALF, LPV_DIM_HALF, LPV_DIM_HALF) + 0.5f * texel.normalWS), 1.0f);`
使得现在的每一个坐标，都是基于LPV坐标系的，**0.5f * texel.normalWS**仅仅是为了最终的画面效果，进行一点注入偏移
> 如果对于这里不是很理解，可以想想我们上一次对于把值域为[-1,1]转换到[0,1]是如何进行操作的

整体：我们得到了：
已经映射到LPV的RSM的每一个像素的：1. 坐标，2.光通量,3. 法线
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
注意，这里的cellpos是基于LPV坐标系的世界坐标，我们为什么叫它世界坐标，是为了把它和屏幕坐标区别开来，屏幕坐标是【-1,1】的
那么来看GS
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
结合下面的就不难理解上面的关于LPV的变换了
screenspace 必须是\[-1,1]的
这里的变换过程我贴出来
>1. output.screenPos = float4((input[0].cellPos.xy + 0.5f) * LPV_DIM_INVERSE * 2.0f - 1.0f, 0.0f, 1.0f);
这里是将cell在light space中的坐标转换到屏幕空间坐标([-1,1]范围)。
input[0].cellPos.xy是cell在light space的xy坐标。
加0.5f是为了转换到cell中心。
然后乘以LPV_DIM_INVERSE是为了归一化到[0,1]范围。
再乘以2扩大到[0,2]范围。
最后减1转换到屏幕空间的[-1,1]范围。
> 2. output.screenPos.y = -output.screenPos.y;
这一步是对y坐标取反,把正交 projection 转换为 OpenGL 的 coordinate system。
也就是对光栅化的cell进行上下翻转,转换到以左下角为原点的屏幕空间,使得与视角一致。 

所以，目前的所有横纵坐标，都变成了xy∈\[-1,1]
也恰好是屏幕空间坐标我们达到了遍历的目的，但是这里仅仅只实现了一层LPV
问题就在于
  `output.layerID = floor(input[0].cellPos.z);`
  我们了解一下layerID:**SV_RenderTargetArrayIndex**
  英文很好理解它是干什么的，RenderTarget层，我们看一看MicroSoft文档的描述：
>   Render-target 数组索引。 应用于几何着色器输出，并指示像素着色器将绘制基元的呈现目标数组切片。 仅当呈现器目标是数组资源时，SV_RenderTargetArrayIndex才有效。 此语义仅适用于基元;如果基元具有多个顶点，则使用前导顶点中的值。 此值还指示深度/模具视图的哪个数组切片用于读取/写入目的。  
可以从几何着色器写入，并由像素着色器读取。  
如果 [D3D11_FEATURE_DATA_D3D11_OPTIONS3：：VPAndRTArrayIndexFromAnyShaderFeedingRasterizer](https://learn.microsoft.com/zh-cn/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_d3d11_options3) 为 `true`，则SV_RenderTargetArrayIndex应用于为光栅器馈送的任何着色器。

从第0层开始，所以是向下取整这一点也很好理解。
xy负责屏幕的光栅化，而layerID这里负责写入到第几层，由于是3D纹理，所以很好理解。
而后GS标准化输出，非常容易理解

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
而后尽心PS的写入就可以了，先提一点，这里的是纯点的体素化，和我们的VXGI的三角形体素化还是有较大差异的。那时候我们要考虑哪个视角去体素化最好
当然都是后话。

然后开始
##### LightPropagation阶段
首先看看在DX12端我们干了什么：
```cpp
	for (int step = 0; step < mLPVPropagationSteps; step++) {
					commandListPropagation->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
commandListPropagation->DrawInstanced(3, LPV_DIM, 0, 0);
				}
```
对，`commandListPropagation->DrawInstanced(3, LPV_DIM, 0, 0);`，这里我们的3，是绘制一个巨大的三角形，它可以被剔除为一个四边形，具体链接：
https://stackoverflow.com/questions/2588875/whats-the-best-way-to-draw-a-fullscreen-quad-in-opengl-3-2 
作者的回复如是说：
> Basically, it is a trick to render a quad (which is a slice of our propagation volume) only with 3 vertices by drawing a bigger triangle which is then "clipped" to a quad. This is efficient and common in full screen passes (we kinda have a fullscreen pass if we think from the "volume" perspective because its our render target)

每个三角形针对每一层，总共针对32层
为什么是32(LPV_DIM)层，因为   
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
InstanceID 表示层数，VertexID用于生成三角形，我们每三个顶点进行一次光栅化
也就是说每三个顶点进行一次遍历本层的所有Volume，因为每次传入有三个顶点，以及一个层标
这里的GS主要用作流送光栅化，遍历每一层。
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
由于是SV_Position，所以这里是进行了一次遍历，获得的是当前像素的坐标，可以理解为类似于GBuffer最后渲染quad的那个操作，有异曲同工之处

那么，传入pixelShader的是什么呢

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
>   SV_Position semantic which stores our screenPos in shader has to be in normalized coordinates; the pixel shader then receives screenPos which was automatically prepared/converted to pixel coordinates (thus [0,32] range which is the dimension of our volume).

同样MicroSoft文档给出的是：
> 声明SV_Position以输入到着色器时，它可以指定两种内插模式之一：linearNoPerspective 或 linearNoPerspectiveCentroid，后者会导致在多重采样抗锯齿时提供质心贴靠的 xyzw 值。 在着色器中使用时，SV_Position描述像素位置。 可用于所有着色器，以获取偏移量为 0.5 的像素中心。


所以不难理解就是遍历的每一个体素！！！
现在我们终于可以遍历每一个体素了，通过CellIndex，那么最终的Propagation也慢慢揭开了面纱
  
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

首先每个Volume都是对六个方向的RGB的积分
通过directFaceSubtendedSolidAngle立体角的占比来实现
```cpp
float4 directionCosLobeSH=DirCosLobeToSH(cellDirections[neighbourCell]);
float4 
directionSH = DirToSH(cellDirections[neighbourCell]);
        result.red += directFaceSubtendedSolidAngle * dot(neighbourContribution.red, directionSH) * directionCosLobeSH;
```
而后是directionCosLobeSH进行低通滤波
directionSH让光照对方向进行投影，下面，插入来自知乎窃贼手套的！
灵魂图片！非常感谢！！！
![Pasted image 20230724230057](https://github.com/Xiyinyue/DX12GIRenderBased-on-SandBoxGI/assets/83278582/58c75265-df7a-45ea-b49e-3ecbe8474004)

那么现在逐渐清晰，一个主方向，另外四个副方向，副方向可以约为同一个立体角
每一个Volume贡献五个方向，主方向+四个侧面，相邻面不进行计算
还是异曲同工，一个用来SH，一个用来低通滤波
至此，我们的Propagation HLSL部分正式结束！
那么看看DX12端干了什么！！！
等等，别忘了我们的PS
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
那这个acc_SH是干什么吃的，为什么要原封不动的复制我们的东西
且看DX12：
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
前三个RT 禁止Blend，后三个RT直接用资源原本值加上当前值
也就是说前三个直接覆盖。那么我们这里就要清楚，我们的SRV并没有改变
所以光照只可能传播的更亮，因为是累计光照，而不可能传播的更远！
最后贴上循环代码！！！可见，没有改变过srv
```cpp
for (int step = 0; step < mLPVPropagationSteps; step++) {
commandListPropagation
->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
commandListPropagation->DrawInstanced(3, LPV_DIM, 0, 0);
				}
```




##### 使用LPV的数据

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

别急，sampler最擅长的是什么，插值！
而且现在的纹理空间的坐标为【0-1】的三次方
可以等价于深度值/层数
最后，用索引到的纹理的RGB也就是光照XAlbedo
那么LPV结束！
