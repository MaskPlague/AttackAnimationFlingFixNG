

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
    std::vector<RE::NiPoint3> positions;
};

inline std::unordered_map<RE::FormID, ActorState> g_actorStates;

ActorState &GetState(RE::Actor *actor)
{
    return g_actorStates[actor->GetFormID()];
}

ActorState *TryGetState(RE::Actor *actor)
{
    if (!actor)
        return nullptr;
    auto it = g_actorStates.find(actor->GetFormID());
    return it != g_actorStates.end() ? &it->second : nullptr;
}

void SlowActorVelocity(RE::Actor *actor)
{
    ActorState *stateCheck = TryGetState(actor);
    if (!stateCheck)
        return;
    auto &state = GetState(actor);
    if (!actor)
        return;
    if (state.flingHappened)
    {
        return;
    }
    if (!actor->IsInMidair())
        return;
    state.positions.push_back(actor->GetPosition());
    RE::NiPoint3 velocity;
    actor->GetLinearVelocity(velocity);
    logger::trace("Velocity: X{}, Y{}", velocity.x, velocity.y);
    float magnitude = sqrt((velocity.x * velocity.x) + (velocity.y * velocity.y));
    logger::debug("Magnitude: {}", magnitude);

    if (magnitude > 500)
    {
        for (int i = 0; i < 5; i++)
        {
            if (!actor)
                return;
        }
        RE::NiPoint3 sum;
        for (int i = 0; i < state.positions.size() - 1; ++i)
        {
            float dx = state.positions[i + 1].x - state.positions[i].x;
            float dy = state.positions[i + 1].y - state.positions[i].y;

            float length = std::sqrt(dx * dx + dy * dy);
            if (length != 0.0f && length < 200.0f)
            {
                dx /= length;
                dy /= length;
                sum.x += dx;
                sum.y += dy;
            }
        }
        float sumLength = std::sqrt(sum.x * sum.x + sum.y * sum.y);
        if (sumLength != 0.0f)
        {
            sum.x /= sumLength;
            sum.y /= sumLength;
        }
        if (auto *controller = actor->GetCharController(); controller)
        {
            state.flingHappened = true;
            logger::debug("In air and velocity: x{:.2f}, y{:.2f}, z{:.2f}", velocity.x, velocity.y, velocity.z);
            if (sumLength == 0.0f)
                sum = velocity / magnitude;
            float zI = velocity.z * 0.02f;
            if (zI > 5.0f)
                zI *= 5.0f;
            RE::NiPoint3 impulse = {sum.x * magnitude * 0.03f, sum.y * magnitude * 0.03f, zI};
            controller->SetLinearVelocityImpl(impulse);
            logger::debug("Impulse set to: x{:.2f}, y{:.2f}, z{:.2f}", impulse.x, impulse.y, impulse.z);
            logger::debug("count {}", state.positions.size());
            state.positions.clear();
            logger::info("Animation Fling Prevented for {}", actor->GetName());
        }
    }
}

bool IsGameWindowFocused()
{
    HWND foreground = ::GetForegroundWindow();
    DWORD foregroundPID = 0;
    ::GetWindowThreadProcessId(foreground, &foregroundPID);
    return foregroundPID == ::GetCurrentProcessId();
}

void LoopSlowActorVelocity(RE::Actor *actor)
{
    if (!actor)
        return;

    logger::debug("Loop starting");
    std::thread([formID = actor->GetFormID()]()
                {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(33));

        if (!IsGameWindowFocused()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        auto it = g_actorStates.find(formID);
        if (it == g_actorStates.end()) {
            logger::debug("Actor state no longer exists, ending LoopEdgeCheck");
            break;
        }

        auto actorPtr = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (!actorPtr)
            break;

        auto& state = it->second;
        if (!state.isAttacking) {
            break;
        }

        SKSE::GetTaskInterface()->AddTask([formID]() {
            if (auto actor = RE::TESForm::LookupByID<RE::Actor>(formID))
                SlowActorVelocity(actor);
        });
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
        ActorState *stateCheck = TryGetState(actor);
        if (!stateCheck)
            return RE::BSEventNotifyControl::kContinue;
        auto &state = GetState(actor);
        logger::trace("{} Payload: {}", holderName, event->payload);
        logger::trace("{} Tag: {}", holderName, event->tag);
        if (event->tag == "PowerAttack_Start_end" || event->tag == "MCO_DodgeInitiate" ||
            event->tag == "RollTrigger" || event->tag == "SidestepTrigger" ||
            event->tag == "TKDR_DodgeStart" || event->tag == "MCO_DisableSecondDodge")
        {
            state.isAttacking = true;
            state.flingHappened = false;
            if (!state.isLooping)
            {
                state.positions.push_back(actor->GetPosition());
                state.isLooping = true;
                LoopSlowActorVelocity(actor);
            }
            logger::debug("Attack Started for {}", holderName);
            if (event->tag == "PowerAttack_Start_end") // Any Attack
                state.animationType = 1;
            else if (event->tag == "MCO_DodgeInitiate") // DMCO
                state.animationType = 2;
            else if (event->tag == "RollTrigger" || event->tag == "SidestepTrigger") // TUDMR
                state.animationType = 3;
            else if (event->tag == "TKDR_DodgeStart") // TK Dodge RE
                state.animationType = 4;
            else if (event->tag == "MCO_DisableSecondDodge") // Old DMCO
                state.animationType = 5;
        }
        else if (state.isAttacking &&
                 ((state.animationType == 1 && event->tag == "attackStop") ||
                  (state.animationType == 2 && event->payload == "$DMCO_Reset") ||
                  (state.animationType == 3 && event->tag == "RollStop") || (state.animationType == 4 && event->tag == "TKDR_DodgeEnd") ||
                  (state.animationType == 5 && event->tag == "EnableBumper") ||
                  state.animationType == 0 || (state.animationType != 1 && event->tag == "InterruptCast") || event->tag == "IdleStop" || event->tag == "JumpUp" || event->tag == "MTstate"))
        {
            if (state.animationType == 0)
                logger::debug("Force ending LoopSlowActorVelocity");
            state.animationType = 0;
            state.isAttacking = false;
            state.isLooping = false;
            state.positions.clear();
            logger::debug("Attack Finished for {}", holderName);
        }
        else if (state.flingHappened && !actor->IsInMidair())
        {
            state.flingHappened = false;
        }

        return RE::BSEventNotifyControl::kContinue;
    }
    static AttackAnimationGraphEventSink *GetSingleton()
    {
        static AttackAnimationGraphEventSink singleton;
        return &singleton;
    }
};

void CleanupActors()
{
    for (auto it = g_actorStates.begin(); it != g_actorStates.end();)
    {
        auto actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
        if (!actor || actor->IsDead() || actor->IsDeleted() || !actor->IsInCombat() || actor->IsDisabled())
        {
            if (actor->IsPlayerRef())
            {
                ++it;
                continue;
            }
            actor->RemoveAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
            it = g_actorStates.erase(it);
        }
        else
            ++it;
    }
}

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
        if (combatState == RE::ACTOR_COMBAT_STATE::kCombat && !g_actorStates.contains(formID))
        {
            g_actorStates[formID];

            actor->AddAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
            logger::debug("Tracking new combat actor: {}", actor->GetName());
        }
        else if (combatState == RE::ACTOR_COMBAT_STATE::kNone && g_actorStates.contains(formID))
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
        auto player = RE::PlayerCharacter::GetSingleton();
        g_actorStates[player->GetFormID()];
        player->AddAnimationGraphEventSink(AttackAnimationGraphEventSink::GetSingleton());
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
