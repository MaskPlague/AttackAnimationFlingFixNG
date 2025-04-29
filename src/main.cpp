

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

struct ActorState
{
    bool isAttacking = false;
    bool flingHappened = false;
    bool isLooping = false;
    int animationType = 0;
};

inline std::unordered_map<RE::FormID, ActorState> g_actorStates;

ActorState &GetState(RE::Actor *actor)
{
    return g_actorStates[actor->GetFormID()];
}

void CleanupActors()
{
    for (auto it = g_actorStates.begin(); it != g_actorStates.end();)
    {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
        if (!actor || actor->IsDead() || actor->IsDeleted() || !actor->IsInCombat() || actor->IsDisabled())
            it = g_actorStates.erase(it);
        else
            ++it;
    }
}

void SlowActorVelocity(RE::Actor *actor)
{
    auto &state = GetState(actor);
    if (!actor || !actor->IsInMidair() || state.flingHappened)
        return;

    RE::NiPoint3 velocity;
    actor->GetLinearVelocity(velocity);

    logger::trace("Velocity: X{}, Y{}", velocity.x, velocity.y);
    float magnitude = sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y));
    logger::trace("Magnitude: {}", magnitude);

    if (magnitude > 500)
    {
        if (auto *controller = actor->GetCharController(); controller)
        {
            state.flingHappened = true;
            logger::debug("In air and velocity: x{:.2f}, y{:.2f}, z{:.2f}", velocity.x, velocity.y, velocity.z);
            RE::NiPoint3 impulse;
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
            logger::info("Animation Fling Prevented for {}", actor->GetName());
        }
    }
}
bool IsGameWindowFocused()
{
    static const HWND gameWindow = ::FindWindow(nullptr, L"Skyrim Special Edition");
    return ::GetForegroundWindow() == gameWindow;
}

void LoopSlowActorVeocity(RE::Actor *actor)
{
    if (!actor)
        return;

    logger::debug("Loop starting");
    std::thread([actor]()
                {
            RE::FormID formID = actor->GetFormID();
            
            while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            if (!IsGameWindowFocused())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            auto it = g_actorStates.find(formID);
            if (it == g_actorStates.end())
            {
                logger::debug("Actor state no longer exists, ending LoopEdgeCheck");
                break;
            }

            auto &state = it->second;
            if (!(state.isAttacking || !state.flingHappened))
            {
                break;
            }
            SKSE::GetTaskInterface() -> AddTask([actor](){SlowActorVelocity(actor);});
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
        // Cast away constness
        auto refr = const_cast<RE::TESObjectREFR *>(event->holder);
        auto actor = refr->As<RE::Actor>();
        if (!actor)
        {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto holderName = event->holder->GetName();
        auto &state = GetState(actor);
        logger::trace("{} Payload: {}", holderName, event->payload);
        logger::trace("{} Tag: {}", holderName, event->tag);
        if (event->tag == "PowerAttack_Start_end" || event->tag == "MCO_DodgeInitiate" ||
            event->tag == "RollTrigger" || event->tag == "TKDR_DodgeStart")
        {
            state.isAttacking = true;
            state.flingHappened = false;
            if (!state.isLooping)
                LoopSlowActorVeocity(actor);
            state.isLooping = true;
            logger::debug("Attack Started for {}", holderName);
            if (event->tag == "PowerAttack_Start_end")
                state.animationType = 1;
            else if (event->tag == "MCO_DodgeInitiate")
                state.animationType = 2;
            else if (event->tag == "RollTrigger")
                state.animationType = 3;
            else if (event->tag == "TKDR_DodgeStart")
                state.animationType = 4;
            else if (event->tag == "MCO_DisableSecondDodge")
                state.animationType = 5;
        }
        else if (state.isAttacking && ((state.animationType == 1 && event->tag == "attackStop") || (state.animationType == 2 && event->payload == "$DMCO_Reset") ||
                                       (state.animationType == 3 && event->tag == "RollStop") || (state.animationType == 4 && event->tag == "TKDR_DodgeEnd") ||
                                       (state.animationType == 5 && event->tag == "EnableBumper")))
        {
            state.animationType = 0;
            state.isAttacking = false;
            state.isLooping = false;
            logger::debug("Attack Finished for {}", holderName);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
    static AttackAnimationGraphEventSink *GetSingleton()
    {
        static AttackAnimationGraphEventSink singleton;
        return &singleton;
    }
};

class CombatEventSink : public RE::BSTEventSink<RE::TESCombatEvent>
{
public:
    virtual RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCombatEvent *a_event,
        RE::BSTEventSource<RE::TESCombatEvent> *) override
    {
        if (!a_event)
        {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto actor = a_event->actor->As<RE::Actor>();
        if (!actor || actor->IsPlayerRef() || !actor->GetActorBase() || !actor->GetActorBase()->GetRace())
            return RE::BSEventNotifyControl::kContinue;
        auto race = actor->GetActorBase()->GetRace();
        if (!race->HasKeywordString("ActorTypeNPC"))
        {
            return RE::BSEventNotifyControl::kContinue;
        }
        auto formID = actor->GetFormID();
        auto combatState = a_event->newState;
        if (combatState == RE::ACTOR_COMBAT_STATE::kCombat)
        {
            g_actorStates[formID];
            actor->AddAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
            logger::debug("Tracking new combat actor: {}", actor->GetName());
        }
        else if (combatState == RE::ACTOR_COMBAT_STATE::kNone)
        {
            g_actorStates.erase(formID);
            actor->RemoveAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
            logger::debug("Stopped tracking actor: {}", actor->GetName());
        }
        CleanupActors();
        return RE::BSEventNotifyControl::kContinue;
    }
    static CombatEventSink *GetSingleton()
    {
        static CombatEventSink singleton;
        return &singleton;
    }
};
void OnPostLoadGame()
{
    logger::info("Creating Event Sink(s)");
    try
    {
        g_actorStates.clear();
        RE::PlayerCharacter::GetSingleton()->AddAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(CombatEventSink::GetSingleton());
        logger::info("Event Sink(s) Created");
    }
    catch (...)
    {
        logger::info("Failed to Create Event Sink(s)");
    }
}

void MessageHandler(SKSE::MessagingInterface::Message *msg)
{
    if (msg->type != SKSE::MessagingInterface::kPostLoadGame)
        return;
    if (!bool(msg->data))
        return;
    OnPostLoadGame();
}

extern "C" DLLEXPORT bool SKSEPlugin_Load(const SKSE::LoadInterface *skse)
{
    SKSE::Init(skse);

    SetupLog();
    spdlog::set_level(spdlog::level::info);

    logger::info("Attack Animation Fling Fix NG Plugin Starting");

    auto *messaging = SKSE::GetMessagingInterface();
    messaging->RegisterListener("SKSE", MessageHandler);

    logger::info("Attack Animation Fling Fix NG Plugin Loaded");

    return true;
}
