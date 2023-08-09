##### Voxelization

首先看HLSL文件
HLSL:**VoxelConeTracingVoxelization.hlsl**
```cpp
GS_IN VSMain(VS_IN input)
{
    GS_IN output = (GS_IN) 0;
    
    output.position = mul(World, float4(input.position.xyz, 1));
    return output;
}
```
```cpp
[maxvertexcount(3)]
void GSMain(triangle GS_IN input[3], inout TriangleStream<PS_IN> OutputStream)
{
    PS_IN output[3];
    output[0] = (PS_IN) 0;
    output[1] = (PS_IN) 0;
    output[2] = (PS_IN) 0;
    
    float3 p1 = input[1].position.rgb - input[0].position.rgb;
    float3 p2 = input[2].position.rgb - input[0].position.rgb;
    float3 n = abs(normalize(cross(p1, p2)));
       
	float axis = max(n.x, max(n.y, n.z));
    
    [unroll]
    for (uint i = 0; i < 3; i++)
    {
output[0].voxelPos = input[i].position.xyz / WorldVoxelScale * 2.0f;
output[1].voxelPos = input[i].position.xyz / WorldVoxelScale * 2.0f;
output[2].voxelPos = input[i].position.xyz / WorldVoxelScale * 2.0f;
        if (axis == n.z)
output[i].position = float4(output[i].voxelPos.x,output[i].voxelPos.y, 0, 1);
        else if (axis == n.x)
output[i].position = float4(output[i].voxelPos.y,output[i].voxelPos.z, 0, 1);
        else
output[i].position = float4(output[i].voxelPos.x,output[i].voxelPos.z, 0, 1);
    
        //output[i].normal = input[i].normal;
        OutputStream.Append(output[i]);
    }
    OutputStream.RestartStrip();
}
```
这是一个处理三角形的GS，为什么是处理三角形呢，因为每3个顶点处理一次GS
并且有voxelPos和position，voxelPos 是一个极小值，它已经缩放到了\[-1,1]
voxelPos/128\*2
screen pos 则是对Voxelpos进行微调，方便光栅化
那么是如何进行微调的呢
且看下面


n为法线，axis为主轴，作用是看法线比较偏向什么轴，那那个方向的值就偏大
也就是`float axis = max(n.x, max(n.y, n.z));`
这是什么道理呢？
假定位于XZ平面上的三角形为ABC

毫无疑问法线就是Y轴，那么主轴肯定是y轴，当主轴是y轴的时候进行了什么操作
让x成为x,z成为y，那么这时候就变成了粉色的新三角形，也就是说，这个视角看上去，肯定可以让三角形最大限度的片元化。
故，**所有的变换都是为了让三角形最大化的投影在XY面上**
![[IMG_1033(20230725-150724).png]]



下面同理：
![[EADBBB69DC90BBA4F9FF6C640398A2DE.png]]
**所有的变换都是为了让三角形最大化的投影在XY面上**
所有的变换都是为了让三角形**最大化**的投影在XY面上
所有的变换都是为了让三角形最大化的投影在**XY面**上
XY面是什么，是我们**光栅化的面**，是我们**体素化的视角**


再看看输出了变换后的三角形如何经理体素化：
```cpp
void PSMain(PS_IN input)
{
    uint width;
    uint height;
    uint depth;
    
    outputTexture.GetDimensions(width, height, depth);
    float3 voxelPos = input.voxelPos.rgb;
    voxelPos.y = -voxelPos.y; 
    
    int3 finalVoxelPos = width * float3(0.5f * voxelPos + float3(0.5f, 0.5f, 0.5f));
    float4 colorRes = float4(DiffuseColor.rgb, 1.0f);
    voxelPos.y = -voxelPos.y; 
    
    float4 worldPos = float4(VoxelToWorld(voxelPos), 1.0f);
    float4 lightSpacePos = mul(ShadowViewProjection, worldPos);
    float4 shadowcoord = lightSpacePos / lightSpacePos.w;
    shadowcoord.rg = shadowcoord.rg * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    float shadow = shadowBuffer.SampleCmpLevelZero(PcfShadowMapSampler, shadowcoord.xy, shadowcoord.z);

    outputTexture[finalVoxelPos] = colorRes * float4(shadow, shadow, shadow, 1.0f);
}
```

