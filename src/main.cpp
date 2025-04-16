#include <spdlog/sinks/basic_file_sink.h>

namespace logger = SKSE::log;

void SetupLog()
{
    auto logsFolder = SKSE::log::log_directory();
    if (!logsFolder)
        SKSE::stl::report_and_fail("SKSE log_directory not provided, logs disabled.");
    auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
    auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
    auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
    auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
    spdlog::set_default_logger(std::move(loggerPtr));
    spdlog::set_level(spdlog::level::trace);
    spdlog::flush_on(spdlog::level::trace);
}

void SlowPlayerVelocity()
{
    auto *player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->IsInMidair() || !player->IsAttacking())
        return;

    RE::NiPoint3 velocity;
    player->GetLinearVelocity(velocity);

    logger::debug("Velocity at HitFrame: X{}, Y{}", velocity.x, velocity.y);
    float magnitude = sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y));
    logger::debug("           Magnitude: {}", magnitude);

    if (magnitude > 600)
    {
        if (auto *controller = player->GetCharController(); controller)
        {
            logger::debug("In air and velocity: x{:.2f}, y{:.2f}, z{:.2f}", velocity.x, velocity.y, velocity.z);
            RE::NiPoint3 impulse = RE::NiPoint3();
            if (magnitude > 1300)
                impulse = RE::NiPoint3(velocity.x * 0.01f, velocity.y * 0.01f, velocity.z * 0.5f);
            else if (magnitude > 1000)
                impulse = RE::NiPoint3(velocity.x * 0.015f, velocity.y * 0.015f, velocity.z * 0.5f);
            else if (magnitude > 800)
                impulse = RE::NiPoint3(velocity.x * 0.025f, velocity.y * 0.025f, velocity.z * 0.5f);
            else
                impulse = RE::NiPoint3(velocity.x * 0.03f, velocity.y * 0.03f, velocity.z * 0.5f);

            controller->SetLinearVelocityImpl(impulse);
            logger::debug("Impulse set to: x{:.2f}, y{:.2f}, z{:.2f}", impulse.x, impulse.y, impulse.z);
            logger::info("Animation Fling Prevented");
        }
    }
}

class AttackAnimationGraphEventSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent *event, RE::BSTEventSource<RE::BSAnimationGraphEvent> *)
    {
        if (!event)
        {
            return RE::BSEventNotifyControl::kStop;
        }
        logger::trace("Tag: {}\n", event->tag);

        if (event->tag == "HitFrame")
        {
            logger::debug("Hiframe Event detected");
            SlowPlayerVelocity();
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

void OnPostLoadGame()
{
    logger::info("Creating HitFrame Event Sink");
    auto *player = RE::PlayerCharacter::GetSingleton();
    if (player)
    {
        try
        {
            auto *sink = new AttackAnimationGraphEventSink();
            player->AddAnimationGraphEventSink(sink);
        }
        catch (const std::runtime_error &error)
        {
            logger::info("Failed to Create HitFrame Event Sink with Error: {}", error.what());
        }
    }
    logger::info("HitFrame Event Sink Created");
}

void MessageHandler(SKSE::MessagingInterface::Message *msg)
{
    if (msg->type == SKSE::MessagingInterface::kPostLoadGame)
    {
        OnPostLoadGame();
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse)
{
    SKSE::Init(skse);

    SetupLog();

    logger::info("Attack Animation Fling Fix NG Plugin Starting");

    auto *messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener("SKSE", MessageHandler);

    spdlog::set_level(spdlog::level::info);
    logger::info("Attack Animation Fling Fix NG Plugin Loaded");

    return true;
}
