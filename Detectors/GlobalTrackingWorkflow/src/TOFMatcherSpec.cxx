// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// @file   TOFMatcherSpec.cxx

#include <vector>
#include <string>
#include "TStopwatch.h"
#include "Framework/ConfigParamRegistry.h"
#include "DetectorsBase/GeometryManager.h"
#include "DetectorsBase/Propagator.h"
#include "DetectorsCommonDataFormats/NameConf.h"
#include "DataFormatsParameters/GRPObject.h"
#include "Headers/DataHeader.h"
#include "CommonDataFormat/InteractionRecord.h"
#include "DataFormatsGlobalTracking/RecoContainer.h"
#include "Framework/Task.h"
#include "Framework/DataProcessorSpec.h"

// from Tracks
#include "ReconstructionDataFormats/GlobalTrackID.h"
#include "ReconstructionDataFormats/GlobalTrackAccessor.h"
#include "ReconstructionDataFormats/GlobalTrackID.h"
#include "DataFormatsTPC/TrackTPC.h"
#include "DataFormatsITS/TrackITS.h"
#include "ReconstructionDataFormats/TrackTPCITS.h"
#include "ReconstructionDataFormats/TrackTPCTOF.h"

// from TOF
#include "DataFormatsTOF/Cluster.h"
#include "GlobalTracking/MatchTOF.h"
#include "GlobalTrackingWorkflow/TOFMatcherSpec.h"

using namespace o2::framework;
// using MCLabelsTr = gsl::span<const o2::MCCompLabel>;
// using GID = o2::dataformats::GlobalTrackID;
// using DetID = o2::detectors::DetID;

using evIdx = o2::dataformats::EvIndex<int, int>;
using MatchOutputType = std::vector<o2::dataformats::MatchInfoTOF>;
using GID = o2::dataformats::GlobalTrackID;

namespace o2
{
namespace globaltracking
{

class TOFMatcherSpec : public Task
{
 public:
  TOFMatcherSpec(std::shared_ptr<DataRequest> dr, bool useMC, bool useFIT, bool tpcRefit, bool strict) : mDataRequest(dr), mUseMC(useMC), mUseFIT(useFIT), mDoTPCRefit(tpcRefit), mStrict(strict) {}
  ~TOFMatcherSpec() override = default;
  void init(InitContext& ic) final;
  void run(ProcessingContext& pc) final;
  void endOfStream(framework::EndOfStreamContext& ec) final;

 private:
  std::shared_ptr<DataRequest> mDataRequest;
  bool mUseMC = true;
  bool mUseFIT = false;
  bool mDoTPCRefit = false;
  bool mStrict = false;
  MatchTOF mMatcher; ///< Cluster finder
  TStopwatch mTimer;
};

void TOFMatcherSpec::init(InitContext& ic)
{
  mTimer.Stop();
  mTimer.Reset();
  //-------- init geometry and field --------//
  o2::base::GeometryManager::loadGeometry();
  o2::base::Propagator::initFieldFromGRP();
  std::unique_ptr<o2::parameters::GRPObject> grp{o2::parameters::GRPObject::loadFrom()};

  // this is a hack to provide Mat.LUT from the local file, in general will be provided by the framework from CCDB
  std::string matLUTPath = ic.options().get<std::string>("material-lut-path");
  std::string matLUTFile = o2::base::NameConf::getMatLUTFileName(matLUTPath);
  if (o2::utils::Str::pathExists(matLUTFile)) {
    auto* lut = o2::base::MatLayerCylSet::loadFromFile(matLUTFile);
    o2::base::Propagator::Instance()->setMatLUT(lut);
    LOG(INFO) << "Loaded material LUT from " << matLUTFile;
  } else {
    LOG(INFO) << "Material LUT " << matLUTFile << " file is absent, only TGeo can be used";
  }
  if (mStrict) {
    mMatcher.setHighPurity();
  }
}

void TOFMatcherSpec::run(ProcessingContext& pc)
{
  mTimer.Start(false);

  RecoContainer recoData;
  recoData.collectData(pc, *mDataRequest.get());

  LOG(INFO) << "isTrackSourceLoaded: TPC -> " << recoData.isTrackSourceLoaded(o2::dataformats::GlobalTrackID::Source::TPC);
  LOG(INFO) << "isTrackSourceLoaded: ITSTPC -> " << recoData.isTrackSourceLoaded(o2::dataformats::GlobalTrackID::Source::ITSTPC);
  LOG(INFO) << "isTrackSourceLoaded: TPCTRD -> " << recoData.isTrackSourceLoaded(o2::dataformats::GlobalTrackID::Source::TPCTRD);
  LOG(INFO) << "isTrackSourceLoaded: ITSTPCTRD -> " << recoData.isTrackSourceLoaded(o2::dataformats::GlobalTrackID::Source::ITSTPCTRD);

  bool isTPCused = recoData.isTrackSourceLoaded(o2::dataformats::GlobalTrackID::Source::TPC);
  bool isITSTPCused = recoData.isTrackSourceLoaded(o2::dataformats::GlobalTrackID::Source::ITSTPC);
  bool isTPCTRDused = recoData.isTrackSourceLoaded(o2::dataformats::GlobalTrackID::Source::TPCTRD);
  bool isITSTPCTRDused = recoData.isTrackSourceLoaded(o2::dataformats::GlobalTrackID::Source::ITSTPCTRD);
  uint32_t ss = o2::globaltracking::getSubSpec(mStrict ? o2::globaltracking::MatchingType::Strict : o2::globaltracking::MatchingType::Standard);
  mMatcher.setFIT(mUseFIT);

  mMatcher.run(recoData);

  if (isTPCused) {
    pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "MTC_TPC", ss, Lifetime::Timeframe}, mMatcher.getMatchedTrackVector(o2::dataformats::MatchInfoTOFReco::TrackType::TPC));
    if (mUseMC) {
      pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "MCMTC_TPC", ss, Lifetime::Timeframe}, mMatcher.getMatchedTOFLabelsVector(o2::dataformats::MatchInfoTOFReco::TrackType::TPC));
    }

    auto nmatch = mMatcher.getMatchedTrackVector(o2::dataformats::MatchInfoTOFReco::TrackType::TPC).size();
    if (mDoTPCRefit) {
      LOG(INFO) << "Refitting " << nmatch << " matched TPC tracks with TOF time info";
    } else {
      LOG(INFO) << "Shifting Z for " << nmatch << " matched TPC tracks according to TOF time info";
    }
    auto& tracksTPCTOF = pc.outputs().make<std::vector<o2::dataformats::TrackTPCTOF>>(OutputRef{"tpctofTracks", ss}, nmatch);
    mMatcher.makeConstrainedTPCTracks(tracksTPCTOF);
  }

  if (isITSTPCused) {
    pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "MTC_ITSTPC", 0, Lifetime::Timeframe}, mMatcher.getMatchedTrackVector(o2::dataformats::MatchInfoTOFReco::TrackType::ITSTPC));
    if (mUseMC) {
      pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "MCMTC_ITSTPC", 0, Lifetime::Timeframe}, mMatcher.getMatchedTOFLabelsVector(o2::dataformats::MatchInfoTOFReco::TrackType::ITSTPC));
    }
  }

  // TODO: TRD-matched tracks
  pc.outputs().snapshot(Output{o2::header::gDataOriginTOF, "CALIBDATA", 0, Lifetime::Timeframe}, mMatcher.getCalibVector());

  mTimer.Stop();
}