这张3D纹理是256\*256\*256的
format 为**DXGI_FORMAT_R8G8B8A8_UNORM**

最大面进行光栅化/体素化
这个PS是针对每一个被三角形覆盖的体素的

`int3 finalVoxelPos = width * float3(0.5f * voxelPos + float3(0.5f, 0.5f, 0.5f));`
规范化到0-1之间。然后规范化到width，也就是体素空间
理清一下思路：首先规范化到\[-1,1]，然后规范化\[0,1]而后规范化到0-width
非常的合理，毕竟此时我们索引的还是以纹理为主，而不是纹理坐标
这里采用的UAV，使用0-width也非常的合理

然后就是world pos
  float4 worldPos = float4(VoxelToWorld(voxelPos), 1.0f);
这里的world pos 和之前的
`input\[i].position.xyz / WorldVoxelScale * 2.0f;`
```cpp
float3 VoxelToWorld(float3 pos)
{
    float3 result = pos;
    result *= WorldVoxelScale;

    return result * 0.5f;
}
```
本质上是一样的

得到的都是world pos，这里的world pos 主要用于阴影
finalVoxelPos则是用于索引3D UAV 


##### MipMap

首先，mipmap有自己的资源形式
创建资源要设定对应的mipmap层级
然后创建视图的时候要在Desc里面设置对应的层级
对每一层的视图是什么

在绘制的时候，直接调用对应层级的mipmap



然后就是**VoxelConeTracingAnisoMipmapPrepareCS.hlsl**

解释一下\[numthreads(8, 8, 8)]是指每个线程组的大小
```cpp
commandList->Dispatch(DivideByMultiple(static_cast<UINT>(cbData.MipDimension), 8u), DivideByMultiple(static_cast<UINT>(cbData.MipDimension), 8u), DivideByMultiple(static_cast<UINT>(cbData.MipDimension), 8u));
```
MipDimension为256/2 为128

128/8=16
也就是执行16,16,16次\[8,8,8]的线程组
也就是会执行\[128,128,128]次
DTid\[x,y,z]<128这个时候就显得非常的有道理



```cpp
[numthreads(8, 8, 8)]
void CSMain(uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= MipDimension || DTid.y >= MipDimension ||	DTid.z >= MipDimension)
        return;
    
    float4 values[8];
    
    int3 sourcePos = DTid * 2;
    [unroll]
    for (int i = 0; i < 8; i++)
        values[i] = voxelTexture.Load(int4(sourcePos + anisoOffsets[i], 0));
    
    voxelTextureResultPosX[DTid] =
        (values[4] + values[0] * (1 - values[4].a) + values[5] + values[1] * (1 - values[5].a) +
        values[6] + values[2] * (1 - values[6].a) +	values[7] + values[3] * (1 - values[7].a)) * 0.25f;
    
    voxelTextureResultNegX[DTid] =
        (values[0] + values[4] * (1 - values[0].a) + values[1] + values[5] * (1 - values[1].a) +
		values[2] + values[6] * (1 - values[2].a) + values[3] + values[7] * (1 - values[3].a)) * 0.25f;
    
    voxelTextureResultPosY[DTid] =
	    (values[2] + values[0] * (1 - values[2].a) + values[3] + values[1] * (1 - values[3].a) +
    	values[7] + values[5] * (1 - values[7].a) +	values[6] + values[4] * (1 - values[6].a)) * 0.25f;
    
    voxelTextureResultNegY[DTid] =
	    (values[0] + values[2] * (1 - values[0].a) + values[1] + values[3] * (1 - values[1].a) +
    	values[5] + values[7] * (1 - values[5].a) + values[4] + values[6] * (1 - values[4].a)) * 0.25f;
    
    voxelTextureResultPosZ[DTid] =
	    (values[1] + values[0] * (1 - values[1].a) + values[3] + values[2] * (1 - values[3].a) +
    	values[5] + values[4] * (1 - values[5].a) + values[7] + values[6] * (1 - values[7].a)) * 0.25f;
    
    voxelTextureResultNegZ[DTid] =
	    (values[0] + values[1] * (1 - values[0].a) + values[2] + values[3] * (1 - values[2].a) +
    	values[4] + values[5] * (1 - values[4].a) + values[6] + values[7] * (1 - values[6].a)) * 0.25f;
    
}
```

