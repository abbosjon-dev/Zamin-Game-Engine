#include "Zamin/Editor/PrefabFieldLabels.h"

namespace Zamin::PrefabFieldLabels {

    const std::vector<PrefabFieldLabel>& GetAll()
    {
        static const std::vector<PrefabFieldLabel> s_Labels = {
            { "Transform", "Pozitsiya", "Transform/Translation" },
            { "Transform", "Aylanish", "Transform/Rotation" },
            { "Transform", "Masshtab", "Transform/Scale" },

            { "SpriteRenderer", "Rang", "SpriteRenderer/Color" },
            { "SpriteRenderer", "Tekstura", "SpriteRenderer/Texture" },
            { "SpriteRenderer", "Sprite (kadr)", "SpriteRenderer/Sprite" },
            { "SpriteRenderer", "Chizish rejimi", "SpriteRenderer/DrawMode" },
            { "SpriteRenderer", "O'lcham (birlik)", "SpriteRenderer/Size" },
            { "SpriteRenderer", "Chegaralar (px)", "SpriteRenderer/SliceBorder" },
            { "SpriteRenderer", "Piksel / birlik", "SpriteRenderer/PixelsPerUnit" },
            { "SpriteRenderer", "Takrorlash", "SpriteRenderer/Tiling" },
            { "SpriteRenderer", "Pixel-art (aniq piksellar)", "SpriteRenderer/PixelArt" },
            { "SpriteRenderer", "Gorizontal aks (Flip X)", "SpriteRenderer/FlipX" },
            { "SpriteRenderer", "Vertikal aks (Flip Y)", "SpriteRenderer/FlipY" },

            { "CircleRenderer", "Rang", "CircleRenderer/Color" },
            { "CircleRenderer", "Qalinlik", "CircleRenderer/Thickness" },
            { "CircleRenderer", "Yumshoqlik", "CircleRenderer/Fade" },

            { "Text", "Matn", "Text/Text" },
            { "Text", "Rang", "Text/Color" },
            { "Text", "Harf oralig'i", "Text/Kerning" },
            { "Text", "Qator oralig'i", "Text/LineSpacing" },
            { "Text", "Gorizontal", "Text/AlignH" },
            { "Text", "Vertikal", "Text/AlignV" },
            { "Text", "O'rash", "Text/WordWrap" },
            { "Text", "Sig'maganda", "Text/Overflow" },
            { "Text", "Shrift", "Text/Font" },

            { "Camera", "Asosiy (Primary)", "Camera/Primary" },
            { "Camera", "Proyeksiya", "Camera/ProjectionType" },
            { "Camera", "Ko'rish burchagi", "Camera/PerspectiveFOV" },
            { "Camera", "O'lcham", "Camera/OrthographicSize" },
            { "Camera", "Qat'iy nisbat", "Camera/FixedAspectRatio" },
            { "Camera", "Fon rangi", "Camera/ClearColor" },

            { "MeshRenderer", "Mesh", "MeshRenderer/Mesh" },
            { "MeshRenderer", "Model fayli", "MeshRenderer/Model" },
            { "MeshRenderer", "Soya tashlaydi", "MeshRenderer/CastShadows" },

            { "Rigidbody2D", "Turi", "Rigidbody2D/BodyType" },
            { "Rigidbody2D", "Aylanmasin", "Rigidbody2D/FixedRotation" },
            { "Rigidbody2D", "Gravitatsiya", "Rigidbody2D/GravityScale" },
            { "Rigidbody2D", "Chiziqli so'nish", "Rigidbody2D/LinearDamping" },
            { "Rigidbody2D", "Burchak so'nishi", "Rigidbody2D/AngularDamping" },
            { "Rigidbody2D", "Tez obyekt (bullet)", "Rigidbody2D/Bullet" },

            { "Rigidbody3D", "Turi", "Rigidbody3D/BodyType" },
            { "Rigidbody3D", "Massa", "Rigidbody3D/Mass" },
            { "Rigidbody3D", "Gravitatsiya", "Rigidbody3D/GravityFactor" },
            { "Rigidbody3D", "Chiziqli so'nish", "Rigidbody3D/LinearDamping" },
            { "Rigidbody3D", "Burchak so'nishi", "Rigidbody3D/AngularDamping" },
            { "Rigidbody3D", "Aylanmasin", "Rigidbody3D/LockRotation" },

            { "BoxCollider2D", "Siljish", "BoxCollider2D/Offset" },
            { "BoxCollider2D", "Yarim o'lcham", "BoxCollider2D/Size" },
            { "BoxCollider2D", "Bir tomonlama (platforma)", "BoxCollider2D/OneWay" },
            { "CircleCollider2D", "Siljish", "CircleCollider2D/Offset" },
            { "CircleCollider2D", "Radius", "CircleCollider2D/Radius" },
            { "BoxCollider3D", "Yarim o'lcham", "BoxCollider3D/HalfExtents" },
            { "SphereCollider3D", "Radius", "SphereCollider3D/Radius" },

            { "UITransform", "Langar min", "UITransform/AnchorMin" },
            { "UITransform", "Langar max", "UITransform/AnchorMax" },
            { "UITransform", "Pivot", "UITransform/Pivot" },
            { "UITransform", "Shrift o'lchami", "UITransform/FontSize" },
            { "UITransform", "Tartib", "UITransform/Order" },
            { "UITransform", "Masshtab", "UITransform/Scale" },
            { "UITransform", "Siljish", "UITransform/VisualOffset" },
            { "UITransform", "Shaffoflik (alpha)", "UITransform/Alpha" },

            { "UIImage", "Rang", "UIImage/Color" },
            { "UIImage", "Tekstura", "UIImage/Texture" },
            { "UIImage", "Sprite (kadr)", "UIImage/Sprite" },
            { "UIImage", "Turi", "UIImage/Type" },
            { "UIImage", "Nisbatni saqlash", "UIImage/PreserveAspect" },
            { "UIImage", "Chegaralar (px)", "UIImage/SliceBorder" },
            { "UIImage", "Chegara masshtabi", "UIImage/BorderScale" },
            { "UIImage", "Pixel-art (aniq piksellar)", "UIImage/PixelArt" },
            { "UIImage", "Sichqonchani to'sadi", "UIImage/RaycastTarget" },

            { "DirectionalLight", "Rang", "DirectionalLight/Color" },
            { "DirectionalLight", "Kuch", "DirectionalLight/Intensity" },
            { "DirectionalLight", "Soyalar", "DirectionalLight/CastShadows" },
            { "PointLight", "Rang", "PointLight/Color" },
            { "PointLight", "Kuch", "PointLight/Intensity" },
            { "PointLight", "Radius", "PointLight/Radius" },
            { "SpotLight", "Rang", "SpotLight/Color" },
            { "SpotLight", "Kuch", "SpotLight/Intensity" },
            { "SpotLight", "Masofa", "SpotLight/Range" },
            { "SpotLight", "Tashqi burchak", "SpotLight/OuterAngle" },
            { "SpotLight", "Ichki burchak", "SpotLight/InnerAngle" },

            { "AudioSource", "Ovoz fayli", "AudioSource/Path" },
            { "AudioSource", "Balandlik", "AudioSource/Volume" },
            { "AudioSource", "Ton (pitch)", "AudioSource/Pitch" },
            { "AudioSource", "Takrorlash", "AudioSource/Loop" },
            { "AudioSource", "Boshida ijro", "AudioSource/PlayOnStart" },
            { "AudioSource", "3D ovoz (spatial)", "AudioSource/Spatial" },
            { "AudioSource", "Min masofa", "AudioSource/MinDistance" },
            { "AudioSource", "Max masofa", "AudioSource/MaxDistance" },
        };
        return s_Labels;
    }

    std::string_view Find(std::string_view component, std::string_view label)
    {
        for (const PrefabFieldLabel& entry : GetAll())
            if (entry.Component == component && entry.Label == label)
                return entry.Path;
        return {};
    }

}
