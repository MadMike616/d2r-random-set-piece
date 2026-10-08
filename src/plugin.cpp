#include <D2RLPlugin/api.h>

#include "default_config.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr char PluginId[] = "random-set-piece";
constexpr char ConvertTarget[] = "HoradricCubePanelMessage";
constexpr char ConvertCommand[] = "Convert";
constexpr std::size_t MaximumConfigBytes = 16U * 1024U;
constexpr std::size_t MaximumCubeItems = 12;

struct SetMember {
    std::uint32_t rowId{};
    std::uint32_t itemCode{};
    std::string setName;
    std::string itemName;
};

struct Configuration {
    std::uint32_t inputX{};
    std::uint32_t inputY{};
    std::uint32_t failureChancePercent{};
    bool useInputX{true};
    bool useInputY{true};
    std::string tableDirectory;
};

struct PendingExchange {
    D2RL::PlayerHandle player{D2RL::InvalidPlayerHandle};
    std::array<D2RL::Items::TransactionInput, 3> inputs{};
    std::uint32_t inputCount{};
    std::uint32_t sourceRowId{};
    std::uint32_t itemLevel{};
    std::uint32_t stateFlags{};
    std::vector<SetMember> possibleOutputs;
};

struct PendingRequest {
    D2RL::PlayerHandle player{D2RL::InvalidPlayerHandle};
    std::uint32_t sourceRowId{};
};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = PluginId,
    .name = "Random Set Piece",
    .version = "0.4.3",
    .author = "Community",
    .description = "Transforms a set item into another random item from the same set in the Horadric Cube.",
    .flags = D2RL::PluginFlags::Client,
};

const D2RL::PluginContext* Context{};
const D2RL::InventoryServiceV1* InventoryService{};
const D2RL::ItemServiceV1* ItemService{};
const D2RL::ThreadServiceV1* ThreadService{};
const D2RL::SharedEventServiceV1* EventService{};
D2RL::SharedEvents::ListenerHandle MessageHandle{D2RL::SharedEvents::InvalidHandle};
Configuration Config;
std::unordered_map<std::uint32_t, SetMember> MembersByRow;
std::unordered_map<std::string, std::vector<SetMember>> MembersBySet;
std::atomic<bool> Active{};
std::atomic<bool> ExchangePending{};
std::atomic<bool> ConvertMessageLogged{};
std::atomic<bool> FirstRecipeMismatchLogged{};
std::mutex PendingMutex;
PendingRequest Pending;
std::mt19937 RandomEngine;

auto Trim(std::string_view text) -> std::string_view {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

auto StripComment(std::string_view text) -> std::string_view {
    bool quoted{};
    bool escaped{};
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quoted && ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            quoted = !quoted;
        } else if (!quoted && ch == '#') {
            return text.substr(0, i);
        }
    }
    return text;
}

