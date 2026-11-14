#pragma once

#include "ARA_Library/PlugIn/ARAPlug.h"
#include <array>

namespace TuneLab::Cubase
{

// M0 archives only a version marker for the empty probe document. It does not
// serialize TuneLab edits or ARA source/modification content yet.
class ProbeDocumentController final : public ARA::PlugIn::DocumentController
{
  public:
    using DocumentController::DocumentController;

  protected:
    // M0 does not consume host content or sample data. These hooks become
    // model/cache invalidation points when the document adapter is implemented.
    void doUpdateMusicalContextContent(ARA::PlugIn::MusicalContext *,
                                       const ARA::ARAContentTimeRange *,
                                       ARA::ContentUpdateScopes) noexcept override
    {
    }

    void doUpdateAudioSourceContent(ARA::PlugIn::AudioSource *, const ARA::ARAContentTimeRange *,
                                    ARA::ContentUpdateScopes) noexcept override
    {
    }

    void willEnableAudioSourceSamplesAccess(ARA::PlugIn::AudioSource *, bool) noexcept override
    {
    }

    void didEnableAudioSourceSamplesAccess(ARA::PlugIn::AudioSource *, bool) noexcept override
    {
    }

    bool doRestoreObjectsFromArchive(ARA::PlugIn::HostArchiveReader *reader,
                                     const ARA::PlugIn::RestoreObjectsFilter *) noexcept override
    {
        std::array<ARA::ARAByte, 8> bytes{};
        return reader->getArchiveSize() == archiveMarker.size() &&
               reader->readBytesFromArchive(0, bytes.size(), bytes.data()) &&
               bytes == archiveMarker;
    }

    bool doStoreObjectsToArchive(ARA::PlugIn::HostArchiveWriter *writer,
                                 const ARA::PlugIn::StoreObjectsFilter *) noexcept override
    {
        return writer->writeBytesToArchive(0, archiveMarker.size(), archiveMarker.data());
    }

  private:
    inline static constexpr std::array<ARA::ARAByte, 8> archiveMarker{'T', 'L', 'P', 'R',
                                                                      'O', 'B', 'E', 1};
};

class ProbeFactoryConfig : public ARA::PlugIn::FactoryConfig
{
  public:
    const char *getFactoryID() const noexcept override
    {
        return "org.tunelab.cubase.probe.factory.1";
    }

    const char *getPlugInName() const noexcept override
    {
        return "TuneLab ARA Bridge Probe";
    }

    const char *getManufacturerName() const noexcept override
    {
        return "TuneLab";
    }

    const char *getInformationURL() const noexcept override
    {
        return "https://github.com/AI-Hobbyist/TuneLab";
    }

    const char *getVersion() const noexcept override
    {
        return "0.1.0";
    }

    const char *getDocumentArchiveID() const noexcept override
    {
        return "org.tunelab.cubase.probe.archive.1";
    }
};

inline const ARA::ARAFactory *getProbeFactory() noexcept
{
    return ARA::PlugIn::PlugInEntry::getPlugInEntry<ProbeFactoryConfig, ProbeDocumentController>()
        ->getFactory();
}

} // namespace TuneLab::Cubase
