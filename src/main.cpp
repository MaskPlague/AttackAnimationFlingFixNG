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

bool isAttacking = false;
bool flingHappened = false;
bool eventSinkStarted = false;

void SlowPlayerVelocity()
{
    auto *player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->IsInMidair())
        return;

    RE::NiPoint3 velocity;
    player->GetLinearVelocity(velocity);

    logger::trace("Velocity: X{}, Y{}", velocity.x, velocity.y);
    float magnitude = sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y));
    logger::trace("Magnitude: {}", magnitude);

    if (magnitude > 500 && !flingHappened)
    {
        if (auto *controller = player->GetCharController(); controller)
        {
            flingHappened = true;
            logger::debug("In air and velocity: x{:.2f}, y{:.2f}, z{:.2f}", velocity.x, velocity.y, velocity.z);
            RE::NiPoint3 impulse = RE::NiPoint3();
            if (magnitude > 1300)
                impulse = RE::NiPoint3(velocity.x * 0.01f, velocity.y * 0.01f, velocity.z * 0.2f);
            else if (magnitude > 1000)
                impulse = RE::NiPoint3(velocity.x * 0.015f, velocity.y * 0.015f, velocity.z * 0.2f);
            else if (magnitude > 800)
                impulse = RE::NiPoint3(velocity.x * 0.025f, velocity.y * 0.025f, velocity.z * 0.2f);
            else
                impulse = RE::NiPoint3(velocity.x * 0.03f, velocity.y * 0.03f, velocity.z * 0.2f);

            controller->SetLinearVelocityImpl(impulse);
            logger::debug("Impulse set to: x{:.2f}, y{:.2f}, z{:.2f}", impulse.x, impulse.y, impulse.z);
            logger::info("Animation Fling Prevented");
        }
    }
}

void LoopSlowPlayerVeocity()
{
    logger::debug("Loop starting");
    std::thread([&]
                {while (isAttacking && !flingHappened) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(33));
                    SKSE::GetTaskInterface()->AddTask([&] {SlowPlayerVelocity();});
            } })
        .detach();
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
        logger::trace("Payload: {}", event->payload);
        logger::trace("Tag: {}\n", event->tag);

        if (!isAttacking && (event->tag == "PowerAttack_Start_end" || event->tag == "MCO_DodgeInitiate" || event->tag == "RollTrigger"))
        {
            isAttacking = true;
            flingHappened = false;
            LoopSlowPlayerVeocity();
            logger::debug("Attack Started");
        }
        else if (isAttacking && (event->tag == "attackStop" || event->tag == "MCO_DodgeOpen" || event->tag == "RollStop"))
        {
            isAttacking = false;
            logger::debug("Attack Finished");
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

void OnPostLoadGame()
{
    auto *player = RE::PlayerCharacter::GetSingleton();
    if (player)
    {
        logger::info("Creating Event Sink");
        try
        {
            auto *sink = new AttackAnimationGraphEventSink();
            player->AddAnimationGraphEventSink(sink);
            logger::info("Event Sink Created");
        }
        catch (...)
        {
            logger::info("Failed to Create Event Sink");
        }
    }
    else
        logger::info("Failed to Create Event Sink as Player Could not be Retrieved");
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
    spdlog::set_level(spdlog::level::trace);

    logger::info("Attack Animation Fling Fix NG Plugin Starting");

    auto *messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener("SKSE", MessageHandler);

    logger::info("Attack Animation Fling Fix NG Plugin Loaded");

    return true;
}