auto ParseTomlString(std::string_view value, std::string& output) -> bool {
    value = Trim(StripComment(value));
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    output.clear();
    for (std::size_t i = 1; i + 1 < value.size(); ++i) {
        const char ch = value[i];
        if (ch == '\\') {
            if (i + 1 >= value.size() - 1) return false;
            ++i;
            const char escaped = value[i];
            switch (escaped) {
            case '\\': output.push_back('\\'); break;
            case '"': output.push_back('"'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            default: return false;
            }
        } else {
            output.push_back(ch);
        }
    }
    return true;
}

auto FindTomlString(std::string_view config, std::string_view key, std::string& output) -> bool {
    std::size_t start{};
    while (start < config.size()) {
        const std::size_t end = config.find('\n', start);
        const std::string_view line = Trim(StripComment(config.substr(start,
            end == std::string_view::npos ? config.size() - start : end - start)));
        const std::size_t equals = line.find('=');
        if (equals != std::string_view::npos && Trim(line.substr(0, equals)) == key) {
            return ParseTomlString(line.substr(equals + 1), output);
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return false;
}

auto FindTomlUnsigned(std::string_view config, std::string_view key,
        std::uint32_t& output, bool& found) -> bool {
    found = false;
    std::size_t start{};
    while (start < config.size()) {
        const std::size_t end = config.find('\n', start);
        const std::string_view line = Trim(StripComment(config.substr(start,
            end == std::string_view::npos ? config.size() - start : end - start)));
        const std::size_t equals = line.find('=');
        if (equals != std::string_view::npos && Trim(line.substr(0, equals)) == key) {
            found = true;
            const std::string_view value = Trim(line.substr(equals + 1));
            if (value.empty()) return false;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output);
            return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

auto ParseItemCode(std::string_view value, std::uint32_t& code) -> bool {
    value = Trim(value);
    if (value.size() != 3 && value.size() != 4) return false;
    std::array<char, 4> chars{' ', ' ', ' ', ' '};
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(value[i]);
        if (std::isalnum(ch) == 0 && ch != '_' && ch != '-') return false;
        chars[i] = static_cast<char>(ch);
    }
    code = D2RL::Items::MakeItemCode(chars[0], chars[1], chars[2], chars[3]);
    return true;
}

auto Utf8Path(std::string_view text) -> std::filesystem::path {
    std::u8string value;
    value.reserve(text.size());
    for (const unsigned char ch : text) {
        value.push_back(static_cast<char8_t>(ch));
    }
    return std::filesystem::path(value);
}

auto ReadConfiguration() -> bool {
    if (Context == nullptr || !Context->EnsureConfig(DefaultConfig)) return false;
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t required{};
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)
            || required > buffer.size()) {
        Context->LogError("RandomSetPiece: could not read the TOML config or it exceeds 16 KiB.");
        return false;
    }
    const std::string_view text(buffer.data());
    std::string inputX;
    std::string inputY;
    if (!FindTomlString(text, "input_x", inputX)
            || !FindTomlString(text, "input_y", inputY)) {
        Context->LogError("RandomSetPiece: recipe.input_x and recipe.input_y must be quoted strings.");
        return false;
    }
    const bool inputXEmpty = Trim(inputX).empty();
    const bool inputYEmpty = Trim(inputY).empty();
    if ((!inputXEmpty && !ParseItemCode(inputX, Config.inputX))
            || (!inputYEmpty && !ParseItemCode(inputY, Config.inputY))) {
        Context->LogError("RandomSetPiece: each recipe.input_x/input_y must be empty or a quoted 3- or 4-character item code.");
        return false;
    }
    Config.useInputX = !inputXEmpty;
    Config.useInputY = !inputYEmpty;
    bool hasFailureChance{};
    if (!FindTomlUnsigned(text, "failure_chance_percent",
                Config.failureChancePercent, hasFailureChance)
            || Config.failureChancePercent > 100) {
        Context->LogError("RandomSetPiece: recipe.failure_chance_percent must be an integer from 0 to 100.");
        return false;
    }
    if (!hasFailureChance) {
        // Existing configs created before this setting was added use the
        // default (zero) unless the user adds the key explicitly.
        Config.failureChancePercent = 0;
    }
    if (!FindTomlString(text, "table_directory", Config.tableDirectory)) {
        Context->LogError("RandomSetPiece: tables.table_directory must be a quoted string.");
        return false;
    }
    return true;
}

auto SplitTabs(std::string_view line) -> std::vector<std::string_view> {
    std::vector<std::string_view> fields;
    std::size_t start{};
    while (true) {
        const std::size_t end = line.find('\t', start);
        fields.push_back(line.substr(start,
            end == std::string_view::npos ? line.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return fields;
}

auto FindColumn(const std::vector<std::string_view>& header, std::string_view name) -> std::size_t {
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (Trim(header[i]) == name) return i;
    }
    return std::string_view::npos;
}

auto ParseUnsigned(std::string_view text, std::uint32_t& value) -> bool {
    text = Trim(text);
    if (text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

auto StripUtf8Bom(std::string& text) -> void {
    if (text.size() >= 3
            && static_cast<unsigned char>(text[0]) == 0xEF
            && static_cast<unsigned char>(text[1]) == 0xBB
            && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
}

auto ReadTextFile(const std::filesystem::path& path, std::string& text) -> bool {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    StripUtf8Bom(text);
    return !file.bad();
}

auto GetTableDirectories() -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> directories;
    if (!Config.tableDirectory.empty()) {
        const std::filesystem::path configured = Utf8Path(Config.tableDirectory);
        if (configured.is_absolute()) {
            directories.push_back(configured);
        } else if (Context != nullptr && Context->modDirectory != nullptr) {
            directories.push_back(std::filesystem::path(Context->modDirectory) / configured);
        } else {
            directories.push_back(std::filesystem::current_path() / configured);
        }
        return directories;
    }

    if (Context != nullptr && Context->modDirectory != nullptr) {
        const std::filesystem::path modRoot(Context->modDirectory);
        directories.push_back(modRoot / L"data" / L"global" / L"excel");
        if (Context->activeMod != nullptr && Context->activeMod[0] != '\0') {
            std::wstring modName;
            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                Context->activeMod, -1, nullptr, 0);
            if (count > 1) {
                modName.resize(static_cast<std::size_t>(count));
                (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    Context->activeMod, -1, modName.data(), count);
                modName.resize(static_cast<std::size_t>(count - 1));
                directories.push_back(modRoot / (modName + L".mpq") / L"data" / L"global" / L"excel");
            }
        }
    }
    if (Context != nullptr && Context->scopeRootDirectory != nullptr) {
        directories.push_back(std::filesystem::path(Context->scopeRootDirectory)
            / L"data" / L"global" / L"excel");
    }
    if (Context != nullptr && Context->modSupportDirectory != nullptr) {
        directories.push_back(std::filesystem::path(Context->modSupportDirectory)
            / L"data" / L"global" / L"excel");
    }
    return directories;
}

auto LoadSetMembers() -> bool {
    for (const auto& directory : GetTableDirectories()) {
        std::string setText;
        std::string setItemText;
        if (!ReadTextFile(directory / L"sets.txt", setText)
                || !ReadTextFile(directory / L"setitems.txt", setItemText)) continue;

        std::unordered_set<std::string> setNames;
        std::size_t lineStart{};
        const std::size_t headerEnd = setText.find('\n');
        if (headerEnd == std::string::npos) continue;
        const auto setHeader = SplitTabs(std::string_view(setText).substr(0, headerEnd));
        const std::size_t setNameColumn = FindColumn(setHeader, "name");
        if (setNameColumn == std::string_view::npos) continue;
        lineStart = headerEnd + 1;
        while (lineStart < setText.size()) {
            const std::size_t lineEnd = setText.find('\n', lineStart);
            std::string_view line(setText.data() + lineStart,
                (lineEnd == std::string::npos ? setText.size() : lineEnd) - lineStart);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (!line.empty()) {
                const auto fields = SplitTabs(line);
                if (setNameColumn < fields.size()) {
                    const auto name = Trim(fields[setNameColumn]);
                    if (!name.empty()) setNames.emplace(name);
                }
            }
            if (lineEnd == std::string::npos) break;
            lineStart = lineEnd + 1;
        }

        const std::size_t itemHeaderEnd = setItemText.find('\n');
        if (itemHeaderEnd == std::string::npos) continue;
        const auto itemHeader = SplitTabs(std::string_view(setItemText).substr(0, itemHeaderEnd));
        // In current D2R tables, "index" is the row's textual name and *ID is
        // the numeric SetItems row ID used by item qualityRecordId. Older
        // table layouts may expose a numeric index instead.
        const std::size_t explicitIdColumn = FindColumn(itemHeader, "*ID");
        const std::size_t rowColumn = explicitIdColumn != std::string_view::npos
            ? explicitIdColumn : FindColumn(itemHeader, "index");
        const std::size_t setColumn = FindColumn(itemHeader, "set");
        const std::size_t codeColumn = FindColumn(itemHeader, "item");
        const std::size_t itemNameColumn = FindColumn(itemHeader, "*ItemName");
        const std::size_t disabledColumn = FindColumn(itemHeader, "disabled");
        if (rowColumn == std::string_view::npos || setColumn == std::string_view::npos
                || codeColumn == std::string_view::npos) continue;

        MembersByRow.clear();
        MembersBySet.clear();
        lineStart = itemHeaderEnd + 1;
        while (lineStart < setItemText.size()) {
            const std::size_t lineEnd = setItemText.find('\n', lineStart);
            std::string_view line(setItemText.data() + lineStart,
                (lineEnd == std::string::npos ? setItemText.size() : lineEnd) - lineStart);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (!line.empty()) {
                const auto fields = SplitTabs(line);
                const bool hasRequiredColumns = rowColumn < fields.size()
                    && setColumn < fields.size() && codeColumn < fields.size();
                const bool hasDisabledColumn = disabledColumn == std::string_view::npos
                    || disabledColumn < fields.size();
                if (hasRequiredColumns && hasDisabledColumn) {
                    std::uint32_t rowId{};
                    std::uint32_t itemCode{};
                    const auto setName = Trim(fields[setColumn]);
                    const auto disabled = disabledColumn == std::string_view::npos
                        ? std::string_view{} : Trim(fields[disabledColumn]);
                    if (ParseUnsigned(fields[rowColumn], rowId)
                            && ParseItemCode(fields[codeColumn], itemCode)
                            && !setName.empty() && setNames.contains(std::string(setName))
                            && disabled != "1") {
                        SetMember member;
                        member.rowId = rowId;
                        member.itemCode = itemCode;
                        member.setName.assign(setName);
                        if (itemNameColumn < fields.size()) {
                            member.itemName.assign(Trim(fields[itemNameColumn]));
                        }
                        MembersByRow.insert_or_assign(rowId, member);
                        MembersBySet[member.setName].push_back(std::move(member));
                    }
                }
            }
            if (lineEnd == std::string::npos) break;
            lineStart = lineEnd + 1;
        }
        if (!MembersByRow.empty()) return true;
    }
    MembersByRow.clear();
    MembersBySet.clear();
    return false;
}

struct CubeCollector {
    std::array<D2RL::Items::ItemInfo, MaximumCubeItems> items{};
    std::size_t count{};
    bool overflow{};
};

auto __cdecl CollectCubeItem(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* item,
        void* userData) noexcept -> D2RL::Inventory::IterationAction {
    auto* collector = static_cast<CubeCollector*>(userData);
    if (collector == nullptr || item == nullptr
            || item->structSize < D2RL::Items::ItemInfoRequiredSize) {
        return D2RL::Inventory::IterationAction::Continue;
    }
    if (collector->count >= collector->items.size()) {
        collector->overflow = true;
        return D2RL::Inventory::IterationAction::Stop;
    }
    collector->items[collector->count++] = *item;
    return D2RL::Inventory::IterationAction::Continue;
}

auto FindMatchingExchange(D2RL::PlayerHandle player, CubeCollector& cube,
        PendingExchange& exchange) -> bool {
    const std::size_t configuredIngredientCount =
        static_cast<std::size_t>(Config.useInputX) + static_cast<std::size_t>(Config.useInputY);
    const std::size_t expectedCount = 1 + configuredIngredientCount;
    if (cube.overflow || cube.count != expectedCount) return false;
    const D2RL::Items::ItemInfo* setItem{};
    std::array<const D2RL::Items::ItemInfo*, 2> ingredients{};
    std::size_t ingredientCount{};
    for (std::size_t i = 0; i < cube.count; ++i) {
        const auto& item = cube.items[i];
        if (item.quality == D2RL::Items::Quality::Set) {
            if (setItem != nullptr) return false;
            setItem = &item;
        } else if (ingredientCount < ingredients.size()) {
            ingredients[ingredientCount++] = &item;
        }
    }
    const std::size_t expectedIngredientCount = configuredIngredientCount;
    if (setItem == nullptr || ingredientCount != expectedIngredientCount
            || setItem->qualityRecordId < 0) return false;
    const auto member = MembersByRow.find(static_cast<std::uint32_t>(setItem->qualityRecordId));
    if (member == MembersByRow.end() || member->second.itemCode != setItem->code) return false;
    const auto targets = MembersBySet.find(member->second.setName);
    if (targets == MembersBySet.end()) return false;

    std::array<std::uint32_t, 2> requiredCodes{};
    std::size_t requiredCount{};
    if (Config.useInputX) requiredCodes[requiredCount++] = Config.inputX;
    if (Config.useInputY) requiredCodes[requiredCount++] = Config.inputY;
    std::array<bool, 2> matchedIngredients{};
    for (std::size_t required = 0; required < requiredCount; ++required) {
        bool found{};
        for (std::size_t actual = 0; actual < ingredientCount; ++actual) {
            if (!matchedIngredients[actual] && ingredients[actual]->code == requiredCodes[required]) {
                matchedIngredients[actual] = true;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }

    exchange = {};
    exchange.player = player;
    exchange.sourceRowId = static_cast<std::uint32_t>(setItem->qualityRecordId);
    exchange.itemLevel = setItem->itemLevel;
    exchange.stateFlags = setItem->stateFlags & D2RL::Items::ItemStateIdentified;
    exchange.inputs[0] = {setItem->handle, 1,
        D2RL::Items::SocketedItemPolicy::RejectIfNotEmpty};
    exchange.inputCount = 1;
    for (std::size_t i = 0; i < ingredientCount; ++i) {
        exchange.inputs[exchange.inputCount++] = {ingredients[i]->handle, 1,
            D2RL::Items::SocketedItemPolicy::RejectIfNotEmpty};
    }
    exchange.possibleOutputs.reserve(targets->second.size());
    for (const auto& target : targets->second) {
        // Exclude every row using the source base code, not only the exact
        // SetItems row. Mods can define several set rows for the same base.
        if (target.itemCode != setItem->code) {
            exchange.possibleOutputs.push_back(target);
        }
    }
    return !exchange.possibleOutputs.empty();
}

auto ConsumeFailedRecipeIngredients(const D2RL::PluginContext* context,
        const PendingExchange& exchange) noexcept -> D2RL::Items::Result {
    if (exchange.inputCount <= 1) return D2RL::Items::Result::Success;
    D2RL::Items::Transaction transaction{
        .structSize = D2RL::Items::TransactionSize,
        .flags = 0,
        .player = exchange.player,
        .inputCount = exchange.inputCount - 1,
        .outputCount = 0,
        .inputs = exchange.inputs.data() + 1,
        .outputs = nullptr,
        .outputItems = nullptr,
        .outputCapacity = 0,
        .reserved = 0,
    };
    D2RL::Items::TransactionResult result{
        .structSize = D2RL::Items::TransactionResultSize,
        .flags = 0,
        .outputCount = 0,
        .reserved = 0,
    };
    return ItemService->executeTransaction(context, &transaction, &result);
}

void __cdecl ExecuteExchange(const D2RL::PluginContext* context, void*) noexcept {
    const std::lock_guard<std::mutex> lock(PendingMutex);
    if (context == nullptr || context != Context || !Active.load(std::memory_order_acquire)) {
        Pending = {};
        ExchangePending.store(false, std::memory_order_release);
        return;
    }

    CubeCollector cube;
    const D2RL::Inventory::ItemFilter filter{
        .structSize = D2RL::Inventory::ItemFilterSize,
        .flags = 0,
        .containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Cube),
        .reserved = 0,
    };
    const auto inventoryResult = InventoryService->forEachInventoryItem(context,
        Pending.player, &filter, CollectCubeItem, &cube);
    if (inventoryResult != D2RL::Inventory::Result::Success) {
        char failureMessage[144]{};
        (void)std::snprintf(failureMessage, sizeof(failureMessage),
            "RandomSetPiece: could not refresh cube items on the game thread (Inventory result=%u).",
            static_cast<unsigned>(inventoryResult));
        Context->LogWarn(failureMessage);
        Pending = {};
        ExchangePending.store(false, std::memory_order_release);
        return;
    }

    PendingExchange exchange;
    if (!FindMatchingExchange(Pending.player, cube, exchange)
            || exchange.sourceRowId != Pending.sourceRowId) {
        Context->LogWarn("RandomSetPiece: cube contents changed before the game-thread transaction; no items were changed.");
        Pending = {};
        ExchangePending.store(false, std::memory_order_release);
        return;
    }

    std::uint32_t failureRoll{};
    if (Config.failureChancePercent != 0) {
        failureRoll = std::uniform_int_distribution<std::uint32_t>(1, 100)(RandomEngine);
        char rollMessage[128]{};
        (void)std::snprintf(rollMessage, sizeof(rollMessage),
            "RandomSetPiece: failure roll=%u/100, configured chance=%u%%.",
            failureRoll, Config.failureChancePercent);
        Context->LogInfo(rollMessage);
    }
    if (failureRoll != 0 && failureRoll <= Config.failureChancePercent) {
        const auto failureStatus = ConsumeFailedRecipeIngredients(context, exchange);
        if (failureStatus == D2RL::Items::Result::Success) {
            char failureMessage[224]{};
            (void)std::snprintf(failureMessage, sizeof(failureMessage),
                (Config.useInputX || Config.useInputY)
                    ? "RandomSetPiece: failure roll %u/%u hit; configured ingredients were consumed and the set item was preserved."
                    : "RandomSetPiece: failure roll %u/%u hit; this free recipe has no ingredients to consume, so the set item was preserved.",
                failureRoll, Config.failureChancePercent);
            Context->LogInfo(failureMessage);
        } else {
            char failureMessage[224]{};
            (void)std::snprintf(failureMessage, sizeof(failureMessage),
                "RandomSetPiece: failure roll %u/%u hit, but ingredient transaction rolled back (ItemService result=%u); no items were changed.",
                failureRoll, Config.failureChancePercent, static_cast<unsigned>(failureStatus));
            Context->LogWarn(failureMessage);
        }
        Pending = {};
        ExchangePending.store(false, std::memory_order_release);
        return;
    }

    // Some valid SetItems rows cannot be created by the runtime item service
    // in every context. Keep the choice random, but retry remaining rows when
    // a candidate is specifically rejected as Unsupported. The transaction
    // API stages and rolls back failed outputs atomically, so refreshed inputs
    // remain safe to use for the next candidate.
    auto outputCandidates = exchange.possibleOutputs;
    std::shuffle(outputCandidates.begin(), outputCandidates.end(), RandomEngine);
    D2RL::Items::Result status = D2RL::Items::Result::NotFound;
    std::uint32_t rejectedCandidates{};
    std::uint32_t attemptedCandidates{};
    std::uint32_t completedRow{};
    std::uint32_t completedCode{};
    for (const SetMember& outputMember : outputCandidates) {
        ++attemptedCandidates;
        D2RL::Items::ItemCreateSpec output{};
        output.structSize = D2RL::Items::ItemCreateSpecSize;
        output.code = outputMember.itemCode;
        output.quality = D2RL::Items::Quality::Set;
        output.qualityRecordId = outputMember.rowId;
        output.itemLevel = exchange.itemLevel;
        output.seedMode = D2RL::Items::SeedMode::Random;
        output.quantity = D2RL::Items::DefaultValue;
        output.durability = D2RL::Items::DefaultValue;
        // Unlike quantity and durability, the SDK defines socketCount as an
        // exact count. DefaultValue is not a valid "native default" sentinel here.
        output.socketCount = 0;
        output.stateFlags = exchange.stateFlags;
        output.destination = {
            .structSize = D2RL::Items::ItemDestinationSize,
            .flags = 0,
            .container = D2RL::Items::ItemContainer::Cube,
            .placement = D2RL::Items::Placement::Automatic,
            .customPageHandle = 0,
            .x = 0,
            .y = 0,
        };

        D2RL::ItemHandle createdItem{D2RL::InvalidItemHandle};
        D2RL::Items::Transaction transaction{
            .structSize = D2RL::Items::TransactionSize,
            .flags = 0,
            .player = exchange.player,
            .inputCount = exchange.inputCount,
            .outputCount = 1,
            .inputs = exchange.inputs.data(),
            .outputs = &output,
            .outputItems = &createdItem,
            .outputCapacity = 1,
            .reserved = 0,
        };
        D2RL::Items::TransactionResult result{
            .structSize = D2RL::Items::TransactionResultSize,
            .flags = 0,
            .outputCount = 0,
            .reserved = 0,
        };
        status = ItemService->executeTransaction(context, &transaction, &result);
        if (status == D2RL::Items::Result::Success) {
            completedRow = outputMember.rowId;
            completedCode = outputMember.itemCode;
            break;
        }
        if (status != D2RL::Items::Result::Unsupported) break;
        ++rejectedCandidates;
    }

    if (status == D2RL::Items::Result::Success) {
        if (rejectedCandidates == 0) {
            Context->LogInfo("RandomSetPiece: cube recipe completed; output is another piece of the same set.");
        } else {
            char successMessage[192]{};
            (void)std::snprintf(successMessage, sizeof(successMessage),
                "RandomSetPiece: cube recipe completed with output row=%u, code=%08X after skipping %u unsupported candidate(s).",
                completedRow, completedCode, rejectedCandidates);
            Context->LogInfo(successMessage);
        }
    } else {
        char failureMessage[224]{};
        (void)std::snprintf(failureMessage, sizeof(failureMessage),
            "RandomSetPiece: transaction rolled back (ItemService result=%u; tried %u output candidate(s), %u returned Unsupported).",
            static_cast<unsigned>(status), attemptedCandidates, rejectedCandidates);
        Context->LogWarn(failureMessage);
    }
    Pending = {};
    ExchangePending.store(false, std::memory_order_release);
}

auto __cdecl OnUiMessage(const D2RL::PluginContext* context,
        const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
        -> D2RL::SharedEvents::UiMessageAction {
    if (context != Context || event == nullptr
            || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize
            || event->target == nullptr || event->command == nullptr
            || std::string_view(event->target) != ConvertTarget
            || std::string_view(event->command) != ConvertCommand
            || !Active.load(std::memory_order_acquire)) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }

    try {
        if (!ConvertMessageLogged.exchange(true, std::memory_order_acq_rel)) {
            Context->LogInfo("RandomSetPiece: observed the Horadric Cube Convert message.");
        }
        D2RL::PlayerHandle player{D2RL::InvalidPlayerHandle};
        if (InventoryService->getLocalPlayer(context, &player) != D2RL::Inventory::Result::Success
                || player == D2RL::InvalidPlayerHandle) {
            return D2RL::SharedEvents::UiMessageAction::Continue;
        }
        CubeCollector cube;
        const D2RL::Inventory::ItemFilter filter{
            .structSize = D2RL::Inventory::ItemFilterSize,
            .flags = 0,
            .containerMask = D2RL::Items::ContainerBit(D2RL::Items::ItemContainer::Cube),
            .reserved = 0,
        };
        if (InventoryService->forEachInventoryItem(context, player, &filter,
                CollectCubeItem, &cube) != D2RL::Inventory::Result::Success) {
            return D2RL::SharedEvents::UiMessageAction::Continue;
        }
        PendingExchange candidate;
        if (!FindMatchingExchange(player, cube, candidate)) {
            if (!FirstRecipeMismatchLogged.exchange(true, std::memory_order_acq_rel)) {
                Context->LogInfo("RandomSetPiece: Convert did not match; the Cube must contain one enabled set item and exactly the configured X/Y ingredients.");
            }
            return D2RL::SharedEvents::UiMessageAction::Continue;
        }
        if (ExchangePending.exchange(true, std::memory_order_acq_rel)) {
            return D2RL::SharedEvents::UiMessageAction::Consume;
        }
        {
            const std::lock_guard<std::mutex> lock(PendingMutex);
            Pending.player = candidate.player;
            Pending.sourceRowId = candidate.sourceRowId;
        }
        if (ThreadService->runOnGameThread(context, ExecuteExchange, nullptr)
                != D2RL::Threads::Result::Success) {
            const std::lock_guard<std::mutex> lock(PendingMutex);
            Pending = {};
            ExchangePending.store(false, std::memory_order_release);
            return D2RL::SharedEvents::UiMessageAction::Continue;
        }
        return D2RL::SharedEvents::UiMessageAction::Consume;
    } catch (...) {
        Context->LogError("RandomSetPiece: failed to prepare the cube transaction; no items were changed.");
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
}

auto Initialize(const D2RL::PluginContext* context) -> bool {
    if (!ReadConfiguration()) return false;

    if (context->QueryService(D2RL::ServiceId::Inventory,
            D2RL::InventoryServiceV1Version, &InventoryService)
            != D2RL::ServiceQueryResult::Success
            || InventoryService == nullptr
            || InventoryService->serviceSize < D2RL::InventoryServiceV1RequiredSize
            || InventoryService->getLocalPlayer == nullptr
            || InventoryService->forEachInventoryItem == nullptr) {
        context->LogError("RandomSetPiece: D2RLoader InventoryService v1 is required.");
        return false;
    }
    if (context->QueryService(D2RL::ServiceId::Item,
            D2RL::ItemServiceV1Version, &ItemService)
            != D2RL::ServiceQueryResult::Success
            || ItemService == nullptr
            || ItemService->serviceSize < D2RL::ItemServiceV1RequiredSize
            || ItemService->executeTransaction == nullptr) {
        context->LogError("RandomSetPiece: D2RLoader ItemService v1 transaction support is required.");
        return false;
    }
    if (context->QueryService(D2RL::ServiceId::Thread,
            D2RL::ThreadServiceV1Version, &ThreadService)
            != D2RL::ServiceQueryResult::Success
            || ThreadService == nullptr
            || ThreadService->serviceSize < D2RL::ThreadServiceV1RequiredSize
            || ThreadService->runOnGameThread == nullptr) {
        context->LogError("RandomSetPiece: D2RLoader game-thread service is required.");
        return false;
    }
    if (context->QueryService(D2RL::ServiceId::SharedEvent,
            D2RL::SharedEventServiceV1Version, &EventService)
            != D2RL::ServiceQueryResult::Success
            || EventService == nullptr
            || EventService->serviceSize < D2RL::SharedEventServiceV1RequiredSize
            || EventService->registerUiMessageListener == nullptr
            || EventService->unregisterUiMessageListener == nullptr) {
        context->LogError("RandomSetPiece: D2RLoader shared UI-message service is required.");
        return false;
    }
    if (!LoadSetMembers()) {
        context->LogWarn("RandomSetPiece: sets.txt and setitems.txt were not found or could not be parsed; the recipe will stay inactive.");
    } else {
        const std::string loadedMessage = "RandomSetPiece: loaded "
            + std::to_string(MembersByRow.size()) + " enabled set-piece rows across "
            + std::to_string(MembersBySet.size()) + " sets.";
        context->LogInfo(loadedMessage.c_str());
    }

    std::uint64_t seed = GetTickCount64() ^ (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32U);
    try {
        std::random_device random;
        seed ^= (static_cast<std::uint64_t>(random()) << 32U) | random();
    } catch (...) {
    }
    RandomEngine.seed(static_cast<std::mt19937::result_type>(seed ^ (seed >> 32U)));

    const D2RL::SharedEvents::UiMessageListener listener{
        .structSize = D2RL::SharedEvents::UiMessageListenerSize,
        .flags = 0,
        .priority = 10000,
        .reserved = 0,
        .callback = OnUiMessage,
        .userData = nullptr,
    };
    if (EventService->registerUiMessageListener(context, &listener, &MessageHandle)
            != D2RL::SharedEvents::Result::Success
            || MessageHandle == D2RL::SharedEvents::InvalidHandle) {
        context->LogError("RandomSetPiece: could not register for the Horadric Cube Convert action.");
        return false;
    }
    Active.store(true, std::memory_order_release);
    if (MembersByRow.empty()) {
        context->LogWarn("RandomSetPiece 0.4.3 loaded, but no set table members are available yet.");
    } else {
        char readyMessage[160]{};
        (void)std::snprintf(readyMessage, sizeof(readyMessage),
            "RandomSetPiece 0.4.3 is ready; recipe failure chance=%u%%.",
            Config.failureChancePercent);
        context->LogInfo(readyMessage);
    }
    return true;
}

void Shutdown() noexcept {
    Active.store(false, std::memory_order_release);
    if (Context != nullptr && EventService != nullptr
            && MessageHandle != D2RL::SharedEvents::InvalidHandle
            && EventService->unregisterUiMessageListener != nullptr) {
        (void)EventService->unregisterUiMessageListener(Context, MessageHandle);
    }
    MessageHandle = D2RL::SharedEvents::InvalidHandle;
    const std::lock_guard<std::mutex> lock(PendingMutex);
    ExchangePending.store(false, std::memory_order_release);
    Pending = {};
    MembersByRow.clear();
    MembersBySet.clear();
    Config = {};
    EventService = nullptr;
    ThreadService = nullptr;
    ItemService = nullptr;
    InventoryService = nullptr;
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    Shutdown();
    if (!D2RL::HasContext(context) || context->apiVersion != D2RL_PLUGIN_API_VERSION) return false;
    Context = context;
    bool initialized{};
    try {
        initialized = Initialize(context);
    } catch (...) {
        Context->LogError("RandomSetPiece: plugin initialization failed.");
    }
    if (!initialized) {
        Shutdown();
        Context = nullptr;
        return false;
    }
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Shutdown();
    Context = nullptr;
}
