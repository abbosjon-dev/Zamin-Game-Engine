# Map Workshop

3D xarita tayyorlash uchun loyiha. `build-review/bin/ZaminEditor Projects/MapWorkshop` bilan oching. Terrain vositalari: **Oyna → Map tayyorlash (Terrain)**. [Qo'llanma](../../docs/Terrain.md).

Map sahnasida relyef, tuproq yo'li, daraxtlar, quyosh, kamera va kelajakdagi qahramon uchun `Player Spawn` belgisi bor. Bu sahna o'yin mantiqisiz map muallifligi namunasi. Play/Simulate statik terrain va daraxt collision'ini ishlatadi.

To'rtta terrain qatlami `Assets/Textures/Terrain` ichidagi seamless albedo, tangent normal va ORM xaritalarini ishlatadi. Bular tashqi assetga bog'liq bo'lmagan analitik test materiallari; `python3 Tools/samples/gen_terrain_materials.py` ularni barqaror UUID bilan qayta yaratadi. Normal/ORM importida sRGB o'chirilgan. Layer parametrlarini **Yer qatlamlari** oynasida o'zgartiring; manba fayli reload qilinganda terrain geometriyasi qayta qurilmaydi.
