#include "GameManager.h"

using namespace Zamin;

void GameManager::OnEnable()
{
    // Tanga yig'ilganini hodisalar shinasi orqali eshitamiz: tanga GameManager'ni bilishi shart emas.
    // Faqat skript yoqilgan paytda tinglaymiz (OnDisable'da obuna bekor qilinadi).
    m_CoinSubscription = Subscribe("CoinCollected", [this](const ScriptMessage&) { AddCoin(); });
}

void GameManager::OnDisable()
{
    Unsubscribe(m_CoinSubscription);
    m_CoinSubscription = 0;
}

void GameManager::OnStart()
{
    // OnStart birinchi Update'dan oldin, sahnadagi hamma skriptlar OnCreate'dan o'tgach chaqiriladi:
    // boshqa skriptlar yaratgan tangalar ham hisobga kiradi. Tangalar "Coin" tegi bilan belgilangan.
    m_TotalCoins = (int)FindEntitiesWithTag("Coin").size();
    RefreshText();
}

void GameManager::OnMessage(const ScriptMessage& message)
{
    if (message.Name == "PlayerFell")
        OnPlayerFell();
}

void GameManager::OnLateUpdate(Timestep)
{
    // HUD: keep the text pinned to the camera (only when it is not a screen-space UI element).
    if (HasComponent<UITransformComponent>())
        return;
    if (Entity camera = GetScene().GetPrimaryCameraEntity())
    {
        const auto& cam = camera.GetComponent<TransformComponent>().Translation;
        auto& t = Transform();
        t.Translation = { cam.x + ScreenOffset.x, cam.y + ScreenOffset.y, 1.0f };
    }
}

void GameManager::AddCoin()
{
    m_Coins++;
    // Kept between sessions (SaveData file in ~/.local/share/Zamin Studio/Zamin Platformer: the company and
    // product name of Project settings > Player).
    SaveData::SetInt("TotalCoins", SaveData::GetInt("TotalCoins") + 1);
    RefreshText();
    if (m_Coins == m_TotalCoins)
        ZM_INFO("Barakalla! Hamma tangalar yig'ildi!");
}

void GameManager::OnPlayerFell()
{
    m_Falls++;
    RefreshText();
}

void GameManager::OnLanguageChanged(const std::string&)
{
    RefreshText();
}

void GameManager::RefreshText()
{
    // The texts come from the language files (Assets/Localization/<til>.json): "{0}: {1} / {2}" ...
    if (auto* text = TryGetComponent<TextComponent>())
    {
        const std::string title = Localization::Has(TitleKey) ? Localize(TitleKey) : Title;
        text->Text = Localize("hud.coins", title, m_Coins, m_TotalCoins);
        if (int total = SaveData::GetInt("TotalCoins"); total > 0)
            text->Text += "   " + Localize("hud.coins_total", total);
        if (m_Falls > 0)
            text->Text += "\n" + Localize("hud.falls", m_Falls);
        if (m_TotalCoins > 0 && m_Coins == m_TotalCoins)
            text->Text += "\n" + Localize("hud.win");
    }
}

ZM_REGISTER_SCRIPT(GameManager,
    ZM_FIELD_TOOLTIP(GameManager, Title, "Ekrandagi hisob matni oldidagi so'z (til fayllarida TitleKey bo'lmasa)"),
    ZM_FIELD_TOOLTIP(GameManager, TitleKey, "Sarlavhaning lokalizatsiya kaliti (Assets/Localization/<til>.json)"),
    ZM_FIELD(GameManager, ScreenOffset).Tooltip("Matnning kameraga nisbatan joyi (UI elementi bo'lmasa)"))