void TOFMatcherSpec::endOfStream(EndOfStreamContext& ec)
{
  LOGF(INFO, "TOF matching total timing: Cpu: %.3e Real: %.3e s in %d slots",
       mTimer.CpuTime(), mTimer.RealTime(), mTimer.Counter() - 1);
}

DataProcessorSpec getTOFMatcherSpec(GID::mask_t src, bool useMC, bool useFIT, bool tpcRefit, bool strict)
{
  uint32_t ss = o2::globaltracking::getSubSpec(strict ? o2::globaltracking::MatchingType::Strict : o2::globaltracking::MatchingType::Standard);
  auto dataRequest = std::make_shared<DataRequest>();
  if (strict) {
    dataRequest->setMatchingInputStrict();
  }
  dataRequest->requestTracks(src, useMC);
  dataRequest->requestClusters(GID::getSourceMask(GID::TOF), useMC);
  if (useFIT) {
    dataRequest->requestClusters(GID::getSourceMask(GID::FT0), false);
  }

  std::vector<OutputSpec> outputs;
  if (GID::includesSource(GID::TPC, src)) {
    outputs.emplace_back(o2::header::gDataOriginTOF, "MTC_TPC", ss, Lifetime::Timeframe);
    outputs.emplace_back(OutputLabel{"tpctofTracks"}, o2::header::gDataOriginTOF, "TOFTRACKS_TPC", ss, Lifetime::Timeframe);
    if (useMC) {
      outputs.emplace_back(o2::header::gDataOriginTOF, "MCMTC_TPC", ss, Lifetime::Timeframe);
    }
  }
  if (GID::includesSource(GID::ITSTPC, src)) {
    outputs.emplace_back(o2::header::gDataOriginTOF, "MTC_ITSTPC", 0, Lifetime::Timeframe);
    if (useMC) {
      outputs.emplace_back(o2::header::gDataOriginTOF, "MCMTC_ITSTPC", 0, Lifetime::Timeframe);
    }
  }
  if (GID::includesSource(GID::ITSTPCTRD, src)) {
    outputs.emplace_back(o2::header::gDataOriginTOF, "MTC_ITSTPCTRD", 0, Lifetime::Timeframe);
    if (useMC) {
      outputs.emplace_back(o2::header::gDataOriginTOF, "MCMTC_ITSTPCTRD", 0, Lifetime::Timeframe);
    }
  }
  if (GID::includesSource(GID::TPCTRD, src)) {
    outputs.emplace_back(o2::header::gDataOriginTOF, "MTC_TPCTRD", ss, Lifetime::Timeframe);
    if (useMC) {
      outputs.emplace_back(o2::header::gDataOriginTOF, "MCMTC_TPCTRD", ss, Lifetime::Timeframe);
    }
  }
  outputs.emplace_back(o2::header::gDataOriginTOF, "CALIBDATA", 0, Lifetime::Timeframe);

  return DataProcessorSpec{
    "tof-matcher",
    dataRequest->inputs,
    outputs,
    AlgorithmSpec{adaptFromTask<TOFMatcherSpec>(dataRequest, useMC, useFIT, tpcRefit, strict)},
    Options{
      {"material-lut-path", VariantType::String, "", {"Path of the material LUT file"}}}};
}

} // namespace globaltracking
} // namespace o2