`int3 sourcePos = DTid * 2;`总共可以索引到256个，也非常的有道理
因为我们的纹理里面的就是256\*256\*256的格子

那么，我们现在思,现在的Voxel里面是什么：
现在的Voxel是一堆带有颜色的格子，是真正带有RGB的格子,这里我们用的是3D mipmap，而不是clip map，并不能生成类似于NVIDIA演示的那种效果
![[Pasted image 20230725223029.png]]
记住，现在的纹理是，256\*256\*256个带有颜色的格子


但是，我们的生成的新3D纹理是看什么：
是看DTid，所以\*2跟我们一点关系都没有，那么问题来了我们希望看见什么
我们希望看见一张只有128\*128\*128的带有RGB的纹理，此为mipmap
注意，这里的value.a是1
```cpp
voxelTextureResultPosX[DTid] =
        (values[4] + values[0] * (1 - values[4].a) + values[5] + values[1] * (1 - values[5].a) +
        values[6] + values[2] * (1 - values[6].a) +	values[7] + values[3] * (1 - values[7].a)) * 0.25f;
```
一算下来，就是
```cpp
voxelTextureResultPosX[DTid] =
        (values[4]  + values[5]  +values[6] +values[7]  ) * 0.25f;
```

下面开始讲解，为什么我们有6张纹理，为什么要采样四个，又为什么要\*0.25
首先确定，xyz∈\[0,127]
因为新的纹理是128\*128\*128
那么每个格子都有索引，而且是一个int3类型的索引
下面我们以(1,0,0)举例：先熟悉一下我们的坐标系
![[Pasted image 20230725224112.png]]
绿为X轴正方向，
红为Y轴正方向，
蓝为Z轴正方向。
以(1,0,0)举例，则为图中的带有X标志的方块，以方块代指体素
那么*sourcePos*为(2,0,0);那么我们看看，我们的 
`(values[4]  + values[5]  +values[6] +values[7]  )`是哪几个方块
![[Pasted image 20230725224953.png]]
我帮大家标出来了，是四个棕色的方块
**不要想太多，这里只是简单卷积，选取八个方块，然后抉择出有意义四个值**
**不要想太多，这里只是简单卷积，选取八个方块，然后抉择出有意义四个值**
**不要想太多，这里只是简单卷积，选取八个方块，然后抉择出有意义四个值**


那么这时候大家就要问了，为什么是这四个方块
请注意这里我们输出的纹理的名字**voxelTextureResultPosX**
这是什么意思，意思就是**当我们的三角形法线朝向X轴正方向的时候**，那
真正可能反弹给三角形的真实有用的值，就是四个棕色方块。
这里我们并没有把三角形放在X这个方块这里，而是这个棕色方块要贡献到X方块，因为这就是我们的mipmap，也可以说是卷积方式。

X方块的值是这四个方块加权平均回来的
那到时候我们算三角形的时候，
我们就直接读取voxelTextureResultPosX对应层级的方块的值！
而生成X方块的值的，必须是有意义的值！
而怎么才是有意义的值，我要从八个小方块里面，选出四个有意义的值，
那就是从摄像机看向面片看过去，会弹射到的方块就是可以对我有贡献的值。

这里我们假定一个条件：**面片朝向X轴**，而且这里我们**只针对八个方块**，
棕色的四个方块，以及白色的四个和棕色相邻的方块
我们先把用八个里面的哪几个作为有效值弄清楚

那么面片的位置，有两种可能：在棕色方块的左边，在棕色方块的右边
很显然，在棕色方块的左边的话，正好摄像机看向面片，由于法线的原因，弹射给棕色方块，那么这个时候的棕色方块就是有意义的值，
而他的后面的两个方块，被挡住了！所以，棕色方块是有意义的值

