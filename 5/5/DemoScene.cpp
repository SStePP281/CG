#include "DemoScene.h"
#include "World.h"
#include "AssetManager.h"
#include "PathManager.h"
#include <random>

using namespace DirectX;

static void CreateLights(World& world)
{
	constexpr int LIGHT_COUNT = 0;

	auto RandomFloat = [&](float min, float max) -> float
		{
			static std::mt19937 gen(std::random_device{}());
			std::uniform_real_distribution<float> dis(min, max);
			return dis(gen);
		};

	//{
	//	LightObject dirLight = {};

	//	dirLight.Position = { 1.0f, 5.0f, 0.0f };

	//	XMFLOAT3 dir = { 1.0f, -0.15f, 0.0f };
	//	XMStoreFloat3(&dir, XMVector3Normalize(XMLoadFloat3(&dir)));
	//	dirLight.Direction = dir;

	//	dirLight.Color = { 3.0f, 2.6f, 2.0f };

	//	dirLight.FalloffStart = 0.f;
	//	dirLight.FalloffEnd = 0.f;
	//	dirLight.SpotPower = 0.f;

	//	dirLight.Type = LightType::Directional;

	//	dirLight.Active = true;

	//	world.Lights().push_back(dirLight);
	//}

	for (int i = 0; i < LIGHT_COUNT; ++i)
	{
		float r = RandomFloat(0, 1);
		LightType type = LightType::Point;

		XMFLOAT3 pos = {
			RandomFloat(-3.f, 3.f),
			RandomFloat(-3.f, 3.f),
			RandomFloat(-3.f, 3.f)
		};

		XMFLOAT3 color = {
			RandomFloat(0.5f, 1.0f),
			RandomFloat(0.5f, 1.0f),
			RandomFloat(0.5f, 1.0f)
		};

		XMFLOAT3 dir = {
			RandomFloat(-1.f, 1.f),
			RandomFloat(-1.f, 0.f),
			RandomFloat(-1.f, 1.f)
		};
		XMStoreFloat3(&dir, XMVector3Normalize(XMLoadFloat3(&dir)));

		float falloffStart = RandomFloat(2.f, 5.f);
		float falloffEnd = RandomFloat(15.f, 30.f);
		float spotPower = RandomFloat(1.f, 1.f);

		LightObject light = {};
		light.Color = color;
		light.FalloffStart = falloffStart;
		light.Direction = dir;
		light.FalloffEnd = falloffEnd;
		light.Position = pos;
		light.SpotPower = spotPower;
		light.Type = type;
		light.Active = true;

		world.Lights().push_back(light);
	}
}

void BuildDemoScene(World& world, AssetManager& assets)
{
    ModelHandle sponza = assets.LoadModel(LOCAL_PATH + "Models/Model.obj");
    ModelHandle statue = assets.LoadModel(LOCAL_PATH + "Models/IBL_TEST.obj");
	//ModelHandle lag = assets.LoadModel(LOCAL_PATH + "Models/A_LOT_OF_POLYGONS.obj");

    world.Spawn(sponza);
    world.Spawn(statue);

	/*const int n = 1;
	const float space = 1.0f;

	for (int i = 0; i < n * n * n; i++)
	{
		Transform t;
		t.Position = { (i % n) * space, ((i / n) % n) * space, (i / (n * n)) * space };
		world.Spawn(lag, t);
	}*/

	TerrainDesc td;
	td.HeightMap = L"Height.png";
	td.Layers = { { L"Default.png", 1024.0f }, { L"Grass.png", 16.0f }, { L"Stone.png", 32.0f }, { L"Muddy.png", 32.0f } };
	world.SetTerrain(assets.LoadTerrain(td));

	AtmosphereDesc ad;
	ad.IrradianceFileName = L"DiffuseHDR.dds";
	ad.SpecularFileName = L"SpecularHDR.dds";
	ad.BrdfFileName = L"Brdf.dds";
	world.SetAtmosphere(assets.LoadAtmosphere(ad));

    CreateLights(world);
}