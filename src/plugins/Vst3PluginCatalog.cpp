#include "plugins/Vst3PluginCatalog.h"

#include <algorithm>
#include <set>

namespace dawhermes::plugins {

namespace {

constexpr const char* kCatalogFileName = "vst3-instruments.xml";
constexpr const char* kDeadMansPedalFileName = "vst3-scan-dead-man.txt";

bool descriptionOrder(
    const juce::PluginDescription& left,
    const juce::PluginDescription& right)
{
    const auto leftName = left.name.toLowerCase();
    const auto rightName = right.name.toLowerCase();
    if (leftName != rightName) {
        return leftName < rightName;
    }
    const auto leftManufacturer =
        left.manufacturerName.toLowerCase();
    const auto rightManufacturer =
        right.manufacturerName.toLowerCase();
    if (leftManufacturer != rightManufacturer) {
        return leftManufacturer < rightManufacturer;
    }
    return stableVst3Identifier(left)
        < stableVst3Identifier(right);
}

juce::String scanCandidateKey(juce::String candidate)
{
    return candidate.trim()
        .replaceCharacter('/', '\\')
        .toLowerCase();
}

}  // namespace

juce::String stableVst3Identifier(
    const juce::PluginDescription& description)
{
    const auto uid = description.uniqueId != 0
        ? description.uniqueId
        : description.deprecatedUid;
    return "VST3:"
        + juce::String::toHexString(uid)
        + ":"
        + description.manufacturerName.trim().toLowerCase()
        + ":"
        + description.name.trim().toLowerCase();
}

std::vector<juce::PluginDescription> filterAndSortVst3Instruments(
    const juce::Array<juce::PluginDescription>& descriptions)
{
    std::vector<juce::PluginDescription> result;
    std::set<juce::String> identifiers;
    for (const auto& description : descriptions) {
        const auto identifier =
            stableVst3Identifier(description);
        if (description.pluginFormatName != "VST3"
            || !description.isInstrument
            || description.numOutputChannels <= 0
            || identifier.isEmpty()
            || !identifiers.insert(identifier).second) {
            continue;
        }
        result.push_back(description);
    }
    std::sort(result.begin(), result.end(), descriptionOrder);
    return result;
}

Vst3ScanRecoveryPlan prepareVst3ScanRecoveryPlan(
    const juce::StringArray& candidates,
    const juce::StringArray& deadMansPedalEntries,
    bool retryRecoveredFailures)
{
    Vst3ScanRecoveryPlan result;
    std::set<juce::String> candidateKeys;
    for (const auto& candidate : candidates) {
        const auto key = scanCandidateKey(candidate);
        if (key.isNotEmpty()) {
            candidateKeys.insert(key);
        }
    }

    std::set<juce::String> recoveredKeys;
    std::set<juce::String> staleKeys;
    for (const auto& entry : deadMansPedalEntries) {
        const auto key = scanCandidateKey(entry);
        if (key.isEmpty()) {
            continue;
        }
        if (candidateKeys.contains(key)) {
            recoveredKeys.insert(key);
        } else {
            staleKeys.insert(key);
        }
    }

    result.recoveredFailureCount =
        static_cast<int>(recoveredKeys.size());
    result.staleRecoveryCount =
        static_cast<int>(staleKeys.size());
    for (const auto& candidate : candidates) {
        if (retryRecoveredFailures
            || !recoveredKeys.contains(
                scanCandidateKey(candidate))) {
            result.candidatesToScan.add(candidate);
        }
    }
    return result;
}

Vst3PluginCatalog::Vst3PluginCatalog(
    juce::PropertiesFile* settings)
    : juce::Thread("DAWHermes VST3 scanner"),
      settings_(settings)
{
    formatManager_.addFormat(
        std::make_unique<juce::VST3PluginFormat>());
    load();
}

Vst3PluginCatalog::~Vst3PluginCatalog()
{
    requestCancel();
    stopThread(10000);
}

std::vector<juce::PluginDescription>
Vst3PluginCatalog::instruments() const
{
    const std::scoped_lock lock(mutex_);
    return instruments_;
}

std::optional<juce::PluginDescription>
Vst3PluginCatalog::findByIdentifier(
    const juce::String& identifier) const
{
    const std::scoped_lock lock(mutex_);
    const auto found = std::find_if(
        instruments_.begin(),
        instruments_.end(),
        [&identifier](const auto& candidate) {
            return stableVst3Identifier(candidate)
                       == identifier
                || candidate.matchesIdentifierString(
                    identifier);
        });
    return found == instruments_.end()
        ? std::optional<juce::PluginDescription> {}
        : std::optional<juce::PluginDescription> { *found };
}

juce::AudioPluginFormatManager&
Vst3PluginCatalog::formatManager() noexcept
{
    return formatManager_;
}

juce::FileSearchPath
Vst3PluginCatalog::defaultSearchPath() const
{
    auto* format = formatManager_.getFormat(0);
    return format == nullptr
        ? juce::FileSearchPath {}
        : format->getDefaultLocationsToSearch();
}

juce::File Vst3PluginCatalog::catalogFile() const
{
    return settingsSibling(kCatalogFileName);
}

juce::File Vst3PluginCatalog::deadMansPedalFile() const
{
    return settingsSibling(kDeadMansPedalFileName);
}

bool Vst3PluginCatalog::startScan(bool rescanExisting)
{
    if (isThreadRunning()) {
        return false;
    }
    {
        const std::scoped_lock lock(mutex_);
        status_ = {};
        status_.running = true;
        status_.summary =
            "Scanning standard VST3 locations...";
        rescanExisting_ = rescanExisting;
    }
    cancelRequested_.store(false, std::memory_order_release);
    return startThread();
}

void Vst3PluginCatalog::requestCancel() noexcept
{
    cancelRequested_.store(true, std::memory_order_release);
    signalThreadShouldExit();
    const std::scoped_lock lock(mutex_);
    status_.cancelRequested = true;
}

Vst3ScanStatus Vst3PluginCatalog::scanStatus() const
{
    const std::scoped_lock lock(mutex_);
    return status_;
}

void Vst3PluginCatalog::run()
{
    juce::KnownPluginList candidateList;
    if (!rescanExisting_) {
        for (const auto& existing : instruments()) {
            candidateList.addType(existing);
        }
    }

    auto* format = formatManager_.getFormat(0);
    if (format == nullptr) {
        const std::scoped_lock lock(mutex_);
        status_.running = false;
        status_.completed = true;
        status_.summary = "VST3 host format is unavailable.";
        return;
    }

    const auto discoveredCandidates =
        format->searchPathsForPlugins(
            defaultSearchPath(),
            true,
            false);
    juce::StringArray deadMansPedalEntries;
    deadMansPedalFile().readLines(
        deadMansPedalEntries);
    deadMansPedalEntries.removeEmptyStrings();
    const auto recoveryPlan =
        prepareVst3ScanRecoveryPlan(
            discoveredCandidates,
            deadMansPedalEntries,
            rescanExisting_);

    juce::PluginDirectoryScanner scanner(
        candidateList,
        *format,
        defaultSearchPath(),
        true,
        deadMansPedalFile(),
        false);
    scanner.setFilesOrIdentifiersToScan(
        recoveryPlan.candidatesToScan);
    {
        const std::scoped_lock lock(mutex_);
        status_.recoverySkippedCount =
            rescanExisting_
            ? 0
            : recoveryPlan.recoveredFailureCount;
        status_.staleRecoveryCount =
            recoveryPlan.staleRecoveryCount;
    }

    juce::String current;
    bool more =
        !recoveryPlan.candidatesToScan.isEmpty();
    while (more
           && !threadShouldExit()
           && !cancelRequested_.load(std::memory_order_acquire)) {
        current = scanner.getNextPluginFileThatWillBeScanned();
        {
            const std::scoped_lock lock(mutex_);
            status_.currentCandidate =
                current.isEmpty()
                    ? "Preparing candidate..."
                    : juce::File(current).getFileName();
            status_.progress = scanner.getProgress();
        }
        more = scanner.scanNextFile(
            !rescanExisting_,
            current);
    }
    candidateList.scanFinished();

    const auto cancelled =
        threadShouldExit()
        || cancelRequested_.load(std::memory_order_acquire);
    const auto filtered =
        filterAndSortVst3Instruments(candidateList.getTypes());
    const auto failedCount = scanner.getFailedFiles().size();
    auto publishedInstrumentCount =
        static_cast<int>(instruments().size());

    if (!cancelled) {
        juce::KnownPluginList persisted;
        for (const auto& description : filtered) {
            persisted.addType(description);
        }
        if (save(persisted)) {
            const std::scoped_lock lock(mutex_);
            instruments_ = filtered;
            status_.catalogReplaced = true;
            publishedInstrumentCount =
                static_cast<int>(instruments_.size());
        }
    }

    const std::scoped_lock lock(mutex_);
    status_.running = false;
    status_.completed = true;
    status_.progress = cancelled ? status_.progress : 1.0f;
    status_.failedCount = failedCount;
    status_.instrumentCount = publishedInstrumentCount;
    status_.currentCandidate.clear();
    if (cancelled) {
        status_.summary =
            "Scan cancelled. The previous catalog was retained.";
    } else if (status_.catalogReplaced) {
        status_.summary =
            "VST3 instrument scan completed.";
    } else {
        status_.summary =
            "Scan finished, but the catalog could not be saved; the previous catalog was retained.";
    }
}

void Vst3PluginCatalog::load()
{
    const auto file = catalogFile();
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) {
        return;
    }
    juce::KnownPluginList list;
    list.recreateFromXml(*xml);
    const auto filtered =
        filterAndSortVst3Instruments(list.getTypes());
    const std::scoped_lock lock(mutex_);
    instruments_ = filtered;
}

bool Vst3PluginCatalog::save(
    const juce::KnownPluginList& list)
{
    const auto file = catalogFile();
    if (!file.getParentDirectory().createDirectory()) {
        return false;
    }
    const auto xml = list.createXml();
    return xml != nullptr && xml->writeTo(file);
}

juce::File Vst3PluginCatalog::settingsSibling(
    const juce::String& name) const
{
    if (settings_ != nullptr
        && settings_->getFile().getParentDirectory()
               != juce::File {}) {
        return settings_->getFile()
            .getParentDirectory()
            .getChildFile(name);
    }
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("DAWHermes")
        .getChildFile(name);
}

}  // namespace dawhermes::plugins