> 有人可能觉得，这样是不正确的，因为眼睛看见东西是光线打入眼睛，那么同理，你只要看光线打给棕色方块左面的时候，会不会弹射给面片，很显然，如果面片的主轴不是X轴正方向的话，是很难弹射给面片造成贡献的


这里依旧是面片朝向X轴正方向的情况下
如果在棕色方块的右边，那么很显然，贡献不到，那么我们这个时候选取的八个方块，就不是这八个方块了，而是另外八个方块，
并且由于3D mipmap原理的原因，这八个方块和我们的八个方块不重合，我们就去寻找合适的八个方块。

所以这也验证了，我们为什么说这里是简单的卷积，不涉及任何位置的关系，
因为铁律：你这个方块的位置不能满足我的要求，那我就再找一个位置可以满足我的要求的方块。
反正法线已经定了，我肯定是在这张纹理里面找一个位置合适的，还记得我们的纹理的名字吗？
**voxelTextureResultPosX**


> 再补充一句：我用不用你的是我的事，生不生成是你的事，你必须生成好所有卷积
以便于我去使用。


同样，副向的，我们就正好相反的面片法线方向
```cpp
    voxelTextureResultNegX[DTid] =
        (values[0] +  values[1] + values[2] +  values[3] ) * 0.25f;
```

正好选取的就是这四个
![[Pasted image 20230725234321.png]]
同样的道理，假如面片在棕色处，朝向X轴负方向
会不会有贡献？不会有贡献，那怎么办，重新选取新的八个里面，面朝面片的四个
这里我们已经把面片朝向固定了，所以直接对所有的8方块进行如此的真实值提取
然后卷积。

整个程序，都是干了这件事！

生成了六张图，非常的完美！本个HLSL我们结束！

下面的MainCS也是作为生成mip层级


##### ConeTracing
我们直接看VCTPS


先看看VS，也是基于quad的
```cpp
PS_IN VSMain(VS_IN input)
{
    PS_IN result = (PS_IN)0;

    result.position = float4(input.position.xyz, 1);
    result.uv = input.uv;
    
    return result;
}
```
现在已经见怪不怪了


然后就是
```cpp
PS_OUT PSMain(PS_IN input)
{
    PS_OUT output = (PS_OUT) 0;
    float2 inPos = input.position.xy;
    
    float3 normal = normalize(normalBuffer[inPos * UpsampleRatio].rgb);
    float4 worldPos = worldPosBuffer[inPos * UpsampleRatio];
    float4 albedo = albedoBuffer[inPos * UpsampleRatio];
    
    uint width;
    uint height;
    uint depth;
    voxelTexture.GetDimensions(width, height, depth);
    
    float ao = 0.0f;
    float4 indirectDiffuse = CalculateIndirectDiffuse(worldPos.rgb, normal.rgb, ao, width);
    float4 indirectSpecular = CalculateIndirectSpecular(worldPos.rgb, normal.rgb, albedo, width);

    output.result = saturate(float4(indirectDiffuse.rgb * albedo.rgb + indirectSpecular.rgb, ao));
    return output;
}
```
前面的采样法线反射率和世界坐标都非常的常规
那么这里能关注的一点就是：这是基于摄像机视角的
**CalculateIndirectDiffuse**让我看看diffuse 先
先提一点，这里trace 6个Cone，Diffuse物体用6个Cone覆盖全部上球面
```cpp
float4 CalculateIndirectDiffuse(float3 worldPos, float3 normal, out float ao, uint voxelResolution)
{
    float4 result;
    float3 coneDirection;
    
    float3 upDir = float3(0.0f, 1.0f, 0.0f);
    if (abs(dot(normal, upDir)) == 1.0f)
        upDir = float3(0.0f, 0.0f, 1.0f);

    float3 right = normalize(upDir - dot(normal, upDir) * normal);
    float3 up = cross(right, normal);
    
    float finalAo = 0.0f;
    float tempAo = 0.0f;
    
    for (int i = 0; i < NUM_CONES; i++)
    {
        coneDirection = normal;
        coneDirection += diffuseConeDirections[i].x * right + diffuseConeDirections[i].z * up;
        coneDirection = normalize(coneDirection);

        result += TraceCone(worldPos, normal, coneDirection, coneAperture, tempAo, true, voxelResolution) * diffuseConeWeights[i];
        finalAo += tempAo * diffuseConeWeights[i];
    }
    
    ao = finalAo;
    
    return IndirectDiffuseStrength * result;
}
```

