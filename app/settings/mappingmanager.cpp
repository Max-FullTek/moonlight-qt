#include "mappingmanager.h"
#include "path.h"
#ifdef LITE_NATIVE_WORKER_JSON
#include "backend/workerpreferences.h"
#endif

#include <QDir>
#ifndef LITE_GPUI_WORKER
#include <QSettings>
#endif

#include "SDL_compat.h"

#define SER_GAMEPADMAPPING "gcmapping"

#define SER_GUID "guid"
#define SER_MAPPING "mapping"

MappingManager::MappingManager()
{
#ifdef LITE_NATIVE_WORKER_JSON
    if (!WorkerPreferences::directory().empty()) {
        try {
            using namespace LegacyWorkerProfile;
            const auto values = WorkerPreferences::load(WorkerPreferences::Section::ControllerMappings);
            const int count = number(values, "size");
            if (count < 0 || static_cast<size_t>(count) > values.size()) {
                throw std::runtime_error("Invalid controller mapping count");
            }
            for (int i = 1; i <= count; ++i) {
                const auto prefix = std::to_string(i) + '/';
                SdlGamepadMapping mapping(QString::fromUtf8(value(values, prefix + SER_GUID)),
                                          QString::fromUtf8(value(values, prefix + SER_MAPPING)));
                addMapping(mapping);
            }
        } catch (const std::exception&) {
            qWarning("Unable to read worker controller mappings");
        }
    } else
#endif
#ifndef LITE_GPUI_WORKER
    {
        QSettings settings;

        const int mappingCount = settings.beginReadArray(SER_GAMEPADMAPPING);
        for (int i = 0; i < mappingCount; i++) {
            settings.setArrayIndex(i);

            SdlGamepadMapping mapping(settings.value(SER_GUID).toString(), settings.value(SER_MAPPING).toString());
            addMapping(mapping);
        }
        settings.endArray();
    }
#else
    {
        qFatal("GPUI controller mappings require an initialized worker profile");
    }
#endif

    // Finally load mappings from SDL_HINT_GAMECONTROLLERCONFIG
    QStringList sdlMappings =
            QString::fromLocal8Bit(SDL_GetHint(SDL_HINT_GAMECONTROLLERCONFIG))
        #if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            .split('\n', Qt::SkipEmptyParts);
        #else
            .split('\n', QString::SkipEmptyParts);
        #endif
    for (const QString& sdlMapping : std::as_const(sdlMappings)) {
        SdlGamepadMapping mapping(sdlMapping);
        addMapping(mapping);
    }

    // Save the updated mappings to settings
    save();
}

void MappingManager::save()
{
#ifdef LITE_NATIVE_WORKER_JSON
    if (!WorkerPreferences::directory().empty()) {
        try {
            // Validate the existing file before replacing it with live mappings.
            WorkerPreferences::load(WorkerPreferences::Section::ControllerMappings);
            LegacyWorkerProfile::Values values;
            values["size"] = std::to_string(m_Mappings.size());
            int index = 0;
            for (const auto& mapping : m_Mappings) {
                const auto prefix = std::to_string(++index) + '/';
                values[prefix + SER_GUID] = mapping.getGuid().toUtf8().toStdString();
                values[prefix + SER_MAPPING] = mapping.getMapping().toUtf8().toStdString();
            }
            WorkerPreferences::save(WorkerPreferences::Section::ControllerMappings, values);
        } catch (const std::exception&) {
            qWarning("Unable to save worker controller mappings");
        }
        return;
    }
#endif
#ifndef LITE_GPUI_WORKER
    QSettings settings;

    settings.remove(SER_GAMEPADMAPPING);
    settings.beginWriteArray(SER_GAMEPADMAPPING);
    QList<SdlGamepadMapping> mappings = m_Mappings.values();
    for (int i = 0; i < mappings.count(); i++) {
        settings.setArrayIndex(i);

        settings.setValue(SER_GUID, mappings[i].getGuid());
        settings.setValue(SER_MAPPING, mappings[i].getMapping());
    }
    settings.endArray();
#endif
}

void MappingManager::applyMappings()
{
    QByteArray mappingData = Path::readDataFile("gamecontrollerdb.txt");
    if (!mappingData.isEmpty()) {
        int newMappings = SDL_GameControllerAddMappingsFromRW(
                    SDL_RWFromConstMem(mappingData.constData(), mappingData.size()), 1);

        if (newMappings > 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Loaded %d new gamepad mappings",
                        newMappings);
        }
        else {
            if (newMappings < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Error loading gamepad mappings: %s",
                             SDL_GetError());
            }
            else if (newMappings == 0) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "0 new mappings found in gamecontrollerdb.txt. Is it corrupt?");
            }

            // Try deleting the cached mapping list just in case it's corrupt
            Path::deleteCacheFile("gamecontrollerdb.txt");
        }
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to load gamepad mapping file");
    }

    QList<SdlGamepadMapping> mappings = m_Mappings.values();
    for (const SdlGamepadMapping& mapping : std::as_const(mappings)) {
        QString sdlMappingString = mapping.getSdlMappingString();
        int ret = SDL_GameControllerAddMapping(qPrintable(sdlMappingString));
        if (ret < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to add mapping: %s",
                        qPrintable(sdlMappingString));
        }
        else if (ret == 1) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Loaded saved user mapping: %s",
                        qPrintable(sdlMappingString));
        }
    }
}

void MappingManager::addMapping(QString mappingString)
{
    SdlGamepadMapping mapping(mappingString);
    addMapping(mapping);
}

void MappingManager::addMapping(SdlGamepadMapping& mapping)
{
    m_Mappings[mapping.getGuid()] = mapping;
}