画出图片得Right以及 normal 和up
这里不要把up理解为向上，理解为垂直于这两个的方向就可以了
![[6AB12A220D5EB9EEB3FBFAC54D02E784.png]]

`coneDirection += diffuseConeDirections[i].x * right + diffuseConeDirections[i].z * up;`

在X方向和Z方向进行値的改变，以达到整体的Z在基于法线的情况下改变
仔细观察diffuseConeDirections的第一个值是(0,1,0)就不难发现，而且是+=
然后看一下TraceCone
voxelResolution是256
可得，第一个voxelWorldSize就是1/2
1. 首先对startPos进行小范围的向上偏移，防止自遮挡现象
2. direction是要trace 的方向的标准向量，可以作为权重，毕竟也可以算作和法线的Cosθ
3. 判断条件是，光照不足够并且没有超出最远的Raymatching距离
这里不是严格的raymatching，因为没有改变起点，都是从同一点出发的。
```cpp
float4 TraceCone(float3 pos, float3 normal, float3 direction, float aperture, out float occlusion, bool calculateAO, uint voxelResolution)
{
    float lod = 0.0;
    float4 color = float4(0.0f, 0.0f, 0.0f, 0.0f);

    occlusion = 0.0f;
    float voxelWorldSize = WorldVoxelScale / voxelResolution;
    float dist = voxelWorldSize;
    float3 startPos = pos + normal * dist;
    
    float3 weight = direction * direction;

    while (dist < MaxConeTraceDistance && color.a < 0.9f)
    {
        float diameter = 2.0f * aperture * dist;
        float lodLevel = log2(diameter / voxelWorldSize);
        float4 voxelColor = GetVoxel(startPos + dist * direction, weight, lodLevel, direction.x > 0.0, direction.y > 0.0, direction.z > 0.0);
    
        // front-to-back
        color += (1.0 - color.a) * voxelColor;
        if (occlusion < 1.0f && calculateAO)
            occlusion += ((1.0f - occlusion) * voxelColor.a) / (1.0f + AOFalloff * diameter);
        
        dist += diameter * SamplingFactor;
    }

    return color;
}
```
解释一下diameter是什么
![[Pasted image 20230726005140.png]]
https://github.com/jose-villegas/VCTRenderer#voxel-illumination
Cone:60°
half of it 30°
根据dist步长，也就是垂线
获得另一条垂边的两倍，也就是直径
一开始选择第0层，然后根据直径，选择和其匹配的体素大小对应的纹理的
对应体素

此时也要选择面片，或者说是像素的法线正方向
正如我们体素化的时候干的
`float3 voxelTextureUV = worldPosition / WorldVoxelScale * 2.0f;`
当时也翻转了y轴`voxelTextureUV.y = -voxelTextureUV.y;`
`改变范围+轻微偏移voxelTextureUV = voxelTextureUV * 0.5f + 0.5f + offset;`
```cpp
float4 GetVoxel(float3 worldPosition, float3 weight, float lod, bool posX, bool posY, bool posZ)
{
    float3 offset = float3(VoxelSampleOffset, VoxelSampleOffset, VoxelSampleOffset);
    float3 voxelTextureUV = worldPosition / WorldVoxelScale * 2.0f;
    voxelTextureUV.y = -voxelTextureUV.y;
    voxelTextureUV = voxelTextureUV * 0.5f + 0.5f + offset;
    
    return GetAnisotropicSample(voxelTextureUV, weight, lod, posX, posY, posZ);
}
```
我们的每个mipmap的上限不一样，
从256或者说是第一次各向异性纹理的128，变成了8
width >>= anisoLevel;
```cpp
float4 GetAnisotropicSample(float3 uv, float3 weight, float lod, bool posX, bool posY, bool posZ)
{
    int anisoLevel = max(lod - 1.0f, 0.0f);
    
    uint width;
    uint height;
    uint depth;
    voxelTexturePosX.GetDimensions(width, height, depth);
    
    width >>= anisoLevel;
    height >>= anisoLevel;
    depth >>= anisoLevel;
    
    float4 anisoSample = 
    weight.x * ((posX) ? voxelTexturePosX.SampleLevel(LinearSampler, uv, anisoLevel) : voxelTextureNegX.SampleLevel(LinearSampler, uv, anisoLevel)) +
    weight.y * ((posY) ? voxelTexturePosY.SampleLevel(LinearSampler, uv, anisoLevel) : voxelTextureNegY.SampleLevel(LinearSampler, uv, anisoLevel)) +
    weight.z * ((posZ) ? voxelTexturePosZ.SampleLevel(LinearSampler, uv, anisoLevel) : voxelTextureNegZ.SampleLevel(LinearSampler, uv, anisoLevel));

    if (lod < 1.0f)
    {      
        float4 baseColor = voxelTexture.SampleLevel(LinearSampler, uv, 0);
        anisoSample = lerp(baseColor, anisoSample, clamp(lod, 0.0f, 1.0f));
    }

    return anisoSample;
}
```

voxelTexture就是原始的那张256的图
也就是层级为<1的时候，进行多层之间的插值
0层是128，这里是为了提高效果和精度，
glossy对于近处的物体效果要好点也可以理解。

这里的采样器是三线性过滤，多层之间通过mipmap自动生成
dist += diameter * SamplingFactor;改变步长
继续贡献
  `color += (1.0 - color.a) * voxelColor;`
  保证Diffuse不会有太多的trace，weight是改变这里的color.a的关键
  因为采样的时候根据了weight进行加权
  得到的a可能并不是1
  这么做的道理是什么呢
  
specular部分比较简单，直接写在注释里了
```cpp
float4 CalculateIndirectSpecular(float3 worldPos, float3 normal, float4 specular, uint voxelResolution)
{
    float4 result;
    float3 viewDirection = normalize(CameraPos.rgb - worldPos);
    //直接反射得到cone 方向
    float3 coneDirection = normalize(reflect(-viewDirection, normal));
    //根据粗糙度，也就是albedo 的a
    float aperture = clamp(tan(PI * 0.5f * (1.0f - specular.a)), specularOneDegree * specularMaxDegreesCount, PI);

    float ao = -1.0f;
    result = TraceCone(worldPos, normal, coneDirection, aperture, ao, false, voxelResolution);
    
    return IndirectSpecularStrength * result * float4(specular.rgb, 1.0f) * specular.a;
}
```


TranceCone 的时候这里的AO真的有道理吗？
越靠近法线的地方的AO越大
 ```cpp
 if (occlusion < 1.0f && calculateAO)
            occlusion += ((1.0f - occlusion) * voxelColor.a) / (1.0f + AOFalloff * diameter);
```

以及越靠近法线的方向越少Trace
```cpp
color += (1.0 - color.a) * voxelColor;
 float4 anisoSample = 
    weight.x * ((posX) ? voxelTexturePosX.SampleLevel(LinearSampler, uv, anisoLevel) : voxelTextureNegX.SampleLevel(LinearSampler, uv, anisoLevel)) +
    weight.y * ((posY) ? voxelTexturePosY.SampleLevel(LinearSampler, uv, anisoLevel) : voxelTextureNegY.SampleLevel(LinearSampler, uv, anisoLevel)) +
    weight.z * ((posZ) ? voxelTexturePosZ.SampleLevel(LinearSampler, uv, anisoLevel) : voxelTextureNegZ.SampleLevel(LinearSampler, uv, anisoLevel));
```
非常的耐人寻味

最后像GBuffer一样直接采样就可以了
  ```cpp
  //VCT
    if (useVCT)
    {
        uint gWidth = 0;
        uint gHeight = 0;
        albedoBuffer.GetDimensions(gWidth, gHeight);
        float4 vct = vctBuffer.Sample(BilinearSampler, inPos * float2(1.0f / gWidth, 1.0f / gHeight));
        
        indirectLighting += vct.rgb * vctGIPower;
        if (!useVCTDebug)
            ao = 1.0f - vct.a;
    }
```  
    








